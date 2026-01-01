#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.6
#define GROUP_WIDTH TABLE_GROUP_WIDTH
#define GROUP_MASK 0xFFu
#define H2_MASK    0x7F

void initTable(Table* table) {
  table->count = 0;
  table->arrayCount = 0;
  table->arrayCapacity = 0;
  table->arrayValues = NULL;
  table->arrayPresent = NULL;
  table->hasIntKeysInHash = false;
  table->tombstones = 0;
  table->capacity = 0;
  table->bucketCount = 0;
  table->bucketMask = 0;
  table->entries = NULL;
  table->control = NULL;
}

void freeTable(Table* table) {
  FREE_ARRAY(Value, table->arrayValues, table->arrayCapacity);
  FREE_ARRAY(uint8_t, table->arrayPresent, table->arrayCapacity);
  FREE_ARRAY(Entry, table->entries, table->capacity);
  FREE_ARRAY(uint8_t, table->control, table->capacity);
  initTable(table);
}

static uint32_t hashDouble(double value) {
  // Normalize -0.0 and +0.0 to the same hash.
  if (value == 0) value = 0;

  uint64_t bits;
  memcpy(&bits, &value, sizeof(uint64_t));

  // 64-bit mix (similar to splitmix64 finalizer).
  bits ^= bits >> 33;
  bits *= 0xff51afd7ed558ccdULL;
  bits ^= bits >> 33;
  bits *= 0xc4ceb9fe1a85ec53ULL;
  bits ^= bits >> 33;
  return (uint32_t)(bits ^ (bits >> 32));
}

uint32_t hashValue(Value value) {
  if (IS_STRING(value)) {
    return (uint32_t)AS_STRING(value)->hash;
  } else if (IS_NUMBER(value)) {
    return hashDouble(AS_NUMBER(value));
  } else {
    return 0;
  }
}

static inline Value normalizeNumberKey(Value key) {
  // Only flonums need normalization for -0.0/+0.0; fixnums are already canonical
  if (IS_NUMBER(key)) {
    uint64_t bits = (uint64_t)key;
    if ((bits << 1) == 0) return NUMBER_VAL(0);
  }
  return key;
}

static inline bool numberKeyToArrayIndex(Value key, uint32_t* index) {
  if (!IS_NUMBER(key)) return false;

  if (IS_FIXNUM(key)) {
    int64_t n = AS_FIXNUM(key);
    if (n < 0 || (uint64_t)n > UINT32_MAX) return false;
    *index = (uint32_t)n;
    return true;
  }

  uint64_t bits = (uint64_t)key;
  // Accept both +0.0 and -0.0 as index 0.
  if ((bits << 1) == 0) {
    *index = 0;
    return true;
  }

  // Negative numbers are never array indices.
  if ((bits & SIGN_BIT) != 0) return false;

  // IEEE-754 binary64: sign(1) exponent(11) mantissa(52)
  uint32_t exponent = (uint32_t)((bits >> 52) & 0x7FFu);
  if (exponent == 0 || exponent == 0x7FFu) return false;

  int32_t e = (int32_t)exponent - 1023;
  if (e < 0 || e > 31) return false;

  uint64_t mantissa = bits & ((1ULL << 52) - 1);
  uint64_t sig = (1ULL << 52) | mantissa;
  int shift = 52 - e;

  uint64_t val;
  if (shift > 0) {
    uint64_t fracMask = (1ULL << shift) - 1;
    if ((sig & fracMask) != 0) return false;
    val = sig >> shift;
  } else {
    val = sig << (-shift);
  }

  if (val > UINT32_MAX) return false;
  *index = (uint32_t)val;
  return true;
}

static bool shouldGrowArrayForIndex(const Table* table, uint32_t index) {
  // Only grow for indices that are plausibly part of a dense int-key map.
  // This avoids allocating enormous arrays for sparse outliers.
  uint32_t threshold = (uint32_t)(table->count * 2 + 8);
  return index <= threshold;
}

