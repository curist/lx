#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->isMarked = false;

  object->next = vm.objects;
  vm.objects = object;

#ifdef DEBUG_LOG_GC
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
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }
  return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length) {
  uint64_t hash = hashString(chars, (size_t)length);
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) return interned;
  char* heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  return allocateString(heapChars, length, hash);
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

ObjArray* newArray() {
  ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
  initValueArray(&array->array);
  return array;
}

static void printFunction(FILE* fd, ObjFunction* function) {
  if (function->name == NULL) {
    fprintf(fd, "<script>");
    return;
  }
  fprintf(fd, "<fn %s>", function->name->chars);
}

void printObject(FILE* fd, Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_CLOSURE:
      printFunction(fd, AS_CLOSURE(value)->function);
      break;
    case OBJ_FUNCTION:
      printFunction(fd, AS_FUNCTION(value));
      break;
    case OBJ_NATIVE:
      fprintf(fd, "<native fn: %s>", AS_NATIVE(value)->name->chars);
      break;
    case OBJ_STRING:
      fprintf(fd, "%s", AS_CSTRING(value));
      break;
    case OBJ_UPVALUE:
      fprintf(fd, "upvalue");
      break;
    case OBJ_HASHMAP:
      {
        fprintf(fd, ".{");
        Table* hashmap = &AS_HASHMAP(value);

        bool printed = false;

        for (int i = hashmap->capacity - 1; i >= 0; --i) {
          Entry* entry = &hashmap->entries[i];
          if (!IS_NIL(entry->key)) {
            if (printed) fprintf(fd, ",");
            else printed = true;
            printValue(fd, entry->key);
            fprintf(fd, ":");
            printValue(fd, entry->value);
          }
        }
        fprintf(fd, "}");
        break;
      }
    case OBJ_ARRAY:
      {
        ValueArray* values = &AS_ARRAY(value);
        fprintf(fd, "[");
        for (int i = 0; i < values->count; ++i) {
          if (i > 0) fprintf(fd, ",");
          printValue(fd, values->values[i]);
        }
        fprintf(fd, "]");
        break;
      }
  }
}
