#ifndef clox_objloader_h
#define clox_objloader_h
#include "vm.h"
#include "memory.h"
#include "object.h"

typedef struct {
  uint32_t count;
  uint32_t capacity;
  Value** values;
} ValuePointers;

static void initValuePointers(ValuePointers* array) {
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}
static void writeValuePointers(ValuePointers* array, Value* value) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values = GROW_ARRAY(Value*, array->values,
                               oldCapacity, array->capacity);
  }
  array->values[array->count] = value;
  array->count++;
}
static void freeValuePointers(ValuePointers* array) {
  FREE_ARRAY(Value*, array->values, array->capacity);
  initValuePointers(array);
}

ObjFunction* loadObj(uint8_t* bytes);

#endif
