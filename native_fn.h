#ifndef clox_native_fn_h
#define clox_native_fn_h

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

#ifndef __EMSCRIPTEN__
#include <wordexp.h>
#endif

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

static bool groanNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: Arg must be a string.");
    return false;
  }
  ObjString* s = AS_STRING(args[0]);
  fprintf(stderr, "%s\n", s->chars);
  args[-1] = NIL_VAL;
  return true;
}

static bool intNative(int argCount, Value* args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: Arg must be a number.");
    return false;
  }
  Value arg = args[0];
  if (!IS_NUMBER(arg)) {
    args[-1] = CSTRING_VAL("Error: Arg must be a number.");
    return false;
  }
  double integer = (int)AS_NUMBER(arg);
  args[-1] = NUMBER_VAL(integer);
  return true;
}

static bool ordNative(int argCount, Value* args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: Arg must be a char.");
    return false;
  }
  Value arg = args[0];
  if (!IS_STRING(arg) || AS_STRING(arg)->length != 1) {
    args[-1] = CSTRING_VAL("Error: Arg must be a single char.");
    return false;
  }
  uint8_t ch = AS_STRING(arg)->chars[0];
  args[-1] = NUMBER_VAL(ch);
  return true;
}

static bool keysNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = CSTRING_VAL("Error: Arg must be a map.");
    return false;
  }
  Value arg = args[0];
  if (!IS_HASHMAP(arg)) {
    args[-1] = CSTRING_VAL("Error: Arg must be a map.");
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
    args[-1] = CSTRING_VAL("Error: Arg must be string or array.");
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

  args[-1] = CSTRING_VAL("Error: Arg must be string or array.");
  return false;
}

static bool typeNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = CSTRING_VAL("Error: type takes 1 arg.");
    return false;
  }
  Value arg = args[0];
  if (IS_NIL(arg)) {
    args[-1] = CSTRING_VAL("nil");
  } else if (IS_BOOL(arg)) {
    args[-1] = CSTRING_VAL("boolean");
  } else if (IS_NUMBER(arg)) {
    args[-1] = CSTRING_VAL("number");
  } else if (IS_CLOSURE(arg) || IS_FUNCTION(arg) || IS_NATIVE(arg)) {
    args[-1] = CSTRING_VAL("fn");
  } else if (IS_STRING(arg)) {
    args[-1] = CSTRING_VAL("string");
  } else if (IS_HASHMAP(arg)) {
    args[-1] = CSTRING_VAL("map");
  } else if (IS_ARRAY(arg)) {
    args[-1] = CSTRING_VAL("array");
  } else {
    args[-1] = CSTRING_VAL("Error: unknown type.");
    return false;
  }

  return true;
}

static bool appendNative(int argCount, Value* args) {
  if (argCount != 2) {
    args[-1] = CSTRING_VAL("Error: append takes 2 args.");
    return false;
  }
  if (!IS_ARRAY(args[0])) {
    args[-1] = CSTRING_VAL("Error: Can only append to array.");
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
    args[-1] = CSTRING_VAL("Error: butlast takes 1 arg.");
    return false;
  }
  if (!IS_ARRAY(args[0])) {
    args[-1] = CSTRING_VAL("Error: Can only butlast to array.");
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
    args[-1] = CSTRING_VAL("Error: push takes 2 args.");
    return false;
  }
  if (!IS_ARRAY(args[0])) {
    args[-1] = CSTRING_VAL("Error: Can only push to array.");
    return false;
  }
  args[-1] = args[0];
  writeValueArray(&AS_ARRAY(args[0]), args[1]);
  return true;
}

static bool popNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = CSTRING_VAL("Error: pop takes 1 arg.");
    return false;
  }
  if (!IS_ARRAY(args[0])) {
    args[-1] = CSTRING_VAL("Error: Can only pop from array.");
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
    args[-1] = CSTRING_VAL("Error: assoc takes 3 args.");
    return false;
  }
  if (!IS_HASHMAP(args[0])) {
    args[-1] = CSTRING_VAL("Error: Can only assoc to map.");
    return false;
  }
  Value key = args[1];
  if (!IS_NUMBER(key) && !IS_STRING(key)) {
    args[-1] = CSTRING_VAL("Error: Hashmap key type must be number or string.");
    return false;
  }
  args[-1] = OBJ_VAL(newHashmap());
  tableAddAll(&AS_HASHMAP(args[0]), &AS_HASHMAP(args[-1]));
  tableSet(&AS_HASHMAP(args[0]), key, args[2]);
  return true;
}

