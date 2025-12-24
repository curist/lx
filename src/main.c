#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "common.h"
#include "vm.h"
#include "objloader.h"
#include "lx/lxlx.h"
#include "lx/lxversion.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_ASYNC_JS(char*, js_prompt, (), {
  const line = await getLineInput();
  var lengthBytes = lengthBytesUTF8(line)+1;
  var heap = _malloc(lengthBytes);
  stringToUTF8(line, heap, lengthBytes);
  return heap;
});
#endif

typedef void (*OptHandler)(int argc, const char* argv[]);

typedef struct {
  const char* name;
  const char* abbr;
  const char* desc;
  OptHandler handler;
} Option;

static void handleHelp(int argc, const char* argv[]);

static bool checkIsLxObj(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  uint8_t* buffer = (uint8_t*)malloc(fileSize);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }
  size_t bytesRead = fread(buffer, sizeof(uint8_t), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }
  // basic lxobj header check
  if (fileSize < 20) {
    fclose(file);
    free(buffer);
    return false;
  }
  size_t obj_size = 0;
  obj_size += buffer[4];
  obj_size += buffer[5] << 8;
  obj_size += buffer[6] << 16;
  obj_size += buffer[7] << 24;

  fclose(file);
  free(buffer);

  if (fileSize != obj_size) {
    return false;
  }

  return true;
}

static uint8_t* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  uint8_t* buffer = (uint8_t*)malloc(fileSize);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }
  size_t bytesRead = fread(buffer, sizeof(uint8_t), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }
  // basic lxobj header check
  if (fileSize < 20) {
    fprintf(stderr, "Invalid lxobj \"%s\".\n", path);
    exit(74);
  }
  size_t obj_size = 0;
  obj_size += buffer[4];
  obj_size += buffer[5] << 8;
  obj_size += buffer[6] << 16;
  obj_size += buffer[7] << 24;

  if (fileSize != obj_size) {
    fprintf(stderr, "Invalid lxobj \"%s\": size mismatch.\n", path);
    exit(75);
  }

  fclose(file);
  return buffer;
}

