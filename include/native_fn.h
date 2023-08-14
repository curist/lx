#ifndef clox_native_fn_h
#define clox_native_fn_h

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <inttypes.h>
#include <limits.h>
#include <time.h>

#ifndef WASM
#include <wordexp.h>
#endif

#include "vm.h"
#include "object.h"

#define RFC3339 "%Y-%m-%dT%H:%M:%S%z"

void defineBuiltinNatives();

// ---- start of native function declarations ----
// ---- start of native function declarations ----
// ---- start of native function declarations ----

static bool timeNative(int argCount, Value* args) {
  time_t now = time(NULL);
  args[-1] = NUMBER_VAL(now);
  return true;
}

static bool strftimeNative(int argCount, Value* args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: Date.format takes 2 args.");
    return false;
  }
  if (!IS_NUMBER(args[0])) {
    args[-1] = CSTRING_VAL("Error: First arg of Date.format is unix timestamp.");
    return false;
  }
  char* format = RFC3339;
  if (argCount >= 2) {
    if (!IS_STRING(args[1])) {
      args[-1] = CSTRING_VAL("Error: Second arg of Date.format is format string.");
      return false;
    }
    format = AS_STRING(args[1])->chars;
  }
  time_t t = (time_t)AS_NUMBER(args[0]);
  struct tm date = {0};
  localtime_r(&t, &date);

  char formatted_time[40];
  strftime(formatted_time, 40, format, &date);
  args[-1] = CSTRING_VAL(formatted_time);
  return true;
}

static bool strptimeNative(int argCount, Value* args) {
  if (argCount < 2) {
    args[-1] = CSTRING_VAL("Error: Date.parse takes 2 args.");
    return false;
  }
  if (!IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: First arg of Date.parse is date string.");
    return false;
  }
  if (!IS_STRING(args[1])) {
    args[-1] = CSTRING_VAL("Error: Second arg of Date.parse is date format.");
    return false;
  }
  struct tm date = {0};
  strptime(AS_STRING(args[0])->chars, AS_STRING(args[1])->chars, &date);
  time_t datetime = mktime(&date);
  args[-1] = NUMBER_VAL(datetime);
  return true;
}

static bool printNative(int argCount, Value* args) {
  for (int i = 0; i < argCount; i++) {
    if (i > 0) printf(" ");
    printValue(stdout, args[i]);
  }
  args[-1] = NIL_VAL;
  return true;
}

static bool printlnNative(int argCount, Value* args) {
  printNative(argCount, args);
  printf("\n");
  fflush(stdout);
  return true;
}

static bool groanNative(int argCount, Value* args) {
  for (int i = 0; i < argCount; i++) {
    if (i > 0) fprintf(stderr, " ");
    printValue(stderr, args[i]);
  }
  args[-1] = NIL_VAL;
  return true;
}

static bool groanlnNative(int argCount, Value* args) {
  groanNative(argCount, args);
  fprintf(stderr, "\n");
  fflush(stderr);
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

static bool randomNative(int argCount, Value* args) {
  double num = (double)rand() / (double)RAND_MAX;
  args[-1] = NUMBER_VAL(num);
  return true;
}

static bool sqrtNative(int argCount, Value* args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: Arg must be a number.");
    return false;
  }
  Value arg = args[0];
  if (!IS_NUMBER(arg)) {
    args[-1] = CSTRING_VAL("Error: Arg must be a number.");
    return false;
  }
  double num = sqrt(AS_NUMBER(arg));
  args[-1] = NUMBER_VAL(num);
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
      for (size_t i = 0; i < s->length; ) {
        uint8_t charlen = utf8CharLength(s->chars[i]);
        if (i + charlen > s->length) break;

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

static int tostring(char** s, Value v) {
  size_t size = 50;
  *s = (char*) malloc(size);
  if (IS_NUMBER(v)) {
    double num = AS_NUMBER(v);
    int64_t val_i = num;
    if (num == val_i) {
      return snprintf(*s, size, "%" PRId64, val_i);
    }
    return snprintf(*s, size, "%lf", num);
  } else if (IS_BOOL(v)) {
    return snprintf(*s, size, "%s", AS_BOOL(v) ? "true" : "false");
  } else if (IS_NIL(v)) {
    return snprintf(*s, size, "nil");
  } else if (IS_STRING(v)) {
    ObjString* str = AS_STRING(v);
    *s = realloc(*s, str->length + 1);
    snprintf(*s, str->length + 1, "%s", str->chars);
    return str->length;
  } else if (IS_NATIVE(v)) {
    return snprintf(*s, size, "<native fn>");
  } else if (IS_FUNCTION(v)) {
    // XXX: compose fn name as part of return value
    return snprintf(*s, size, "<fn>");
  } else if (IS_CLOSURE(v)) {
    // XXX: compose fn name as part of return value
    return snprintf(*s, size, "<fn>");
  } else if (IS_HASHMAP(v)) {
    return snprintf(*s, size, "<map>");
  } else if (IS_ARRAY(v)) {
    return snprintf(*s, size, "<array>");
  }
  return snprintf(*s, size, "<unknown>");
}

static bool strNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = CSTRING_VAL("Error: str takes 1 arg.");
    return false;
  }
  char* s = NULL;
  tostring(&s, args[0]);
  args[-1] = CSTRING_VAL(s);
  free(s);
  return true;
}

