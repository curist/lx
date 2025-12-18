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
  table->tombstones = 0;
  table->capacity = 0;
  table->bucketCount = 0;
  table->bucketMask = 0;
  table->entries = NULL;
  table->control = NULL;
}

void freeTable(Table* table) {
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
#ifdef NAN_BOXING
  if (IS_STRING(value)) {
    return (uint32_t)AS_STRING(value)->hash;
  } else if (IS_NUMBER(value)) {
    return hashDouble(valueToNum(value));
  } else {
    return 0;
  }
#else
  switch (value.type) {
    case VAL_NUMBER: return hashDouble(AS_NUMBER(value));
    case VAL_OBJ: {
      if (IS_STRING(value)) {
        return (uint32_t)AS_STRING(value)->hash;
      }
      return 0;
    }
    default: return 0;
  }
#endif
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
  double projected = (double)(table->count + table->tombstones + 1);
  if (projected > (double)table->capacity * TABLE_MAX_LOAD) return true;
  return table->tombstones > table->count / 2;
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
  table->count = 0;
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

  uint32_t hash = hashValue(key);
  Entry* entry = findExisting(table, key, hash);
  if (entry == NULL) return false;

  *value = entry->value;
  return true;
}

bool tableSet(Table* table, Value key, Value value) {
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
  for (int i = 0; i < from->capacity; i++) {
    if (from->control != NULL && (from->control[i] & CTRL_EMPTY) != 0) continue;
    Entry* entry = &from->entries[i];
    if (!IS_NIL(entry->key)) {
      tableSet(to, entry->key, entry->value);
    }
  }
}

ObjString* tableFindString(Table* table, const char* chars, int length, uint64_t hash) {
  if (table->count == 0) return NULL;

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
  for (int i = 0; i < table->capacity; i++) {
    if (table->control != NULL && (table->control[i] & CTRL_EMPTY) != 0) continue;
    Entry* entry = &table->entries[i];
    if (IS_STRING(entry->key)) {
      markObject(AS_OBJ(entry->key));
    }
    markValue(entry->value);
  }
}