static void runFile(const char* path) {
  uint8_t* obj = readFile(path);
  InterpretResult result = interpret(obj);
  free(obj);

  if (result == INTERPRET_LOADOBJ_ERROR) exit(65);
  if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static void handleCompile(int argc, const char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s compile <lxfile> [-o|--output <output>]\n", argv[0]);
    return;
  }
  InterpretResult result = interpret((uint8_t*)lxlx_bytecode);

  if (result == INTERPRET_LOADOBJ_ERROR) exit(65);
  if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static void handleCompileAndRun(int argc, const char* argv[]) {
  InterpretResult result = interpret((uint8_t*)lxlx_bytecode);
  Value value = vm.lastResult;
  if (!IS_ARRAY(value)) {
    fprintf(stderr, "failed to compile lxobj\n");
    freeVM();
    return exit(65);
  }
  // assume we got a valid bytes array (which stored in doubles :()
  ValueArray* code = &AS_ARRAY(value);
  uint8_t* obj = (uint8_t*)malloc(code->count);
  for (int i = 0; i < code->count; ++i) {
    uint8_t byte = AS_NUMBER(code->values[i]);
    obj[i] = byte;
  }
  result = interpret(obj);

  free(obj);

  if (result == INTERPRET_LOADOBJ_ERROR) exit(65);
  if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static void handleRun(int argc, const char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s run <lxfile>\n", argv[0]);
    return;
  }
  const char* path = argv[2];
  if (checkIsLxObj(path)) {
    return runFile(path);
  }
  handleCompileAndRun(argc, argv);
}

static void handleEval(int argc, const char* argv[]) {
  if (argc <= 2) {
    fprintf(stderr, "Usage: %s eval <expression>\n", argv[0]);
    exit(64);
  }

  // 1) Set __lx_input__ in vm.globals (host -> compiler-driver script).
  ObjString* key = COPY_CSTRING("__lx_input__");
  ObjString* source = COPY_CSTRING(argv[2]);

  // Root 'source' while inserting into globals in case table grows / GC runs.
  push(OBJ_VAL(source));
  tableSet(&vm.globals, OBJ_VAL(key), OBJ_VAL(source));
  pop();

  // 2) Run lxlx_bytecode (compiler-driver). It returns either:
  //    - string error message, or
  //    - array of bytes (stored as doubles) representing an lxobj.
  InterpretResult r = interpret((uint8_t*)lxlx_bytecode);
  if (r != INTERPRET_OK) {
    fprintf(stderr, "failed to run eval compiler\n");
    exit(65);
  }

  Value compiled = vm.lastResult;

  if (IS_STRING(compiled)) {
    fprintf(stderr, "%s\n", AS_STRING(compiled)->chars);
    exit(65);
  }
  if (!IS_ARRAY(compiled)) {
    fprintf(stderr, "unexpected eval compiler result (expected string or array)\n");
    exit(65);
  }

  // 3) Convert the returned "bytes array" -> uint8_t buffer.
  ValueArray* code = &AS_ARRAY(compiled);
  uint8_t* obj = (uint8_t*)malloc((size_t)code->count);
  if (obj == NULL) {
    fprintf(stderr, "out of memory\n");
    exit(70);
  }

  for (int i = 0; i < code->count; ++i) {
    // Defensive: ensure elements are numbers.
    if (!IS_NUMBER(code->values[i])) {
      fprintf(stderr, "unexpected bytecode element type\n");
      free(obj);
      exit(65);
    }
    double n = AS_NUMBER(code->values[i]);
    // Optional: range check
    if (n < 0 || n > 255 || (uint8_t)n != n) {
      fprintf(stderr, "invalid byte value in compiled output\n");
      free(obj);
      exit(65);
    }
    obj[i] = (uint8_t)n;
  }

  // 4) Run the compiled object. Decide what to print.
  r = interpret(obj);
  free(obj);

  if (r != INTERPRET_OK) {
    exit(r == INTERPRET_RUNTIME_ERROR ? 70 : 65);
  }

  // If your top-level OP_RETURN stashes lastResult for all scripts,
  // the evaluated expression result should be vm.lastResult here.
  printValue(stdout, vm.lastResult);
  printf("\n");
}

static void handleRepl(int argc, const char* argv[]) {
#ifndef __EMSCRIPTEN__
  char line[1024];
#endif

  // Create the key once. It will remain reachable because it's used in vm.globals.
  ObjString* key = COPY_CSTRING("__lx_input__");

  for (;;) {
#ifdef __EMSCRIPTEN__
    char* read = js_prompt();
    if (read == NULL) break;
#else
    printf("> ");
    char* read = fgets(line, sizeof(line), stdin);
    if (read == NULL) {
      fprintf(stderr, "\n");
      break;
    }
#endif

    // Store input into vm.globals["__lx_input__"] (root source during tableSet).
    ObjString* source = COPY_CSTRING(read);
    push(OBJ_VAL(source));
    tableSet(&vm.globals, OBJ_VAL(key), OBJ_VAL(source));
    pop();

    // 1) Compile snippet via lxlx_bytecode (returns string error or bytes array).
    InterpretResult r = interpret((uint8_t*)lxlx_bytecode);
    if (r != INTERPRET_OK) {
      // Keep REPL alive; print a generic error.
      fprintf(stderr, "compile driver failed\n");
#ifdef __EMSCRIPTEN__
      free(read);
#endif
      continue;
    }

    Value compiled = vm.lastResult;

    if (IS_STRING(compiled)) {
      fprintf(stderr, "%s\n", AS_STRING(compiled)->chars);
#ifdef __EMSCRIPTEN__
      free(read);
#endif
      continue;
    }

    if (!IS_ARRAY(compiled)) {
      fprintf(stderr, "unexpected compiler result (expected string or array)\n");
#ifdef __EMSCRIPTEN__
      free(read);
#endif
      continue;
    }

    // 2) Convert bytes array -> obj buffer.
    ValueArray* code = &AS_ARRAY(compiled);
    uint8_t* obj = (uint8_t*)malloc((size_t)code->count);
    if (obj == NULL) {
      fprintf(stderr, "out of memory\n");
#ifdef __EMSCRIPTEN__
      free(read);
#endif
      continue;
    }

    bool ok = true;
    for (int i = 0; i < code->count; ++i) {
      if (!IS_NUMBER(code->values[i])) { ok = false; break; }
      double n = AS_NUMBER(code->values[i]);
      if (n < 0 || n > 255 || (uint8_t)n != n) { ok = false; break; }
      obj[i] = (uint8_t)n;
    }

    if (!ok) {
      fprintf(stderr, "invalid compiled bytecode\n");
      free(obj);
#ifdef __EMSCRIPTEN__
      free(read);
#endif
      continue;
    }

    // 3) Run the compiled object. It should set vm.lastResult to the snippet value.
    r = interpret(obj);
    free(obj);

    if (r != INTERPRET_OK) {
      // Runtime errors should already print via runtimeError().
      // Keep REPL alive.
#ifdef __EMSCRIPTEN__
      free(read);
#endif
      continue;
    }

    printf("=> ");
    printValue(stdout, vm.lastResult);
    printf("\n");

#ifdef __EMSCRIPTEN__
    free(read);
#endif
  }
}

static void handleDisasm(int argc, const char* argv[]) {
  if (argc <= 2) {
    fprintf(stderr, "Usage: %s disasm <lxobj|lxfile>\n", argv[0]);
    return;
  }

  const char* path = argv[2];
  if (checkIsLxObj(path)) {
    uint8_t* obj = readFile(path);
    loadObj(obj, true);
    free(obj);
    return;
  }

  Value lxValue;
  if (!tableGet(&vm.globals, CSTRING_VAL("Lx"), &lxValue) ||
      !IS_HASHMAP(lxValue)) {
    fprintf(stderr, "failed to access Lx globals\n");
    return exit(65);
  }
  Table* lx = &AS_HASHMAP(lxValue);

  Value oldArgs;
  bool hasOldArgs = tableGet(lx, CSTRING_VAL("args"), &oldArgs);

  ObjArray* argsArray = newArray();
  push(OBJ_VAL(argsArray));
  push(CSTRING_VAL(argv[0]));
  writeValueArray(&argsArray->array, vm.stackTop[-1]);
  pop();
  push(CSTRING_VAL("run"));
  writeValueArray(&argsArray->array, vm.stackTop[-1]);
  pop();
  push(CSTRING_VAL(path));
  writeValueArray(&argsArray->array, vm.stackTop[-1]);
  pop();

  push(CSTRING_VAL("args"));
  push(OBJ_VAL(argsArray));
  tableSet(lx, vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();

  interpret((uint8_t*)lxlx_bytecode);

  if (hasOldArgs) {
    push(CSTRING_VAL("args"));
    push(oldArgs);
    tableSet(lx, vm.stackTop[-2], vm.stackTop[-1]);
    pop();
    pop();
  }

  Value value = vm.lastResult;
  if (!IS_ARRAY(value)) {
    fprintf(stderr, "failed to compile lxobj\n");
    return exit(65);
  }
  ValueArray* code = &AS_ARRAY(value);
  uint8_t* obj = (uint8_t*)malloc(code->count);
  for (int i = 0; i < code->count; ++i) {
    uint8_t byte = AS_NUMBER(code->values[i]);
    obj[i] = byte;
  }
  loadObj(obj, true);
  free(obj);
}

static void handleVersion(int argc, const char* argv[]) {
  printf("lx version %s\n", LX_VERSION);
}

Option options[] = {
  {"run",      "r",  "Run source or lxobj",       handleRun},
  {"eval",     NULL, "Evaluate expression",       handleEval},
  {"repl",     NULL, "Start REPL",                handleRepl},
  {"compile",  "c",  "Compile source to lxobj (-o/--output <output>)",   handleCompile},
  {"disasm",   "d",  "Disassemble lxobj or lx source", handleDisasm},
  {"version",  NULL, "Print version",             handleVersion},
  {"help",     NULL, "Print this helpful page",   handleHelp},
};

static void handleHelp(int argc, const char* argv[]) {
  if (argc == 0) return exit(127);

  int optionCount = sizeof(options) / sizeof(Option);
  fprintf(stderr,
      "Usage:\n\n"
      "  %s <command> [arguments]\n\n"
      "The commands are:\n",
      argv[0]
      );
  for (int i = 0; i < optionCount; i++) {
    Option opt = options[i];
    fprintf(stderr, "  %-12s %s\n", opt.name, opt.desc);
  }
}

int main(int argc, const char* argv[]) {
  srand(time(NULL));
  rand();

  LX_ARGC = argc;
  LX_ARGV = argv;

  if (argc < 2) {
    handleHelp(argc, argv);
    return 28;
  }

  const char* cmd = argv[1];

  int optionCount = sizeof(options) / sizeof(Option);
  OptHandler* handler = NULL;
  for (int i = 0; i < optionCount; i++) {
    if ((options[i].abbr != NULL && strcmp(cmd, options[i].abbr) == 0)
      || (strcmp(cmd, options[i].name) == 0)) {
      handler = &options[i].handler;
      break;
    }
  }
  if (handler == NULL) {
    handleHelp(argc, argv);
    return 28;
  }

  initVM();
  (*handler)(argc, argv);
  freeVM();

  return 0;
}
