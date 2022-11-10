#ifndef clox_native_fn_h
#define clox_native_fn_h

#include <stdio.h>
#include <time.h>
#include "object.h"

static Value clockNative(int argCount, Value* args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value printNative(int argCount, Value* args) {
  for (int i = 0; i < argCount; i++) {
    if (i > 0) printf(" ");
    printValue(args[i]);
  }
  printf("\n");
  return NIL_VAL;
}

static Value intNative(int argCount, Value* args) {
  if (argCount < 1) {
    return NIL_VAL;
  }
  Value arg = args[0];
  if (IS_NUMBER(arg)) {
    double integer = (int)arg.as.number;
    return NUMBER_VAL(integer);
  }
  return NIL_VAL;
}

#endif