static bool joinNative(int argCount, Value* args) {
  if (argCount != 2) {
    args[-1] = CSTRING_VAL("Error: join takes 2 args.");
    return false;
  }
  if (!IS_ARRAY(args[0])) {
    args[-1] = CSTRING_VAL("Error: First args of join is array.");
    return false;
  }
  if (!IS_STRING(args[1])) {
    args[-1] = CSTRING_VAL("Error: Second args of join is string.");
    return false;
  }
  ValueArray* array = &AS_ARRAY(args[0]);
  if (array->count == 0) {
    args[-1] = CSTRING_VAL("");
    return true;
  }
  char* s = NULL;
  int slen = tostring(&s, array->values[0]);
  if (array->count == 1) {
    args[-1] = CSTRING_VAL(s);
    free(s);
    return true;
  }
  char* result = malloc(slen);
  size_t result_size = slen;
  memcpy(result, s, slen);
  free(s);

  const char* sep = AS_STRING(args[1])->chars;
  size_t seplen = AS_STRING(args[1])->length;

  for (int i = 1; i < array->count; ++i) {
    slen = tostring(&s, array->values[i]);
    char* result_tmp = realloc(result, result_size + seplen + slen + 1);
    if (result_tmp == NULL) {
      args[-1] = CSTRING_VAL("Error: Realloc failed.");
      return false;
    }
    result = result_tmp;
    memcpy(result + result_size, sep, seplen);
    memcpy(result + result_size + seplen, s, slen);
    free(s);
    result_size += seplen + slen;
  }
  result[result_size] = '\0';
  args[-1] = CSTRING_VAL(result);
  free(result);
  return true;
}

static bool getlineNative(int argCount, Value* args) {
  char line[8192];
  char* read = NULL;
  if (!(read = fgets(line, sizeof(line), stdin))) {
    args[-1] = NIL_VAL;
  } else {
    args[-1] = OBJ_VAL(copyString(read, (int)strlen(read) - 1));
  }
  return true;
}

static bool readNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_NUMBER(args[0])) {
    args[-1] = CSTRING_VAL("Error: Arg must be a number.");
    return false;
  }
  size_t n = AS_NUMBER(args[0]);
  char chars[n];
  size_t read;
  if (!(read = fread(chars, 1, n, stdin))) {
    args[-1] = NIL_VAL;
  } else {
    args[-1] = OBJ_VAL(copyString(chars, read));
  }
  return true;
}

