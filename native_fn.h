#ifndef clox_native_fn_h
#define clox_native_fn_h

#include <stdio.h>
#include <time.h>
#include "object.h"

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
    COPY_STATIC_STRING(args[-1], "Error: first args must be a number");
    return false;
  }
  Value arg = args[0];
  if (!IS_NUMBER(arg)) {
    COPY_STATIC_STRING(args[-1], "Error: first args must be a number");
    return false;
  }
  double integer = (int)arg.as.number;
  args[-1] = NUMBER_VAL(integer);
  return true;
}

#endif
