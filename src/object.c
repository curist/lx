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

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  push(OBJ_VAL(string));
  tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
  pop();

  return string;
}

static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

ObjString* takeString(char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }
  return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length) {
  uint32_t hash = hashString(chars, length);
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
