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

static bool ordNative(int argCount, Value* args) {
  if (argCount < 1) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Arg must be a char."));
    return false;
  }
  Value arg = args[0];
  if (!IS_STRING(arg) || AS_STRING(arg)->length != 1) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Arg must be a char."));
    return false;
  }
  char ch = AS_STRING(arg)->chars[0];
  args[-1] = NUMBER_VAL(ch);
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

static bool globalsNative(int argCount, Value* args) {
  Table* table = &vm.globals;
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
    double len = AS_STRING(arg)->length;
    args[-1] = NUMBER_VAL(len);
    return true;
  }
  if (IS_ARRAY(arg)) {
    double count = AS_ARRAY(arg).count;
    args[-1] = NUMBER_VAL(count);
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

static bool butlastNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: butlast takes 1 arg."));
    return false;
  }
  if (!IS_ARRAY(args[0])) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Can only butlast to array."));
    return false;
  }
  ValueArray* arr = &AS_ARRAY(args[0]);
  args[-1] = OBJ_VAL(newArray());
  for (int i = 0; i < arr->count - 1; i++) {
    writeValueArray(&AS_ARRAY(args[-1]), arr->values[i]);
  }
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

static bool assocNative(int argCount, Value* args) {
  if (argCount != 3) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: assoc takes 3 args."));
    return false;
  }
  if (!IS_HASHMAP(args[0])) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Can only assoc to map."));
    return false;
  }
  Value key = args[1];
  if (!IS_NUMBER(key) && !IS_STRING(key)) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Hashmap key type must be number or string."));
    return false;
  }
  args[-1] = OBJ_VAL(newHashmap());
  tableAddAll(&AS_HASHMAP(args[0]), &AS_HASHMAP(args[-1]));
  tableSet(&AS_HASHMAP(args[0]), key, args[2]);
  return true;
}

static bool concatNative(int argCount, Value* args) {
  if (argCount != 2) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: concat takes 2 args."));
    return false;
  }
  if (!IS_ARRAY(args[0]) || !IS_ARRAY(args[1])) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: Can only concat arrays."));
    return false;
  }
  ValueArray* arr1 = &AS_ARRAY(args[0]);
  ValueArray* arr2 = &AS_ARRAY(args[1]);
  args[-1] = OBJ_VAL(newArray());
  for (int i = 0; i < arr1->count; i++) {
    writeValueArray(&AS_ARRAY(args[-1]), arr1->values[i]);
  }
  for (int i = 0; i < arr2->count; i++) {
    writeValueArray(&AS_ARRAY(args[-1]), arr2->values[i]);
  }
  return true;
}

static bool rangeNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: range takes 1 args."));
    return false;
  }
  Value arg = args[0];
  if (IS_ARRAY(arg)) {
    args[-1] = arg;
  } else if(IS_NUMBER(arg)) {
    double dnum = AS_NUMBER(arg);
    int num = dnum;
    if (dnum != num || num < 0) {
      args[-1] = OBJ_VAL(
          COPY_CSTRING("Error: range number should be positive integer."));
      return false;
    }
    args[-1] = OBJ_VAL(newArray());
    for (int i = 0; i < num; i++) {
      writeValueArray(&AS_ARRAY(args[-1]), NUMBER_VAL(i));
    }
  } else {
    // every other value types start with an empty array
    args[-1] = OBJ_VAL(newArray());

    if (IS_STRING(arg))  {
      ObjString* s = AS_STRING(arg);
      for (int i = 0; i < s->length; i++) {
        writeValueArray(&AS_ARRAY(args[-1]), OBJ_VAL(COPY_CSTRING(&s->chars[i])));
      }
    } else if(IS_HASHMAP(arg)) {
      Table* table = &AS_HASHMAP(arg);
      for (int i = table->capacity - 1; i >= 0; --i) {
        Entry* entry = &table->entries[i];
        if (!IS_NIL(entry->key)) {
          writeValueArray(&AS_ARRAY(args[-1]), entry->key);
        }
      }
    }
  }
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
    int64_t val_i = num;
    char* str;
    if (num == val_i) {
      asprintf(&str, "%lld", val_i);
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

static bool exitNative(int argCount, Value* args) {
  int exitCode = 0;
  if (argCount == 0) {
  } else if (argCount != 1 || !IS_NUMBER(args[0])) {
    args[-1] = OBJ_VAL(COPY_CSTRING("Error: exit takes a number arg."));
    return false;
  } else {
    exitCode = AS_NUMBER(args[0]);
  }
  freeVM();
  exit(exitCode);
  return true;
}

// ---- end of native function declarations ----
// ---- end of native function declarations ----
// ---- end of native function declarations ----

static void defineTableFunction(Table* table, const char* name, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  tableSet(table, vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();
}

static void defineNative(const char* name, NativeFn function) {
  defineTableFunction(&vm.globals, name, function);
}

static void defineLxNatives() {
  push(OBJ_VAL(COPY_CSTRING("Lx")));
  push(OBJ_VAL(newHashmap()));
  tableSet(&vm.globals, vm.stack[0], vm.stack[1]);

  push(OBJ_VAL(COPY_CSTRING("args")));
  push(OBJ_VAL(newArray()));
  tableSet(&AS_HASHMAP(vm.stack[1]), vm.stack[2], vm.stack[3]);
  for (int i = 0; i < LX_ARGC; i++) {
    push(OBJ_VAL(COPY_CSTRING(LX_ARGV[i])));
    writeValueArray(&AS_ARRAY(vm.stack[3]), vm.stack[4]);
    pop();
  }
  pop(); pop();

  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "globals", globalsNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "exit", exitNative);

  pop();
  pop();
}

void defineBuiltinNatives() {
  defineLxNatives();

  defineNative("clock", clockNative);
  defineNative("print", printNative);
  defineNative("str", strNative);
  defineNative("int", intNative);
  defineNative("ord", ordNative);
  defineNative("keys", keysNative);
  defineNative("len", lenNative);
  defineNative("type", typeNative);
  defineNative("append", appendNative);
  defineNative("butlast", butlastNative);
  defineNative("push", pushNative);
  defineNative("pop", popNative);
  defineNative("assoc", assocNative);
  defineNative("concat", concatNative);
  defineNative("range", rangeNative);
  // TODO: slurp
}
#endif
