#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"
#include "lx/lxlx.h"
#include "lx/lxversion.h"

typedef void (*OptHandler)(int argc, const char* argv[]);

typedef struct {
  const char* name;
  const char* desc;
  OptHandler handler;
} Option;

static void handleHelp(int argc, const char* argv[]);

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

static void handleRunObject(int argc, const char* argv[]) {
  if (argc <= 2) {
    fprintf(stderr, "Usage: %s runo <lxobj>\n", argv[0]);
    return;
  }

  runFile(argv[2]);
}

static void handleCompile(int argc, const char* argv[]) {
  InterpretResult result = interpret((uint8_t*)lxlx_bytecode);

  if (result == INTERPRET_LOADOBJ_ERROR) exit(65);
  if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static void handleCompileAndRun(int argc, const char* argv[]) {
  InterpretResult result = interpret((uint8_t*)lxlx_bytecode);
  Value value;
  if (!tableGet(&vm.globals, CSTRING_VAL("__lx_result__"), &value) ||
      !IS_ARRAY(value)) {
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

static void handleRepl(int argc, const char* argv[]) {
  char line[1024];

  // intern the key
  push(CSTRING_VAL("__lx_input__"));
  pop();

  ObjString* key = COPY_CSTRING("__lx_input__");
  Value value;

  for (;;) {
    fprintf(stderr, "> ");

    char* read = NULL;
    if (!(read = fgets(line, sizeof(line), stdin))) {
      fprintf(stderr, "\n");
      break;
    }
    ObjString* source = COPY_CSTRING(read);
    push(OBJ_VAL(source));
    tableSet(&vm.globals, OBJ_VAL(key), OBJ_VAL(source));
    pop();

    interpret((uint8_t*)lxlx_bytecode);

    if (!tableGet(&vm.globals, CSTRING_VAL("__lx_result__"), &value)) {
      fprintf(stderr, "failed to compile lxobj\n");
      return exit(65);
    } else if (IS_STRING(value)) {
      // got some errors
      fprintf(stderr, "%s\n", AS_STRING(value)->chars);
      continue;
    } else if (!IS_ARRAY(value)) {
      fprintf(stderr, "unexpected lx compiled result\n");
      continue;
    }

    ValueArray* code = &AS_ARRAY(value);
    uint8_t* obj = (uint8_t*)malloc(code->count);
    for (int i = 0; i < code->count; ++i) {
      uint8_t byte = AS_NUMBER(code->values[i]);
      obj[i] = byte;
    }
    interpret(obj);
    free(obj);
  }
}

static void handleVersion(int argc, const char* argv[]) {
  printf("lx version %s\n", LX_VERSION);
}

Option options[] = {
  {"run",        "Compile Lx source and run it", handleCompileAndRun},
  {"runo",       "Run a lxobj file",             handleRunObject},
  {"repl",       "Start Lx REPL",                handleRepl},
  {"compile",    "Compile Lx source to lxobj",   handleCompile},
  {"version",    "Print Lx version",             handleVersion},
  {"help",       "Print this",                   handleHelp},
};

static void handleHelp(int argc, const char* argv[]) {
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
  LX_ARGC = argc;
  LX_ARGV = argv;

  if (argc < 2) {
    handleHelp(argc, argv);
    return 128;
  }

  const char* cmd = argv[1];

  int optionCount = sizeof(options) / sizeof(Option);
  OptHandler* handler = NULL;
  for (int i = 0; i < optionCount; i++) {
    Option opt = options[i];
    if (strcmp(cmd, opt.name) == 0) {
      handler = &opt.handler;
      break;
    }
  }
  if (handler == NULL) {
    handleHelp(argc, argv);
    return 128;
  }

  initVM();
  (*handler)(argc, argv);
  freeVM();

  return 0;
}
