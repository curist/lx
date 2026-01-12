#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "print.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

#define INTERN_MAX_LEN 64

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->isMarked = false;

  object->next = vm.objects;
  vm.objects = object;

#if defined(DEBUG_LOG_GC) && DEBUG_LOG_GC_VERBOSE
  printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif

  return object;
}

ObjClosure* newClosure(ObjFunction* function) {
  ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }

  ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjFunction* newFunction() {
  ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  function->filename = NULL;
  initChunk(&function->chunk);
  return function;
}

ObjNative* newNative(NativeFn function, ObjString* name) {
  ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  native->function = function;
  native->name = name;
  return native;
}

static ObjString* allocateString(char* chars, int length, uint64_t hash) {
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  push(OBJ_VAL(string));
  tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
  pop();

  return string;
}

static inline uint64_t rotl64(uint64_t x, int r) {
  return (x << r) | (x >> (64 - r));
}

static inline uint64_t read64(const uint8_t* p) {
  uint64_t v;
  memcpy(&v, p, sizeof(uint64_t));
  return v;
}

// xxHash64 (public domain) for fast, high-quality string hashing.
static uint64_t hashString(const char* key, size_t length) {
  const uint64_t PRIME64_1 = 11400714785074694791ULL;
  const uint64_t PRIME64_2 = 14029467366897019727ULL;
  const uint64_t PRIME64_3 =  1609587929392839161ULL;
  const uint64_t PRIME64_4 =  9650029242287828579ULL;
  const uint64_t PRIME64_5 =  2870177450012600261ULL;

  uint64_t h;
  const uint8_t* p = (const uint8_t*)key;
  const uint8_t* bEnd = p + length;

  if (length >= 32) {
    const uint8_t* limit = bEnd - 32;
    uint64_t v1 = PRIME64_1 + PRIME64_2;
    uint64_t v2 = PRIME64_2;
    uint64_t v3 = 0;
    uint64_t v4 = -PRIME64_1;

    do {
      v1 = rotl64(v1 + read64(p) * PRIME64_2, 31) * PRIME64_1; p += 8;
      v2 = rotl64(v2 + read64(p) * PRIME64_2, 31) * PRIME64_1; p += 8;
      v3 = rotl64(v3 + read64(p) * PRIME64_2, 31) * PRIME64_1; p += 8;
      v4 = rotl64(v4 + read64(p) * PRIME64_2, 31) * PRIME64_1; p += 8;
    } while (p <= limit);

    h = rotl64(v1, 1)  + rotl64(v2, 7)  + rotl64(v3, 12) + rotl64(v4, 18);

    v1 = rotl64(v1 * PRIME64_2, 31) * PRIME64_1; h ^= v1; h = h * PRIME64_1 + PRIME64_4;
    v2 = rotl64(v2 * PRIME64_2, 31) * PRIME64_1; h ^= v2; h = h * PRIME64_1 + PRIME64_4;
    v3 = rotl64(v3 * PRIME64_2, 31) * PRIME64_1; h ^= v3; h = h * PRIME64_1 + PRIME64_4;
    v4 = rotl64(v4 * PRIME64_2, 31) * PRIME64_1; h ^= v4; h = h * PRIME64_1 + PRIME64_4;
  } else {
    h = PRIME64_5;
  }

  h += (uint64_t)length;

  while (p + 8 <= bEnd) {
    uint64_t k1 = read64(p) * PRIME64_2;
    k1 = rotl64(k1, 31);
    k1 *= PRIME64_1;
    h ^= k1;
    h = rotl64(h, 27) * PRIME64_1 + PRIME64_4;
    p += 8;
  }

  while (p < bEnd) {
    h ^= (*p) * PRIME64_5;
    h = rotl64(h, 11) * PRIME64_1;
    p++;
  }

  h ^= h >> 33;
  h *= PRIME64_2;
  h ^= h >> 29;
  h *= PRIME64_3;
  h ^= h >> 32;
  return h;
}

ObjString* takeString(char* chars, int length) {
  uint64_t hash = hashString(chars, (size_t)length);

  if (length <= INTERN_MAX_LEN) {
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) {
      FREE_ARRAY(char, chars, length + 1);
      return interned;
    }

    return allocateString(chars, length, hash);
  }

  // Large string: do NOT intern
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  return string;
}