static bool concatNative(int argCount, Value* args) {
  if (argCount != 2) {
    args[-1] = CSTRING_VAL("Error: concat takes 2 args.");
    return false;
  }
  if (!IS_ARRAY(args[0]) || !IS_ARRAY(args[1])) {
    args[-1] = CSTRING_VAL("Error: Can only concat arrays.");
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

static uint8_t utf8CharLength(uint8_t val) {
  if (val < 128) {
    return 1;
  } else if (val < 224) {
    return 2;
  } else if (val < 240) {
    return 3;
  }
  return 4;
}

static bool rangeNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = CSTRING_VAL("Error: range takes 1 args.");
    return false;
  }
  Value arg = args[0];
  if (IS_ARRAY(arg)) {
    args[-1] = arg;
  } else if(IS_NUMBER(arg)) {
    double dnum = AS_NUMBER(arg);
    int num = dnum;
    if (dnum != num || num < 0) {
      args[-1] = CSTRING_VAL("Error: range number should be positive integer.");
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
      for (int i = 0; i < s->length; ) {
        uint8_t charlen = utf8CharLength(s->chars[i]);
        push(OBJ_VAL(copyString(&s->chars[i], charlen)));
        writeValueArray(&AS_ARRAY(args[-1]), vm.stackTop[-1]);
        pop();
        i += charlen;
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
    args[-1] = CSTRING_VAL("Error: str takes 1 arg.");
    return false;
  }
  Value arg = args[0];
  if (IS_NUMBER(arg)) {
    double num = AS_NUMBER(arg);
    int64_t val_i = num;
    char str[20];
    if (num == val_i) {
      snprintf(str, sizeof(str), "%lld", val_i);
    } else {
      snprintf(str, sizeof(str), "%f", num);
    }
    args[-1] = CSTRING_VAL(str);
  } else if (IS_BOOL(arg)) {
    const char* s = AS_BOOL(arg) ? "true" : "false";
    args[-1] = CSTRING_VAL(s);
  } else if (IS_NIL(arg)) {
    args[-1] = CSTRING_VAL("nil");
  } else if (IS_STRING(arg)) {
    args[-1] = arg;
  } else if (IS_NATIVE(arg)) {
    args[-1] = CSTRING_VAL("<native fn>");
  } else if (IS_FUNCTION(arg)) {
    // XXX: compose fn name as part of return value
    args[-1] = CSTRING_VAL("<fn>");
  } else if (IS_CLOSURE(arg)) {
    // XXX: compose fn name as part of return value
    args[-1] = CSTRING_VAL("<fn>");
  } else if (IS_HASHMAP(arg)) {
    args[-1] = CSTRING_VAL("<map>");
  } else if (IS_ARRAY(arg)) {
    args[-1] = CSTRING_VAL("<array>");
  } else {
    args[-1] = CSTRING_VAL("Error: unknown type.");
    return false;
  }
  return true;
}

#ifndef __EMSCRIPTEN__
static bool systemNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: Arg must be a string.");
    return false;
  }
  ObjString* s = AS_STRING(args[0]);
  int ret = system(s->chars) >> 8;
  args[-1] = NUMBER_VAL(ret);
  return true;
}

static bool exitNative(int argCount, Value* args) {
  int exitCode = 0;
  if (argCount == 0) {
  } else if (argCount != 1 || !IS_NUMBER(args[0])) {
    args[-1] = CSTRING_VAL("Error: exit takes a number arg.");
    return false;
  } else {
    exitCode = AS_NUMBER(args[0]);
  }
  freeVM();
  exit(exitCode);
  return true;
}