#ifndef WASM
static bool systemNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: Arg must be a string.");
    return false;
  }
  ObjString* cmd = AS_STRING(args[0]);
  FILE *fp;

  if ((fp = popen(cmd->chars, "r")) == NULL) {
    args[-1] = CSTRING_VAL("Error: Failed to start process.");
    return false;
  }

  char buf[500];
  size_t buflen;
  char* result = NULL;
  size_t result_size = 0;

  while ((buflen = fread(buf, sizeof(char), sizeof(buf), fp)) > 0) {
    char* tmp_result = (char*)realloc(result, buflen + result_size + 1);

    if (tmp_result == NULL) {
      args[-1] = CSTRING_VAL("Error: Realloc failed.");
      pclose(fp);
      if (result != NULL) free(result);
      return false;
    }

    memcpy(tmp_result + result_size, buf, buflen);
    result = tmp_result;

    result_size += buflen;
    result[result_size] = '\0';
  }

  int code = pclose(fp) >> 8;
  args[-1] = OBJ_VAL(newHashmap());
  push(CSTRING_VAL("code"));
  tableSet(&AS_HASHMAP(args[-1]), vm.stackTop[-1], NUMBER_VAL(code));
  pop();
  push(CSTRING_VAL("out"));
  if (result != NULL) {
    push(CSTRING_VAL(result));
    free(result);
  } else {
    push(CSTRING_VAL(""));
  }
  tableSet(&AS_HASHMAP(args[-1]), vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();

  return true;
}
#endif

#ifndef __EMSCRIPTEN__
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

#ifndef WASM
  char path[PATH_MAX + 1] = "";
  wordexp_t exp_result;
  wordexp(AS_STRING(args[0])->chars, &exp_result, 0);
  strncpy(path, exp_result.we_wordv[0], sizeof(path));
  wordfree(&exp_result);
#else
  char* path = AS_STRING(args[0])->chars;
#endif

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

  char* buffer = (char*)malloc(fileSize + 1);
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
  buffer[fileSize] = '\0';
  args[-1] = OBJ_VAL(takeString(buffer, fileSize));

  fclose(file);

  return true;
}
#endif


static bool tolowerNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: tolower takes a string arg.");
    return false;
  }
  ObjString* str = AS_STRING(args[0]);
  char* s = (char*)malloc(str->length + 1);
  if (s == NULL) {
    args[-1] = CSTRING_VAL("Error: failed to allocate memory.");
    return false;
  }
  for (size_t i = 0; i < str->length; ++i) {
    s[i] = tolower(str->chars[i]);
  }
  args[-1] = OBJ_VAL(copyString(s, str->length));
  free(s);
  return true;
}

static bool toupperNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: toupper takes a string arg.");
    return false;
  }
  ObjString* str = AS_STRING(args[0]);
  char* s = (char*)malloc(str->length + 1);
  if (s == NULL) {
    args[-1] = CSTRING_VAL("Error: failed to allocate memory.");
    return false;
  }
  for (size_t i = 0; i < str->length; ++i) {
    s[i] = toupper(str->chars[i]);
  }
  args[-1] = OBJ_VAL(copyString(s, str->length));
  free(s);
  return true;
}

static bool tonumberNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: tonumber takes a string arg.");
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
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "doubleToUint8Array", doubleToUint8ArrayNative);
#ifndef __EMSCRIPTEN__
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "exit", exitNative);
#endif

  pop();
  pop();
}

static void defineDateNatives() {
  push(CSTRING_VAL("Date"));
  push(OBJ_VAL(newHashmap()));
  tableSet(&vm.globals, vm.stack[0], vm.stack[1]);

  push(CSTRING_VAL("RFC3339"));
  push(CSTRING_VAL(RFC3339));
  tableSet(&AS_HASHMAP(vm.stack[1]), vm.stack[2], vm.stack[3]);
  pop();
  pop();

  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "time", timeNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "format", strftimeNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "parse", strptimeNative);

  pop();
  pop();
}

void defineBuiltinNatives() {
  defineLxNatives();
  defineDateNatives();

  defineNative("print", printNative);
  defineNative("println", printlnNative);
  defineNative("groan", groanNative);
  defineNative("groanln", groanlnNative);
  defineNative("str", strNative);
  defineNative("join", joinNative);
  defineNative("tolower", tolowerNative);
  defineNative("toupper", toupperNative);
  defineNative("tonumber", tonumberNative);
  defineNative("int", intNative);
  defineNative("ord", ordNative);
  defineNative("random", randomNative);
  defineNative("sqrt", sqrtNative);
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
  defineNative("getline", getlineNative);
  defineNative("read", readNative);

#ifndef WASM
  defineNative("system", systemNative);
#endif
#ifndef __EMSCRIPTEN__
  defineNative("slurp", slurpNative);
#endif
}
#endif