static void ensureArrayCapacity(Table* table, uint32_t minCapacity) {
  if (minCapacity <= (uint32_t)table->arrayCapacity) return;

  int newCapacity = table->arrayCapacity < 8 ? 8 : table->arrayCapacity;
  while ((uint32_t)newCapacity < minCapacity) newCapacity *= 2;

  Value* newValues = ALLOCATE(Value, newCapacity);
  uint8_t* newPresent = ALLOCATE(uint8_t, newCapacity);
  for (int i = 0; i < newCapacity; i++) {
    newValues[i] = NIL_VAL;
    newPresent[i] = 0;
  }

  for (int i = 0; i < table->arrayCapacity; i++) {
    newValues[i] = table->arrayValues[i];
    newPresent[i] = table->arrayPresent[i];
  }

  FREE_ARRAY(Value, table->arrayValues, table->arrayCapacity);
  FREE_ARRAY(uint8_t, table->arrayPresent, table->arrayCapacity);
  table->arrayValues = newValues;
  table->arrayPresent = newPresent;
  table->arrayCapacity = newCapacity;
}

static inline uint8_t h2hash(uint32_t hash) { return (uint8_t)(hash & H2_MASK); }
static inline uint32_t h1hash(uint32_t hash, uint32_t mask) { return (hash >> 7) & mask; }
static inline Entry* entryAt(Table* table, uint32_t group, uint32_t offset) {
  return &table->entries[group * GROUP_WIDTH + offset];
}
static inline uint8_t* ctrlAt(Table* table, uint32_t group) {
  return &table->control[group * GROUP_WIDTH];
}

static inline uint32_t matchByte(const uint8_t* control, uint8_t byte) {
  uint32_t mask = 0;
  for (int i = 0; i < GROUP_WIDTH; i++) {
    mask |= ((uint32_t)(control[i] == byte)) << i;
  }
  return mask;
}

static inline uint32_t matchH2(const uint8_t* control, uint8_t h2) { return matchByte(control, h2); }
static inline uint32_t matchEmpty(const uint8_t* control) { return matchByte(control, CTRL_EMPTY); }
static inline uint32_t matchTomb(const uint8_t* control) { return matchByte(control, CTRL_TOMB); }
static inline uint32_t matchEmptyOrTomb(const uint8_t* control) {
  return matchEmpty(control) | matchTomb(control);
}

static inline int lowestBitIndex(uint32_t mask) { return __builtin_ctz(mask); }

static bool tableShouldGrow(Table* table) {
  if (table->bucketCount == 0) return true;
  int hashCount = table->count - table->arrayCount;
  double projected = (double)(hashCount + table->tombstones + 1);
  if (projected > (double)table->capacity * TABLE_MAX_LOAD) return true;
  return table->tombstones > hashCount / 2;
}

static void adjustCapacity(Table* table, int capacity) {
  if (capacity < GROUP_WIDTH) capacity = GROUP_WIDTH;
  capacity = (capacity + GROUP_WIDTH - 1) & ~(GROUP_WIDTH - 1);

  int bucketCount = capacity / GROUP_WIDTH;
  uint32_t bucketMask = (uint32_t)(bucketCount - 1);

  Entry* entries = ALLOCATE(Entry, capacity);
  uint8_t* control = ALLOCATE(uint8_t, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NIL_VAL;
    entries[i].value = NIL_VAL;
    control[i] = CTRL_EMPTY;
  }

  Entry* oldEntries = table->entries;
  uint8_t* oldControl = table->control;
  int oldCapacity = table->capacity;

  table->entries = entries;
  table->control = control;
  table->capacity = capacity;
  table->bucketCount = bucketCount;
  table->bucketMask = bucketMask;
  table->count = table->arrayCount;
  table->tombstones = 0;

  if (oldEntries != NULL) {
    for (int i = 0; i < oldCapacity; i++) {
      if (oldControl != NULL && (oldControl[i] & CTRL_EMPTY) != 0) continue;
      if (IS_NIL(oldEntries[i].key)) continue;

      Value key = oldEntries[i].key;
      Value value = oldEntries[i].value;
      uint32_t hash = hashValue(key);
      uint8_t h2 = h2hash(hash);
      uint32_t group = h1hash(hash, table->bucketMask);
      for (;;) {
        uint8_t* ctrl = ctrlAt(table, group);
        uint32_t empties = matchEmptyOrTomb(ctrl);
        if (empties != 0) {
          int offset = lowestBitIndex(empties);
          Entry* dest = entryAt(table, group, (uint32_t)offset);
          dest->key = key;
          dest->value = value;
          ctrl[offset] = h2;
          table->count++;
          break;
        }
        group = (group + 1) & table->bucketMask;
      }
    }
    FREE_ARRAY(Entry, oldEntries, oldCapacity);
    FREE_ARRAY(uint8_t, oldControl, oldCapacity);
  }
}

typedef struct {
  Entry* entry;
  uint8_t* ctrl;
  bool found;
} ProbeResult;

