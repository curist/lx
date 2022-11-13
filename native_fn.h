#ifndef clox_native_fn_h
#define clox_native_fn_h

#include <stdio.h>
#include <time.h>

#include "vm.h"
#include "object.h"

void defineBuiltinNatives();

static bool clockNative(int argCount, Value* args) {
  args[-1] = NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
  return true;
}

static bool printNative(int argCount, Value* args) {
  for (int i = 0; i < argCount; i++) {
    if (i > 0) printf(" ");
    printValue(args[i]);
  }
  printf("\n");
  args[-1] = NIL_VAL;
  return true;
}

static bool intNative(int argCount, Value* args) {
  if (argCount < 1) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Arg must be a number."));
    return false;
  }
  Value arg = args[0];
  if (!IS_NUMBER(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Arg must be a number."));
    return false;
  }
  double integer = (int)AS_NUMBER(arg);
  args[-1] = NUMBER_VAL(integer);
  return true;
}

static bool keysNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Arg must be a map."));
    return false;
  }
  Value arg = args[0];
  if (!IS_HASHMAP(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Arg must be a map."));
    return false;
  }
  Table* table = &AS_HASHMAP(arg);
  ObjArray* array = newArray();
  args[-1] = OBJ_VAL(array);

  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (!IS_NIL(entry->key)) {
      writeValueArray(&array->array, entry->key);
    }
  }
  return true;
}

static bool lenNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Arg must be string or array."));
    return false;
  }
  Value arg = args[0];
  if (IS_STRING(arg)) {
    args[-1] = numToValue(AS_STRING(arg)->length);
    return true;
  }
  if (IS_ARRAY(arg)) {
    args[-1] = numToValue(AS_ARRAY(arg).count);
    return true;
  }

  args[-1] = OBJ_VAL(COPY_CSTRING("Error: Arg must be string or array."));
  return false;
}


static void defineNative(const char* name, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  tableSet(&vm.globals, OBJ_VAL(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

void defineBuiltinNatives() {
  defineNative("clock", clockNative);
  defineNative("print", printNative);
  defineNative("int", intNative);
  defineNative("keys", keysNative);
  defineNative("len", lenNative);
}
#endif