ObjString* copyString(const char* chars, size_t length) {
  if (length > (size_t)PTRDIFF_MAX) {
    // Guard against invalid length from signed conversion.
    length = 0;
  }
  uint64_t hash = hashString(chars, length);

  if (length <= (size_t)INTERN_MAX_LEN) {
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;
  }

  char* heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';

  if (length <= (size_t)INTERN_MAX_LEN) {
    return allocateString(heapChars, length, hash);
  }

  // Large string: heap-only
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = heapChars;
  string->hash = hash;
  return string;
}

ObjUpvalue* newUpvalue(Value* slot) {
  ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  upvalue->closed = NIL_VAL;
  upvalue->location = slot;
  upvalue->next = NULL;
  return upvalue;
}

ObjHashmap* newHashmap() {
  ObjHashmap* hashmap = ALLOCATE_OBJ(ObjHashmap, OBJ_HASHMAP);
  initTable(&hashmap->table);
  return hashmap;
}

ObjEnum* newEnum() {
  ObjEnum* e = ALLOCATE_OBJ(ObjEnum, OBJ_ENUM);
  initTable(&e->forward);
  initTable(&e->reverse);
  initValueArray(&e->names);
  return e;
}

ObjArray* newArray() {
  ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
  initValueArray(&array->array);
  return array;
}

void printObject(FILE* fd, Value value) {
  Writer writer = writerForFile(fd);
  writeObject(&writer, value);
}

static void writeFunction(Writer* writer, ObjFunction* function) {
  if (function->name == NULL) {
    writerWrite(writer, "<script>", 8);
    return;
  }
  writerPrintf(writer, "<fn %s>", function->name->chars);
}

void writeObject(Writer* writer, Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_CLOSURE:
      writeFunction(writer, AS_CLOSURE(value)->function);
      break;
    case OBJ_FUNCTION:
      writeFunction(writer, AS_FUNCTION(value));
      break;
    case OBJ_NATIVE:
      writerPrintf(writer, "<native fn: %s>", AS_NATIVE(value)->name->chars);
      break;
    case OBJ_STRING:
      {
        ObjString* s = AS_STRING(value);
        writerWrite(writer, s->chars, (size_t)s->length);
        break;
      }
      break;
    case OBJ_UPVALUE:
      writerWrite(writer, "upvalue", 7);
      break;
    case OBJ_HASHMAP:
      {
        writerWrite(writer, ".{", 2);
        Table* hashmap = &AS_HASHMAP(value);

        bool printed = false;

        for (int i = hashmap->arrayCapacity - 1; i >= 0; --i) {
          if (hashmap->arrayPresent != NULL && hashmap->arrayPresent[i]) {
            if (printed) writerWrite(writer, ",", 1);
            else printed = true;
            writeValue(writer, NUMBER_VAL((double)i));
            writerWrite(writer, ":", 1);
            writeValue(writer, hashmap->arrayValues[i]);
          }
        }

        for (int i = hashmap->capacity - 1; i >= 0; --i) {
          Entry* entry = &hashmap->entries[i];
          if (!IS_NIL(entry->key)) {
            if (printed) writerWrite(writer, ",", 1);
            else printed = true;
            writeValue(writer, entry->key);
            writerWrite(writer, ":", 1);
            writeValue(writer, entry->value);
          }
        }
        writerWrite(writer, "}", 1);
        break;
      }
    case OBJ_ENUM:
      {
        writerWrite(writer, "enum{", 5);
        ObjEnum* e = AS_ENUM(value);

        bool printed = false;

        for (int i = 0; i < e->names.count; i++) {
          Value key = e->names.values[i];
          Value val = NIL_VAL;
          (void)tableGet(&e->forward, key, &val);
          if (printed) writerWrite(writer, ",", 1);
          else printed = true;
          writeValue(writer, key);
          writerWrite(writer, ":", 1);
          writeValue(writer, val);
        }
        writerWrite(writer, "}", 1);
        break;
      }
    case OBJ_ARRAY:
      {
        ValueArray* values = &AS_ARRAY(value);
        writerWrite(writer, "[", 1);
        for (int i = 0; i < values->count; ++i) {
          if (i > 0) writerWrite(writer, ",", 1);
          writeValue(writer, values->values[i]);
        }
        writerWrite(writer, "]", 1);
        break;
      }
  }
}
