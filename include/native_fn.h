#ifndef clox_native_fn_h
#define clox_native_fn_h

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "lx/lxversion.h"

#include "memory.h"
#include "object.h"
#include "vm.h"

#define RFC3339 "%Y-%m-%dT%H:%M:%S%z"

void defineBuiltinNatives();

// ---- start of native function declarations ----
// ---- start of native function declarations ----
// ---- start of native function declarations ----

// Implemented in `src/vm.c` (needs access to the VM run loop).
static bool pcallNative(int argCount, Value *args);
static void runtimeError(const char* format, ...);

static bool timeNative(int argCount, Value *args) {
  time_t now = time(NULL);
  args[-1] = NUMBER_VAL(now);
  return true;
}

static bool strftimeNative(int argCount, Value *args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: Date.format takes 2 args.");
    return false;
  }
  if (!IS_NUMBER(args[0])) {
    args[-1] =
        CSTRING_VAL("Error: First arg of Date.format is unix timestamp.");
    return false;
  }
  char *format = RFC3339;
  if (argCount >= 2) {
    if (!IS_STRING(args[1])) {
      args[-1] =
          CSTRING_VAL("Error: Second arg of Date.format is format string.");
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

static bool strptimeNative(int argCount, Value *args) {
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

static bool printNative(int argCount, Value *args) {
  for (int i = 0; i < argCount; i++) {
    if (i > 0)
      printf(" ");
    printValue(stdout, args[i]);
  }
  args[-1] = NIL_VAL;
  return true;
}

static bool printlnNative(int argCount, Value *args) {
  printNative(argCount, args);
  printf("\n");
  fflush(stdout);
  return true;
}

static bool putcNative(int argCount, Value *args) {
  for (int i = 0; i < argCount; i++) {
    printf("%c", (int)AS_NUMBER(args[i]));
  }
  args[-1] = NIL_VAL;
  return true;
}

static bool groanNative(int argCount, Value *args) {
  for (int i = 0; i < argCount; i++) {
    if (i > 0)
      fprintf(stderr, " ");
    printValue(stderr, args[i]);
  }
  args[-1] = NIL_VAL;
  return true;
}

static bool groanlnNative(int argCount, Value *args) {
  groanNative(argCount, args);
  fprintf(stderr, "\n");
  fflush(stderr);
  return true;
}

static bool intNative(int argCount, Value *args) {
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

static bool chrNative(int argCount, Value *args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: Arg must be a number.");
    return false;
  }
  Value arg = args[0];
  if (!IS_NUMBER(arg)) {
    args[-1] = CSTRING_VAL("Error: Arg must be a number.");
    return false;
  }
  char *chrs = malloc(2);
  chrs[0] = AS_NUMBER(arg);
  chrs[1] = '\0';
  args[-1] = OBJ_VAL(takeString(chrs, 1));
  return true;
}

static bool ordNative(int argCount, Value *args) {
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

static bool randomNative(int argCount, Value *args) {
  double num = (double)rand() / (double)RAND_MAX;
  args[-1] = NUMBER_VAL(num);
  return true;
}

static bool sqrtNative(int argCount, Value *args) {
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

static bool keysNative(int argCount, Value *args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: keys takes 1 arg.");
    return false;
  }
  Value arg = args[0];
  if (!IS_HASHMAP(arg) && !IS_ENUM(arg)) {
    args[-1] = CSTRING_VAL("Error: Arg must be a map or enum.");
    return false;
  }
  ObjArray *array = newArray();
  args[-1] = OBJ_VAL(array);

  if (IS_ENUM(arg)) {
    ValueArray* names = &AS_ENUM(arg)->names;
    for (int i = 0; i < names->count; i++) {
      writeValueArray(&array->array, names->values[i]);
    }
  } else {
    Table *table = &AS_HASHMAP(arg);
    for (int i = table->arrayCapacity - 1; i >= 0; --i) {
      if (table->arrayPresent != NULL && table->arrayPresent[i]) {
        writeValueArray(&array->array, NUMBER_VAL((double)i));
      }
    }
    for (int i = table->capacity - 1; i >= 0; --i) {
      Entry *entry = &table->entries[i];
      if (!IS_NIL(entry->key)) {
        writeValueArray(&array->array, entry->key);
      }
    }
  }
  return true;
}

static bool nameOfNative(int argCount, Value *args) {
  if (argCount < 2) {
    args[-1] = CSTRING_VAL("Error: nameOf(enum, value) requires 2 args.");
    return false;
  }
  Value enumLike = args[0];
  if (IS_ENUM(enumLike)) {
    Value target = args[1];
    if (!IS_NUMBER(target) && !IS_STRING(target)) {
      args[-1] = NIL_VAL;
      return true;
    }
    Value name;
    if (tableGet(&AS_ENUM_REVERSE(enumLike), target, &name) && IS_STRING(name)) {
      args[-1] = name;
      return true;
    }
    args[-1] = NIL_VAL;
    return true;
  }
  args[-1] = CSTRING_VAL("Error: nameOf(enum, value) expects enum to be an enum.");
  return false;
}

static bool globalsNative(int argCount, Value *args) {
  Table *table = &vm.globals;
  ObjArray *array = newArray();
  args[-1] = OBJ_VAL(array);

  for (int i = table->arrayCapacity - 1; i >= 0; --i) {
    if (table->arrayPresent != NULL && table->arrayPresent[i]) {
      writeValueArray(&array->array, NUMBER_VAL((double)i));
    }
  }
  for (int i = table->capacity - 1; i >= 0; --i) {
    Entry *entry = &table->entries[i];
    if (!IS_NIL(entry->key)) {
      writeValueArray(&array->array, entry->key);
    }
  }
  return true;
}

static bool lenNative(int argCount, Value *args) {
  if (argCount < 1) {
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

static bool typeNative(int argCount, Value *args) {
  if (argCount < 1) {
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
  } else if (IS_ENUM(arg)) {
    args[-1] = CSTRING_VAL("enum");
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

static bool pushNative(int argCount, Value *args) {
  if (argCount < 2) {
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

static bool popNative(int argCount, Value *args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: pop takes 1 arg.");
    return false;
  }
  if (!IS_ARRAY(args[0])) {
    args[-1] = CSTRING_VAL("Error: Can only pop from array.");
    return false;
  }
  ValueArray *arr = &AS_ARRAY(args[0]);
  if (arr->count == 0) {
    args[-1] = NIL_VAL;
    return true;
  }
  args[-1] = arr->values[--arr->count];
  return true;
}

static bool concatNative(int argCount, Value *args) {
  if (argCount < 2) {
    args[-1] = CSTRING_VAL("Error: concat takes 2 args.");
    return false;
  }
  if (!IS_ARRAY(args[0]) || !IS_ARRAY(args[1])) {
    args[-1] = CSTRING_VAL("Error: Can only concat arrays.");
    return false;
  }
  ValueArray *arr1 = &AS_ARRAY(args[0]);
  ValueArray *arr2 = &AS_ARRAY(args[1]);
  args[-1] = OBJ_VAL(newArray());
  for (int i = 0; i < arr1->count; i++) {
    writeValueArray(&AS_ARRAY(args[-1]), arr1->values[i]);
  }
  for (int i = 0; i < arr2->count; i++) {
    writeValueArray(&AS_ARRAY(args[-1]), arr2->values[i]);
  }
  return true;
}

static bool pathJoinNative(int argCount, Value *args) {
  // join("a", "b", "c") => "a/b/c"
  // If a later segment is absolute (starts with '/'), it replaces the previous result.
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("");
    return true;
  }

  size_t cap = 1;
  for (int i = 0; i < argCount; i++) {
    if (!IS_STRING(args[i])) {
      args[-1] = CSTRING_VAL("Error: Lx.path.join takes string args.");
      return false;
    }
    cap += AS_STRING(args[i])->length + 1;
  }

  char* out = (char*)malloc(cap);
  if (out == NULL) {
    args[-1] = CSTRING_VAL("Error: out of memory.");
    return false;
  }

  size_t outLen = 0;
  for (int i = 0; i < argCount; i++) {
    ObjString* segStr = AS_STRING(args[i]);
    const char* seg = segStr->chars;
    size_t segLen = segStr->length;
    if (segLen == 0) continue;

    if (seg[0] == '/') {
      // Absolute segment replaces the entire path.
      outLen = 0;
      memcpy(out + outLen, seg, segLen);
      outLen += segLen;
      continue;
    }

    // Trim trailing slashes on current output (but keep "/" intact).
    while (outLen > 1 && out[outLen - 1] == '/') {
      outLen--;
    }

    // Trim leading slashes on segment.
    size_t start = 0;
    while (start < segLen && seg[start] == '/') {
      start++;
    }

    // Add separator if needed.
    if (outLen > 0 && out[outLen - 1] != '/' && start < segLen) {
      out[outLen++] = '/';
    }

    if (start < segLen) {
      memcpy(out + outLen, seg + start, segLen - start);
      outLen += segLen - start;
    }
  }

  out[outLen] = '\0';
  args[-1] = OBJ_VAL(takeString(out, (int)outLen));
  return true;
}

static bool pathDirnameNative(int argCount, Value *args) {
  // Roughly matches POSIX dirname semantics:
  // dirname("") => "."
  // dirname("a") => "."
  // dirname("a/b") => "a"
  // dirname("/a") => "/"
  // dirname("/") => "/"
  if (argCount < 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: Lx.path.dirname takes 1 string arg.");
    return false;
  }

  ObjString* inStr = AS_STRING(args[0]);
  const char* in = inStr->chars;
  size_t len = inStr->length;

  if (len == 0) {
    args[-1] = CSTRING_VAL(".");
    return true;
  }

  // Strip trailing slashes (but keep a single leading slash).
  size_t end = len;
  while (end > 1 && in[end - 1] == '/') {
    end--;
  }

  // If the path is all slashes, dirname is "/".
  if (end == 1 && in[0] == '/') {
    args[-1] = CSTRING_VAL("/");
    return true;
  }

  // Find last '/' before end.
  ssize_t lastSlash = -1;
  for (ssize_t i = (ssize_t)end - 1; i >= 0; i--) {
    if (in[i] == '/') {
      lastSlash = i;
      break;
    }
  }

  if (lastSlash < 0) {
    args[-1] = CSTRING_VAL(".");
    return true;
  }

  if (lastSlash == 0) {
    args[-1] = CSTRING_VAL("/");
    return true;
  }

  // Strip any trailing slashes from the result (e.g. "a/b/" -> "a").
  size_t outLen = (size_t)lastSlash;
  while (outLen > 1 && in[outLen - 1] == '/') {
    outLen--;
  }

  char* out = (char*)malloc(outLen + 1);
  if (out == NULL) {
    args[-1] = CSTRING_VAL("Error: out of memory.");
    return false;
  }
  memcpy(out, in, outLen);
  out[outLen] = '\0';
  args[-1] = OBJ_VAL(takeString(out, (int)outLen));
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

static bool rangeNative(int argCount, Value *args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: range takes 1 args.");
    return false;
  }
  Value arg = args[0];
  if (IS_ARRAY(arg)) {
    args[-1] = arg;
  } else if (IS_NUMBER(arg)) {
    double dnum = AS_NUMBER(arg);
    int num = dnum;
    if (dnum != num || num < 0) {
      args[-1] = CSTRING_VAL("Error: range number should be positive integer.");
      return false;
    }
    ObjArray *arr = newArray();
    args[-1] = OBJ_VAL(arr);
    arr->array.capacity = num;
    arr->array.values = GROW_ARRAY(Value, NULL, 0, num);
    arr->array.count = num;
    for (int i = 0; i < num; i++) {
      arr->array.values[i] = NUMBER_VAL(i);
    }
  } else {
    // every other value types start with an empty array
    args[-1] = OBJ_VAL(newArray());

    if (IS_STRING(arg)) {
      ObjString *s = AS_STRING(arg);
      for (size_t i = 0; i < s->length;) {
        uint8_t charlen = utf8CharLength(s->chars[i]);
        if (i + charlen > s->length)
          break;

        push(OBJ_VAL(copyString(&s->chars[i], charlen)));
        writeValueArray(&AS_ARRAY(args[-1]), vm.stackTop[-1]);
        pop();
        i += charlen;
      }
    } else if (IS_ENUM(arg) || IS_HASHMAP(arg)) {
      if (IS_ENUM(arg)) {
        ValueArray* names = &AS_ENUM(arg)->names;
        for (int i = 0; i < names->count; i++) {
          writeValueArray(&AS_ARRAY(args[-1]), names->values[i]);
        }
      } else {
        Table *table = &AS_HASHMAP(arg);
        for (int i = table->capacity - 1; i >= 0; --i) {
          Entry *entry = &table->entries[i];
          if (!IS_NIL(entry->key)) {
            writeValueArray(&AS_ARRAY(args[-1]), entry->key);
          }
        }
      }
    }
  }
  return true;
}

static bool linesNative(int argCount, Value *args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: lines takes 1 arg.");
    return false;
  }
  if (!IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: Arg must be a string.");
    return false;
  }

  ObjString *input = AS_STRING(args[0]);
  ObjArray *result = newArray();
  args[-1] = OBJ_VAL(result);

  const char *start = input->chars;
  const char *end = input->chars;
  const char *limit = input->chars + input->length;

  while (end < limit) {
    if (*end == '\n') {
      size_t lineLength = end - start;
      ObjString *line = copyString(start, lineLength);
      writeValueArray(&result->array, OBJ_VAL(line));
      start = end + 1;
    }
    end++;
  }

  // Handle the last line if it doesn't end with a newline
  if (start < limit) {
    size_t lineLength = limit - start;
    ObjString *line = copyString(start, lineLength);
    writeValueArray(&result->array, OBJ_VAL(line));
  }

  return true;
}

static int tostring(char **s, Value v) {
  return valueToString(v, s);
}

static bool strNative(int argCount, Value *args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: str takes 1 arg.");
    return false;
  }
  char *s = NULL;
  tostring(&s, args[0]);
  args[-1] = CSTRING_VAL(s);
  free(s);
  return true;
}

static bool joinNative(int argCount, Value *args) {
  if (argCount < 2) {
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
  ValueArray *array = &AS_ARRAY(args[0]);
  if (array->count == 0) {
    args[-1] = CSTRING_VAL("");
    return true;
  }

  const char *sep = AS_STRING(args[1])->chars;
  size_t seplen = AS_STRING(args[1])->length;

  // First pass: calculate total length needed
  // Store string representations to avoid converting twice
  char **strings = malloc(sizeof(char*) * array->count);
  if (strings == NULL) {
    args[-1] = CSTRING_VAL("Error: Malloc failed.");
    return false;
  }

  size_t total_length = 0;
  for (int i = 0; i < array->count; i++) {
    int slen = tostring(&strings[i], array->values[i]);
    total_length += slen;
    if (i > 0) {
      total_length += seplen;
    }
  }

  // Single allocation for final result
  char *result = malloc(total_length + 1);
  if (result == NULL) {
    for (int i = 0; i < array->count; i++) {
      free(strings[i]);
    }
    free(strings);
    args[-1] = CSTRING_VAL("Error: Malloc failed.");
    return false;
  }

  // Second pass: copy all strings into result
  size_t offset = 0;
  for (int i = 0; i < array->count; i++) {
    if (i > 0) {
      memcpy(result + offset, sep, seplen);
      offset += seplen;
    }
    size_t slen = strlen(strings[i]);
    memcpy(result + offset, strings[i], slen);
    offset += slen;
    free(strings[i]);
  }
  result[total_length] = '\0';
  free(strings);

  args[-1] = CSTRING_VAL(result);
  free(result);
  return true;
}

static bool splitNative(int argCount, Value *args) {
  if (argCount < 2) {
    args[-1] = CSTRING_VAL("Error: split takes 2 args.");
    return false;
  }
  if (!IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: First arg must be a string.");
    return false;
  }
  if (!IS_STRING(args[1])) {
    args[-1] = CSTRING_VAL("Error: Second arg must be a string.");
    return false;
  }

  ObjString *input = AS_STRING(args[0]);
  ObjString *delimiter = AS_STRING(args[1]);

  ObjArray *result = newArray();
  args[-1] = OBJ_VAL(result);

  if (delimiter->length == 0) {
    return rangeNative(argCount, args);
  }

  const char *start = input->chars;
  const char *next = strstr(start, delimiter->chars);

  while (next != NULL) {
    size_t length = next - start;
    ObjString *part = copyString(start, length);
    writeValueArray(&result->array, OBJ_VAL(part));

    start = next + delimiter->length;
    next = strstr(start, delimiter->chars);
  }

  // Add remaining part
  if (*start != '\0') {
    size_t length = (input->chars + input->length) - start;
    ObjString *part = copyString(start, length);
    writeValueArray(&result->array, OBJ_VAL(part));
  }

  return true;
}

static bool getlineNative(int argCount, Value *args) {
  char line[8192];
  char *read = NULL;
  if (!(read = fgets(line, sizeof(line), stdin))) {
    args[-1] = NIL_VAL;
  } else {
    args[-1] = OBJ_VAL(copyString(read, (int)strlen(read) - 1));
  }
  return true;
}

static bool readNative(int argCount, Value *args) {
  if (argCount < 1 || !IS_NUMBER(args[0])) {
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
static bool execNative(int argCount, Value *args) {
  if (argCount < 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: Arg must be a string.");
    return false;
  }
  ObjString *cmd = AS_STRING(args[0]);
  FILE *fp;

  if ((fp = popen(cmd->chars, "r")) == NULL) {
    args[-1] = CSTRING_VAL("Error: Failed to start process.");
    return false;
  }

  char buf[500];
  size_t buflen;
  char *result = NULL;
  size_t result_size = 0;

  while ((buflen = fread(buf, sizeof(char), sizeof(buf), fp)) > 0) {
    char *tmp_result = (char *)realloc(result, buflen + result_size + 1);

    if (tmp_result == NULL) {
      args[-1] = CSTRING_VAL("Error: Realloc failed.");
      pclose(fp);
      if (result != NULL)
        free(result);
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

static bool systemNative(int argCount, Value *args) {
  if (argCount < 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: Arg must be a string.");
    return false;
  }
  ObjString *cmd = AS_STRING(args[0]);
  int code = system(cmd->chars) >> 8;
  args[-1] = NUMBER_VAL(code);

  return true;
}
#endif

static bool exitNative(int argCount, Value *args) {
  int exitCode = 0;
  if (argCount == 0) {
  } else if (argCount < 1 || !IS_NUMBER(args[0])) {
    args[-1] = CSTRING_VAL("Error: exit takes a number arg.");
    return false;
  } else {
    exitCode = AS_NUMBER(args[0]);
  }
  freeVM();
  exit(exitCode);
  return true;
}

static bool resolvePathFromCwd(const char* in, char* out, size_t outSize) {
  if (in == NULL) return false;
  if (out == NULL || outSize == 0) return false;

  // Absolute paths are used as-is.
  if (in[0] == '/') {
    size_t n = strlen(in);
    if (n + 1 > outSize) return false;
    memcpy(out, in, n + 1);
    return true;
  }

  char cwd[PATH_MAX + 1];
  if (getcwd(cwd, sizeof(cwd)) == NULL) return false;

  size_t cwdLen = strlen(cwd);
  size_t inLen = strlen(in);

  // Need: cwd + "/" + in + "\0"
  if (cwdLen + 1 + inLen + 1 > outSize) return false;

  memcpy(out, cwd, cwdLen);
  if (cwdLen > 0 && cwd[cwdLen - 1] != '/') {
    out[cwdLen] = '/';
    memcpy(out + cwdLen + 1, in, inLen + 1);
  } else {
    memcpy(out + cwdLen, in, inLen + 1);
  }

  return true;
}

static bool slurpNative(int argCount, Value *args) {
  if (argCount < 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: slurp takes a string arg.");
    return false;
  }

  const char* inPath = AS_STRING(args[0])->chars;
  char path[PATH_MAX + 1] = "";
  if (!resolvePathFromCwd(inPath, path, sizeof(path))) {
    args[-1] = CSTRING_VAL("Error: invalid path.");
    return false;
  }

  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    // XXX: more unified error reporting
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    args[-1] = CSTRING_VAL("Error: failed to open file.");
    return false;
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char *buffer = (char *)malloc(fileSize + 1);
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

static bool spitNative(int argCount, Value *args) {
  if (argCount < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
    args[-1] = CSTRING_VAL("Error: spit takes a path and string content.");
    return false;
  }

  const char* inPath = AS_STRING(args[0])->chars;
  char path[PATH_MAX + 1] = "";
  if (!resolvePathFromCwd(inPath, path, sizeof(path))) {
    args[-1] = CSTRING_VAL("Error: invalid path.");
    return false;
  }

  FILE *file = fopen(path, "wb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    args[-1] = CSTRING_VAL("Error: failed to open file.");
    return false;
  }

  ObjString *content = AS_STRING(args[1]);
  size_t written = fwrite(content->chars, sizeof(char), content->length, file);
  if (written < (size_t)content->length) {
    fprintf(stderr, "Could not write file \"%s\".\n", path);
    args[-1] = CSTRING_VAL("Error: failed to write file.");
    fclose(file);
    return false;
  }

  fclose(file);
  args[-1] = BOOL_VAL(true);
  return true;
}

static bool fsCwdNative(int argCount, Value *args) {
  if (argCount != 0) {
    args[-1] = CSTRING_VAL("Error: Lx.fs.cwd takes 0 args.");
    return false;
  }

  char buf[PATH_MAX + 1];
  if (getcwd(buf, sizeof(buf)) == NULL) {
    args[-1] = CSTRING_VAL("Error: failed to getcwd.");
    return false;
  }

  args[-1] = CSTRING_VAL(buf);
  return true;
}

static bool fsExistsNative(int argCount, Value *args) {
  if (argCount < 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: Lx.fs.exists takes a string path.");
    return false;
  }

  const char* inPath = AS_STRING(args[0])->chars;
  char path[PATH_MAX + 1] = "";
  if (!resolvePathFromCwd(inPath, path, sizeof(path))) {
    args[-1] = BOOL_VAL(false);
    return true;
  }

  struct stat st;
  int ok = stat(path, &st) == 0;

  args[-1] = BOOL_VAL(ok);
  return true;
}

static bool fsStatNative(int argCount, Value *args) {
  if (argCount < 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: Lx.fs.stat takes a string path.");
    return false;
  }

  const char* inPath = AS_STRING(args[0])->chars;
  char path[PATH_MAX + 1] = "";
  if (!resolvePathFromCwd(inPath, path, sizeof(path))) {
    args[-1] = NIL_VAL;
    return true;
  }

  struct stat st;
  if (stat(path, &st) != 0) {
    args[-1] = NIL_VAL;
    return true;
  }

  ObjHashmap* out = newHashmap();
  args[-1] = OBJ_VAL(out);
  push(OBJ_VAL(out));

  const char* type = "other";
  if (S_ISREG(st.st_mode)) type = "file";
  else if (S_ISDIR(st.st_mode)) type = "dir";
#ifdef S_ISLNK
  else if (S_ISLNK(st.st_mode)) type = "symlink";
#endif

  push(CSTRING_VAL("type"));
  push(CSTRING_VAL(type));
  tableSet(&out->table, vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();

  push(CSTRING_VAL("size"));
  push(NUMBER_VAL((double)st.st_size));
  tableSet(&out->table, vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();

  push(CSTRING_VAL("mtime"));
  push(NUMBER_VAL((double)st.st_mtime));
  tableSet(&out->table, vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();

  push(CSTRING_VAL("mode"));
  push(NUMBER_VAL((double)st.st_mode));
  tableSet(&out->table, vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();

  pop(); // out
  return true;
}

static bool fsRealpathNative(int argCount, Value *args) {
  if (argCount < 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: Lx.fs.realpath takes a string path.");
    return false;
  }

  const char* inPath = AS_STRING(args[0])->chars;
  char path[PATH_MAX + 1] = "";
  if (!resolvePathFromCwd(inPath, path, sizeof(path))) {
    args[-1] = NIL_VAL;
    return true;
  }

  char resolved[PATH_MAX + 1];
  if (realpath(path, resolved) == NULL) {
    args[-1] = NIL_VAL;
    return true;
  }

  args[-1] = CSTRING_VAL(resolved);
  return true;
}

static bool tolowerNative(int argCount, Value *args) {
  if (argCount < 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: tolower takes a string arg.");
    return false;
  }
  ObjString *str = AS_STRING(args[0]);
  char *s = (char *)malloc(str->length + 1);
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

static bool toupperNative(int argCount, Value *args) {
  if (argCount < 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: toupper takes a string arg.");
    return false;
  }
  ObjString *str = AS_STRING(args[0]);
  char *s = (char *)malloc(str->length + 1);
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

static bool tonumberNative(int argCount, Value *args) {
  if (argCount < 1 || !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: tonumber takes a string arg.");
    return false;
  }
  char *s = AS_STRING(args[0])->chars;
  args[-1] = NUMBER_VAL(strtod(s, NULL));
  return true;
}

static bool doubleToUint8ArrayNative(int argCount, Value *args) {
  if (argCount < 1 || !IS_NUMBER(args[0])) {
    args[-1] =
        CSTRING_VAL("Error: doubleToUint8ArrayNative takes a number arg.");
    return false;
  }
  union { // union is awesome https://stackoverflow.com/a/24420279/3282323
    double value;
    uint8_t bytes[8];
  } u;
  u.value = AS_NUMBER(args[0]);

  ObjArray *array = newArray();
  args[-1] = OBJ_VAL(array);
  for (int i = 0; i < 8; i++) {
    writeValueArray(&array->array, NUMBER_VAL(u.bytes[i]));
  }
  return true;
}

static bool lxErrorNative(int argCount, Value *args) {
  if (argCount < 1) {
    runtimeError("Error");
    return false;
  }

  if (IS_STRING(args[0])) {
    runtimeError("%s", AS_STRING(args[0])->chars);
    return false;
  }

  runtimeError("Error");
  return false;
}

static inline uint32_t readU32le(const uint8_t* bytes) {
  return (uint32_t)bytes[0]
      | ((uint32_t)bytes[1] << 8)
      | ((uint32_t)bytes[2] << 16)
      | ((uint32_t)bytes[3] << 24);
}

static inline bool lxObjHeaderLooksValid(const uint8_t* bytes, size_t len) {
  if (bytes == NULL) return false;
  // objIsValid() reads from fixed offsets; make sure it can't read OOB.
  if (len < 32) return false;
  if (!(bytes[0] == 'L' && bytes[1] == 'X')) return false;
  // Version is currently 1.
  if (bytes[2] != 1) return false;
  uint32_t objSize = readU32le(bytes + 4);
  return objSize == (uint32_t)len;
}

static bool lxIsLxObjNative(int argCount, Value *args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: Lx.isLxObj takes 1 arg.");
    return false;
  }

  if (IS_STRING(args[0])) {
    ObjString* s = AS_STRING(args[0]);
    args[-1] = BOOL_VAL(lxObjHeaderLooksValid((const uint8_t*)s->chars, (size_t)s->length));
    return true;
  }

  if (IS_ARRAY(args[0])) {
    ValueArray* a = &AS_ARRAY(args[0]);
    if (a->count < 32) {
      args[-1] = BOOL_VAL(false);
      return true;
    }

    // Header bytes: "LX" + version + flags.
    for (int i = 0; i < 8; i++) {
      if (!IS_NUMBER(a->values[i])) {
        args[-1] = BOOL_VAL(false);
        return true;
      }
      double n = AS_NUMBER(a->values[i]);
      if (n < 0 || n > 255 || (uint8_t)n != n) {
        args[-1] = BOOL_VAL(false);
        return true;
      }
    }

    uint8_t b0 = (uint8_t)AS_NUMBER(a->values[0]);
    uint8_t b1 = (uint8_t)AS_NUMBER(a->values[1]);
    uint8_t ver = (uint8_t)AS_NUMBER(a->values[2]);
    if (!(b0 == 'L' && b1 == 'X')) {
      args[-1] = BOOL_VAL(false);
      return true;
    }
    if (ver != 1) {
      args[-1] = BOOL_VAL(false);
      return true;
    }

    uint32_t objSize =
        (uint32_t)(uint8_t)AS_NUMBER(a->values[4]) |
        ((uint32_t)(uint8_t)AS_NUMBER(a->values[5]) << 8) |
        ((uint32_t)(uint8_t)AS_NUMBER(a->values[6]) << 16) |
        ((uint32_t)(uint8_t)AS_NUMBER(a->values[7]) << 24);

    args[-1] = BOOL_VAL(objSize == (uint32_t)a->count);
    return true;
  }

  args[-1] = CSTRING_VAL("Error: Lx.isLxObj expects a string or byte array.");
  return false;
}

static bool lxLoadObjNative(int argCount, Value *args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: Lx.loadObj takes 1 arg (bytes).");
    return false;
  }

  bool printCode = false;
  if (argCount >= 2) {
    if (!IS_BOOL(args[1])) {
      args[-1] = CSTRING_VAL("Error: Lx.loadObj arg2 must be a bool (printCode).");
      return false;
    }
    printCode = AS_BOOL(args[1]);
  }

  uint8_t* owned = NULL;
  uint8_t* bytes = NULL;
  size_t len = 0;

  if (IS_STRING(args[0])) {
    ObjString* s = AS_STRING(args[0]);
    bytes = (uint8_t*)s->chars;
    len = (size_t)s->length;
  } else if (IS_ARRAY(args[0])) {
    ValueArray* a = &AS_ARRAY(args[0]);
    len = (size_t)a->count;
    owned = (uint8_t*)malloc(len);
    if (owned == NULL) {
      args[-1] = CSTRING_VAL("Error: out of memory.");
      return false;
    }
    for (size_t i = 0; i < len; i++) {
      if (!IS_NUMBER(a->values[i])) {
        free(owned);
        args[-1] = CSTRING_VAL("Error: Lx.loadObj byte arrays must contain numbers.");
        return false;
      }
      double n = AS_NUMBER(a->values[i]);
      if (n < 0 || n > 255 || (uint8_t)n != n) {
        free(owned);
        args[-1] = CSTRING_VAL("Error: invalid byte value in Lx.loadObj input.");
        return false;
      }
      owned[i] = (uint8_t)n;
    }
    bytes = owned;
  } else {
    args[-1] = CSTRING_VAL("Error: Lx.loadObj expects a string or byte array.");
    return false;
  }

  if (!lxObjHeaderLooksValid(bytes, len)) {
    free(owned);
    args[-1] = CSTRING_VAL("Error: invalid lxobj.");
    return false;
  }

  ObjFunction* function = loadObj(bytes, printCode);
  free(owned);

  if (function == NULL) {
    args[-1] = CSTRING_VAL("Error: failed to load lxobj.");
    return false;
  }

  push(OBJ_VAL(function));
  ObjClosure* closure = newClosure(function);
  pop();

  args[-1] = OBJ_VAL(closure);
  return true;
}

static bool stdinReadAllNative(int argCount, Value *args) {
  if (argCount != 0) {
    args[-1] = CSTRING_VAL("Error: Lx.stdin.readAll takes 0 args.");
    return false;
  }

  size_t capacity = 1024;
  size_t length = 0;
  char* buffer = (char*)malloc(capacity);
  if (buffer == NULL) {
    args[-1] = CSTRING_VAL("Error: out of memory.");
    return false;
  }

  int c;
  while ((c = fgetc(stdin)) != EOF) {
    if (length + 1 >= capacity) {
      capacity *= 2;
      char* next = (char*)realloc(buffer, capacity);
      if (next == NULL) {
        free(buffer);
        args[-1] = CSTRING_VAL("Error: out of memory.");
        return false;
      }
      buffer = next;
    }
    buffer[length++] = (char)c;
  }
  buffer[length] = '\0';

  args[-1] = OBJ_VAL(takeString(buffer, (int)length));
  return true;
}

static bool stdinReadLineNative(int argCount, Value *args) {
  if (argCount > 1) {
    args[-1] = CSTRING_VAL("Error: Lx.stdin.readLine takes 0 or 1 args.");
    return false;
  }
  if (argCount == 1 && !IS_STRING(args[0])) {
    args[-1] = CSTRING_VAL("Error: Lx.stdin.readLine prompt must be a string.");
    return false;
  }

  if (argCount == 1) {
    ObjString* prompt = AS_STRING(args[0]);
    fwrite(prompt->chars, sizeof(char), (size_t)prompt->length, stdout);
    fflush(stdout);
  }

  char* line = NULL;
  size_t cap = 0;
  ssize_t n = getline(&line, &cap, stdin);
  if (n < 0) {
    free(line);
    args[-1] = NIL_VAL;
    return true;
  }

  args[-1] = OBJ_VAL(takeString(line, (int)n));
  return true;
}

// ---- end of native function declarations ----
// ---- end of native function declarations ----
// ---- end of native function declarations ----

static void defineTableFunction(Table *table, const char *name,
                                NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  ObjString *nameString = COPY_CSTRING(name);
  push(OBJ_VAL(nameString));
  push(OBJ_VAL(newNative(function, nameString)));
  tableSet(table, vm.stackTop[-3], vm.stackTop[-1]);
  pop();
  pop();
  pop();
}

static void defineNative(const char *name, NativeFn function) {
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
  pop();
  pop();

  push(CSTRING_VAL("env"));
  push(OBJ_VAL(newHashmap()));
  tableSet(&AS_HASHMAP(vm.stack[1]), vm.stack[2], vm.stack[3]);
#ifndef WASM
  extern char** environ;
  if (environ != NULL) {
    for (int i = 0; environ[i] != NULL; i++) {
      char* entry = environ[i];
      char* eq = strchr(entry, '=');
      if (eq == NULL || eq == entry) continue;
      size_t keyLen = (size_t)(eq - entry);
      ObjString* key = copyString(entry, (int)keyLen);
      ObjString* value = copyString(eq + 1, (int)strlen(eq + 1));
      push(OBJ_VAL(key));
      push(OBJ_VAL(value));
      tableSet(&AS_HASHMAP(vm.stack[3]), vm.stack[4], vm.stack[5]);
      pop();
      pop();
    }
  }
#endif
  pop();
  pop();

  push(CSTRING_VAL("version"));
  push(CSTRING_VAL(LX_VERSION));
  tableSet(&AS_HASHMAP(vm.stack[1]), vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();

  // Lx.fs
  ObjHashmap* fs = newHashmap();
  push(OBJ_VAL(fs)); // root
  push(CSTRING_VAL("fs"));
  push(OBJ_VAL(fs));
  tableSet(&AS_HASHMAP(vm.stack[1]), vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();
  defineTableFunction(&fs->table, "cwd", fsCwdNative);
  defineTableFunction(&fs->table, "exists", fsExistsNative);
  defineTableFunction(&fs->table, "stat", fsStatNative);
  defineTableFunction(&fs->table, "realpath", fsRealpathNative);
  pop(); // fs

  // Lx.path
  ObjHashmap* path = newHashmap();
  push(OBJ_VAL(path)); // root
  push(CSTRING_VAL("path"));
  push(OBJ_VAL(path));
  tableSet(&AS_HASHMAP(vm.stack[1]), vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();
  defineTableFunction(&path->table, "join", pathJoinNative);
  defineTableFunction(&path->table, "dirname", pathDirnameNative);
  pop(); // path

  // Lx.stdin
  ObjHashmap* stdinTable = newHashmap();
  push(OBJ_VAL(stdinTable)); // root
  push(CSTRING_VAL("stdin"));
  push(OBJ_VAL(stdinTable));
  tableSet(&AS_HASHMAP(vm.stack[1]), vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();
  defineTableFunction(&stdinTable->table, "readAll", stdinReadAllNative);
  defineTableFunction(&stdinTable->table, "readLine", stdinReadLineNative);
  pop(); // stdin

  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "globals", globalsNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "doubleToUint8Array",
                      doubleToUint8ArrayNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "pcall", pcallNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "error", lxErrorNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "isLxObj", lxIsLxObjNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "loadObj", lxLoadObjNative);
  defineTableFunction(&AS_HASHMAP(vm.stack[1]), "exit", exitNative);

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
  defineNative("putc", putcNative);
  defineNative("groan", groanNative);
  defineNative("groanln", groanlnNative);
  defineNative("str", strNative);
  defineNative("join", joinNative);
  defineNative("split", splitNative);
  defineNative("tolower", tolowerNative);
  defineNative("toupper", toupperNative);
  defineNative("tonumber", tonumberNative);
  defineNative("int", intNative);
  defineNative("chr", chrNative);
  defineNative("ord", ordNative);
  defineNative("random", randomNative);
  defineNative("sqrt", sqrtNative);
  defineNative("keys", keysNative);
  defineNative("nameOf", nameOfNative);
  defineNative("len", lenNative);
  defineNative("type", typeNative);
  defineNative("push", pushNative);
  defineNative("pop", popNative);
  defineNative("concat", concatNative);
  defineNative("range", rangeNative);
  defineNative("lines", linesNative);
  defineNative("getline", getlineNative);
  defineNative("read", readNative);

#ifndef WASM
  defineNative("exec", execNative);
  defineNative("system", systemNative);
#endif
  defineNative("slurp", slurpNative);
  defineNative("spit", spitNative);
}
#endif