static bool slurpNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: slurp takes a string arg.");
    return false;
  }

  char path[PATH_MAX + 1] = "";
  wordexp_t exp_result;
  wordexp(AS_STRING(args[0])->chars, &exp_result, 0);
  strncpy(path, exp_result.we_wordv[0], sizeof(path));
  wordfree(&exp_result);

  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    // XXX: more unified error reporting
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    args[-1] = CSTRING_VAL("Error: failed to open file.");
    return false;
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char* buffer = (char*)malloc(fileSize);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    args[-1] = CSTRING_VAL("Error: failed to open file.");
    fclose(file);
    return false;
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    args[-1] = CSTRING_VAL("Error: failed to read file.");
    fclose(file);
    free(buffer);
    return false;
  }
  args[-1] = OBJ_VAL(takeString(buffer, fileSize));

  fclose(file);

  return true;
}
#endif


static bool toLowerCaseNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: toLowerCase takes a string arg.");
    return false;
  }
  ObjString* str = AS_STRING(args[0]);
  char* s = (char*)malloc(str->length + 1);
  if (s == NULL) {
    args[-1] = CSTRING_VAL("Error: failed to allocate memory.");
    return false;
  }
  for (int i = 0; i < str->length; ++i) {
    s[i] = tolower(str->chars[i]);
  }
  args[-1] = OBJ_VAL(copyString(s, str->length));
  return true;
}

static bool toUpperCaseNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: toUpperCase takes a string arg.");
    return false;
  }
  ObjString* str = AS_STRING(args[0]);
  char* s = (char*)malloc(str->length + 1);
  if (s == NULL) {
    args[-1] = CSTRING_VAL("Error: failed to allocate memory.");
    return false;
  }
  for (int i = 0; i < str->length; ++i) {
    s[i] = toupper(str->chars[i]);
  }
  args[-1] = OBJ_VAL(copyString(s, str->length));
  return true;
}

static bool parseFloatNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: parseFloat takes a string arg.");
    return false;
  }
  char* s = AS_STRING(args[0])->chars;
  args[-1] = NUMBER_VAL(strtod(s, NULL));
  return true;
}

static bool doubleToUint8ArrayNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_NUMBER(args[0])) {
    args[-1] = CSTRING_VAL("Error: doubleToUint8ArrayNative takes a number arg.");
    return false;
  }
  union { // union is awesome https://stackoverflow.com/a/24420279/3282323
    double value;
    uint8_t bytes[8];
  } u;
  u.value = AS_NUMBER(args[0]);

  ObjArray* array = newArray();
  args[-1] = OBJ_VAL(array);
  for (int i = 0; i < 8; i++) {
    writeValueArray(&array->array, NUMBER_VAL(u.bytes[i]));
  }
  return true;
}

// ---- end of native function declarations ----
// ---- end of native function declarations ----
// ---- end of native function declarations ----

static void defineTableFunction(Table* table, const char* name, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  ObjString* nameString = COPY_CSTRING(name);
  push(OBJ_VAL(nameString));
  push(OBJ_VAL(newNative(function, nameString)));
  tableSet(table, vm.stackTop[-3], vm.stackTop[-1]);
  pop();
  pop();
  pop();
}

static void defineNative(const char* name, NativeFn function) {
  defineTableFunction(&vm.globals, name, function);
}

static void defineLxNatives() {
  push(CSTRING_VAL("Lx"));
  push(OBJ_VAL(newHashmap()));
  tableSet(&vm.globals, vm.stack[0], vm.stack[1]);

  push(CSTRING_VAL("args"));
  push(OBJ_VAL(newArray()));
  tableSet(&AS_HASHMAP(vm.stack[1]), vm.stack[2], vm.stack[3]);
  for (int i = 0; i < LX_ARGC; i++) {
    push(CSTRING_VAL(LX_ARGV[i]));
    writeValueArray(&AS_ARRAY(vm.stack[3]), vm.stack[4]);
    pop();
  }
  pop(); pop();

  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "globals", globalsNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "toLowerCase", toLowerCaseNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "toUpperCase", toUpperCaseNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "parseFloat", parseFloatNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "doubleToUint8Array", doubleToUint8ArrayNative);
#ifndef __EMSCRIPTEN__
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "exit", exitNative);
#endif

  pop();
  pop();
}

void defineBuiltinNatives() {
  defineLxNatives();

  defineNative("clock", clockNative);
  defineNative("print", printNative);
  defineNative("groan", groanNative);
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
#ifndef WASM
  defineNative("slurp", slurpNative);
  defineNative("system", systemNative);
#endif
}
#endif