static ProbeResult findSlot(Table* table, Value key, uint32_t hash) {
  uint8_t h2 = h2hash(hash);
  uint32_t mask = table->bucketMask;
  uint32_t group = h1hash(hash, mask);

  Entry* firstTombEntry = NULL;
  uint8_t* firstTombCtrl = NULL;

  for (;;) {
    uint8_t* ctrl = ctrlAt(table, group);
    uint32_t match = matchH2(ctrl, h2);
    while (match) {
      int offset = lowestBitIndex(match);
      Entry* entry = entryAt(table, group, (uint32_t)offset);
      if (!IS_NIL(entry->key) && valuesEqual(entry->key, key)) {
        return (ProbeResult){entry, ctrl + offset, true};
      }
      match &= match - 1;
    }

    uint32_t emptyMask = matchEmpty(ctrl);
    uint32_t tombMask = matchTomb(ctrl);
    if (firstTombEntry == NULL && tombMask != 0) {
      int offset = lowestBitIndex(tombMask);
      firstTombEntry = entryAt(table, group, (uint32_t)offset);
      firstTombCtrl = ctrl + offset;
    }
    if (emptyMask != 0) {
      if (firstTombEntry != NULL) {
        return (ProbeResult){firstTombEntry, firstTombCtrl, false};
      }
      int offset = lowestBitIndex(emptyMask);
      return (ProbeResult){entryAt(table, group, (uint32_t)offset), ctrl + offset, false};
    }

    group = (group + 1) & mask;
  }
}

static Entry* findExisting(Table* table, Value key, uint32_t hash) {
  if (table->bucketCount == 0) return NULL;
  uint8_t h2 = h2hash(hash);
  uint32_t mask = table->bucketMask;
  uint32_t group = h1hash(hash, mask);

  for (;;) {
    uint8_t* ctrl = ctrlAt(table, group);
    uint32_t match = matchH2(ctrl, h2);
    while (match) {
      int offset = lowestBitIndex(match);
      Entry* entry = entryAt(table, group, (uint32_t)offset);
      if (!IS_NIL(entry->key) && valuesEqual(entry->key, key)) {
        return entry;
      }
      match &= match - 1;
    }

    if (matchEmpty(ctrl) != 0) return NULL;
    group = (group + 1) & mask;
  }
}

bool tableGet(Table* table, Value key, Value* value) {
  if (table->count == 0) return false;

  key = normalizeNumberKey(key);

  uint32_t arrayIndex;
  if (numberKeyToArrayIndex(key, &arrayIndex)) {
    if (arrayIndex < (uint32_t)table->arrayCapacity) {
      if (table->arrayPresent[arrayIndex]) {
        *value = table->arrayValues[arrayIndex];
        return true;
      }
      if (!table->hasIntKeysInHash) return false;
    } else if (!table->hasIntKeysInHash) {
      return false;
    }
  }

  uint32_t hash = hashValue(key);
  Entry* entry = findExisting(table, key, hash);
  if (entry == NULL) return false;

  *value = entry->value;
  return true;
}

bool tableSet(Table* table, Value key, Value value) {
  key = normalizeNumberKey(key);

  uint32_t arrayIndex;
  if (numberKeyToArrayIndex(key, &arrayIndex)) {
    if (arrayIndex < (uint32_t)table->arrayCapacity ||
        shouldGrowArrayForIndex(table, arrayIndex)) {
      bool existedInHash = false;
      if (table->hasIntKeysInHash && table->bucketCount != 0) {
        uint32_t hash = hashValue(key);
        Entry* existing = findExisting(table, key, hash);
        if (existing != NULL) {
          int index = (int)(existing - table->entries);
          table->control[index] = CTRL_TOMB;
          existing->key = NIL_VAL;
          existing->value = NIL_VAL;
          table->count--;
          table->tombstones++;
          existedInHash = true;
        }
      }

      if (arrayIndex >= (uint32_t)table->arrayCapacity) {
        ensureArrayCapacity(table, arrayIndex + 1);
      }

      bool wasPresent = table->arrayPresent[arrayIndex];
      if (!wasPresent) {
        table->arrayPresent[arrayIndex] = 1;
        table->arrayCount++;
        table->count++;
      }
      table->arrayValues[arrayIndex] = value;
      return !wasPresent && !existedInHash;
    }
    table->hasIntKeysInHash = true;
  }

  if (tableShouldGrow(table)) {
    int newCapacity = table->capacity == 0 ? GROUP_WIDTH : GROW_CAPACITY(table->capacity);
    adjustCapacity(table, newCapacity);
  }

  uint32_t hash = hashValue(key);
  ProbeResult probe = findSlot(table, key, hash);
  if (probe.found) {
    probe.entry->value = value;
    return false;
  }

  if (*probe.ctrl == CTRL_TOMB) {
    table->tombstones--;
  }

  probe.entry->key = key;
  probe.entry->value = value;
  *probe.ctrl = h2hash(hash);
  table->count++;
  return true;
}

