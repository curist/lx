#ifndef clox_native_fn_h
#define clox_native_fn_h

#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#include "vm.h"
#include "object.h"

void defineBuiltinNatives();


// ---- start of native function declarations ----
// ---- start of native function declarations ----
// ---- start of native function declarations ----


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

  for (int i = table->capacity - 1; i >= 0; --i) {
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

static bool typeNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: type takes 1 arg."));
    return false;
  }
  Value arg = args[0];
  if (IS_NIL(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("nil"));
  } else if (IS_BOOL(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("boolean"));
  } else if (IS_NUMBER(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("number"));
  } else if (IS_CLOSURE(arg) || IS_FUNCTION(arg) || IS_NATIVE(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("fn"));
  } else if (IS_STRING(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("string"));
  } else if (IS_HASHMAP(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("map"));
  } else if (IS_ARRAY(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("array"));
  } else {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: unknown type."));
    return false;
  }

  return true;
}

static bool appendNative(int argCount, Value* args) {
  if (argCount != 2) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: append takes 2 args."));
    return false;
  }
  if (!IS_ARRAY(args[0])) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Can only append to array."));
    return false;
  }
  ValueArray* arr = &AS_ARRAY(args[0]);
  args[-1] = OBJ_VAL(newArray());
  for (int i = 0; i < arr->count; i++) {
    writeValueArray(&AS_ARRAY(args[-1]), arr->values[i]);
  }
  writeValueArray(&AS_ARRAY(args[-1]), args[1]);
  return true;
}

static bool pushNative(int argCount, Value* args) {
  if (argCount != 2) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: push takes 2 args."));
    return false;
  }
  if (!IS_ARRAY(args[0])) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Can only push to array."));
    return false;
  }
  args[-1] = args[0];
  writeValueArray(&AS_ARRAY(args[0]), args[1]);
  return true;
}

static bool popNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: pop takes 1 arg."));
    return false;
  }
  if (!IS_ARRAY(args[0])) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Can only pop from array."));
    return false;
  }
  ValueArray* arr = &AS_ARRAY(args[0]);
  if (arr->count == 0) {
    args[-1] = NIL_VAL;
    return true;
  }
  args[-1] = arr->values[--arr->count];
  return true;
}

static bool strNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: str takes 1 arg."));
    return false;
  }
  Value arg = args[0];
  if (IS_NUMBER(arg)) {
    double num = AS_NUMBER(arg);
    int val_i = num;
    char* str;
    if (num == val_i) {
      asprintf(&str, "%d", val_i);
    } else {
      asprintf(&str, "%f", num);
    }
    args[-1] = OBJ_VAL(COPY_CSTRING(str));
    free(str);
  } else if (IS_BOOL(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING(AS_BOOL(arg) ? "true" : "false"));
  } else if (IS_NIL(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("nil"));
  } else if (IS_STRING(arg)) {
    args[-1] = arg;
  } else if (IS_NATIVE(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("<native fn>"));
  } else if (IS_FUNCTION(arg)) {
    // XXX: compose fn name as part of return value
    args[-1] = OBJ_VAL(COPY_CSTRING("<fn>"));
  } else if (IS_CLOSURE(arg)) {
    // XXX: compose fn name as part of return value
    args[-1] = OBJ_VAL(COPY_CSTRING("<fn>"));
  } else if (IS_HASHMAP(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("<map>"));
  } else if (IS_ARRAY(arg)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("<array>"));
  } else {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: unknown type."));
    return false;
  }
  return true;
}

// ---- end of native function declarations ----
// ---- end of native function declarations ----
// ---- end of native function declarations ----

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
  defineNative("type", typeNative);
  defineNative("str", strNative);
  defineNative("append", appendNative);
  defineNative("push", pushNative);
  defineNative("pop", popNative);
}
#endif
