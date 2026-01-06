#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "object.h"
#include "memory.h"
#include "print.h"
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
  Writer writer = writerForFile(fd);
  writeValue(&writer, value);
}

bool valuesEqual(Value a, Value b) {
  if (IS_NUMBER(a) && IS_NUMBER(b)) {
    return AS_NUMBER(a) == AS_NUMBER(b);
  }
  // For strings, compare by value (length, hash, and content)
  if (IS_STRING(a) && IS_STRING(b)) {
    ObjString* aStr = AS_STRING(a);
    ObjString* bStr = AS_STRING(b);
    return aStr->length == bStr->length &&
           aStr->hash == bStr->hash &&
           memcmp(aStr->chars, bStr->chars, aStr->length) == 0;
  }
  return a == b;
}

void writeValue(Writer* writer, Value value) {
  if (IS_BOOL(value)) {
    writerWrite(writer, AS_BOOL(value) ? "true" : "false", AS_BOOL(value) ? 4 : 5);
  } else if (IS_NIL(value)) {
    writerWrite(writer, "nil", 3);
  } else if (IS_NUMBER(value)) {
    double num = AS_NUMBER(value);
    int64_t val_i = num;
    if (num == (double)val_i) {
      writerPrintf(writer, "%" PRId64, val_i);
    } else {
      writerPrintf(writer, "%f", num);
    }
  } else if (IS_OBJ(value)) {
    writeObject(writer, value);
  }
}

typedef struct {
  char* data;
  size_t len;
  size_t cap;
} StringWriter;

static void stringWriterWrite(void* ctx, const char* data, size_t len) {
  StringWriter* sw = (StringWriter*)ctx;
  if (len == 0) return;

  size_t needed = sw->len + len + 1;
  if (needed > sw->cap) {
    size_t newCap = sw->cap < 64 ? 64 : sw->cap;
    while (newCap < needed) newCap *= 2;
    sw->data = (char*)realloc(sw->data, newCap);
    if (sw->data == NULL) exit(1);
    sw->cap = newCap;
  }

  memcpy(sw->data + sw->len, data, len);
  sw->len += len;
  sw->data[sw->len] = '\0';
}

int valueToString(Value value, char** out) {
  StringWriter sw;
  sw.data = NULL;
  sw.len = 0;
  sw.cap = 0;

  Writer writer;
  writer.write = stringWriterWrite;
  writer.ctx = &sw;

  writeValue(&writer, value);

  if (sw.data == NULL) {
    sw.data = (char*)malloc(1);
    if (sw.data == NULL) exit(1);
    sw.data[0] = '\0';
  }

  *out = sw.data;
  if (sw.len > (size_t)INT_MAX) return INT_MAX;
  return (int)sw.len;
}
