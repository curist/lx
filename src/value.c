#include <stdio.h>
#include <string.h>

#include "object.h"
#include "memory.h"
#include "value.h"

void initValueArray(ValueArray* array) {
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values = GROW_ARRAY(Value, array->values,
                               oldCapacity, array->capacity);
  }

  array->values[array->count] = value;
  array->count++;
}

void freeValueArray(ValueArray* array) {
  FREE_ARRAY(Value, array->values, array->capacity);
  initValueArray(array);
}

void printValue(FILE* fd, Value value) {
#ifdef NAN_BOXING
  if (IS_BOOL(value)) {
    fprintf(fd, AS_BOOL(value) ? "true" : "false");
  } else if (IS_NIL(value)) {
    fprintf(fd, "nil");
  } else if (IS_NUMBER(value)) {
    double num = AS_NUMBER(value);
    int64_t val_i = num;
    if (num == val_i) {
      fprintf(fd, "%lld", val_i);
    } else {
      fprintf(fd, "%f", num);
    }
  } else if (IS_OBJ(value)) {
    printObject(fd, value);
  }
#else
  switch (value.type) {
    case VAL_BOOL:
      fprintf(fd, AS_BOOL(value) ? "true" : "false");
      break;
    case VAL_NIL: fprintf(fd, "nil"); break;
    case VAL_NUMBER: {
      double num = AS_NUMBER(value);
      int val_i = num;
      if (num == val_i) {
        fprintf(fd, "%d", val_i);
      } else {
        fprintf(fd, "%f", num);
      }
      break;
    }
    case VAL_OBJ: printObject(fd, value); break;
  }
#endif
}

bool valuesEqual(Value a, Value b) {
#ifdef NAN_BOXING
  return a == b;
#else
  if (a.type != b.type) return false;
  switch (a.type) {
    case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:    return true;
    case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ:    return AS_OBJ(a) == AS_OBJ(b);
    default:         return false; // Unreachable.
  }
#endif
}