bool tableDelete(Table* table, Value key) {
  if (table->count == 0) return false;

  key = normalizeNumberKey(key);

  uint32_t arrayIndex;
  if (numberKeyToArrayIndex(key, &arrayIndex)) {
    bool deleted = false;
    if (arrayIndex < (uint32_t)table->arrayCapacity) {
      if (table->arrayPresent[arrayIndex]) {
        table->arrayPresent[arrayIndex] = 0;
        table->arrayValues[arrayIndex] = NIL_VAL;
        table->arrayCount--;
        table->count--;
        deleted = true;
      }
      if (!table->hasIntKeysInHash) return deleted;
    } else if (!table->hasIntKeysInHash) {
      return false;
    }

    uint32_t hash = hashValue(key);
    Entry* entry = findExisting(table, key, hash);
    if (entry == NULL) return deleted;

    int index = (int)(entry - table->entries);
    table->control[index] = CTRL_TOMB;
    entry->key = NIL_VAL;
    entry->value = NIL_VAL;
    table->count--;
    table->tombstones++;
    return true;
  }

  uint32_t hash = hashValue(key);
  Entry* entry = findExisting(table, key, hash);
  if (entry == NULL) return false;

  int index = (int)(entry - table->entries);
  table->control[index] = CTRL_TOMB;
  entry->key = NIL_VAL;
  entry->value = NIL_VAL;
  table->count--;
  table->tombstones++;
  return true;
}

void tableAddAll(Table* from, Table* to) {
  for (int i = 0; i < from->arrayCapacity; i++) {
    if (from->arrayPresent != NULL && from->arrayPresent[i]) {
      tableSet(to, NUMBER_VAL((double)i), from->arrayValues[i]);
    }
  }
  for (int i = 0; i < from->capacity; i++) {
    if (from->control != NULL && (from->control[i] & CTRL_EMPTY) != 0) continue;
    Entry* entry = &from->entries[i];
    if (!IS_NIL(entry->key)) {
      tableSet(to, entry->key, entry->value);
    }
  }
}

ObjString* tableFindString(Table* table, const char* chars, int length, uint64_t hash) {
  if (table->bucketCount == 0) return NULL;

  uint32_t hash32 = (uint32_t)hash;
  uint8_t h2 = h2hash(hash32);
  uint32_t mask = table->bucketMask;
  uint32_t group = h1hash(hash32, mask);

  for (;;) {
    uint8_t* ctrl = ctrlAt(table, group);
    uint32_t match = matchH2(ctrl, h2);
    while (match) {
      int offset = lowestBitIndex(match);
      Entry* entry = entryAt(table, group, (uint32_t)offset);
      if (IS_STRING(entry->key)) {
        ObjString* str = AS_STRING(entry->key);
        if (str->length == (size_t)length &&
            str->hash == hash &&
            memcmp(str->chars, chars, length) == 0) {
          return str;
        }
      }
      match &= match - 1;
    }

    if (matchEmpty(ctrl) != 0) return NULL;
    group = (group + 1) & mask;
  }
}

void tableRemoveWhite(Table* table) {
  for (int i = 0; i < table->capacity; i++) {
    if (table->control != NULL && (table->control[i] & CTRL_EMPTY) != 0) continue;

    Entry* entry = &table->entries[i];
    if (!IS_NIL(entry->key) && IS_STRING(entry->key) &&
        !AS_STRING(entry->key)->obj.isMarked) {
      tableDelete(table, entry->key);
    }
  }
}

void markTable(Table* table) {
  for (int i = 0; i < table->arrayCapacity; i++) {
    if (table->arrayPresent != NULL && table->arrayPresent[i]) {
      markValue(table->arrayValues[i]);
    }
  }
  for (int i = 0; i < table->capacity; i++) {
    if (table->control != NULL && (table->control[i] & CTRL_EMPTY) != 0) continue;
    Entry* entry = &table->entries[i];
    if (IS_STRING(entry->key)) {
      markObject(AS_OBJ(entry->key));
    }
    markValue(entry->value);
  }
}
