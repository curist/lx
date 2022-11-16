#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"
#include "version.h"

typedef void (*OptHandler)(int argc, const char* argv[]);

typedef struct {
  const char* name;
  const char* desc;
  OptHandler handler;
} Option;

void handleHelp(int argc, const char* argv[]);

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

void handleRun(int argc, const char* argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s run <lxobj>\n", argv[0]);
    return;
  }

  initVM();
  runFile(argv[2]);
  freeVM();
}

void handleCompile(int argc, const char* argv[]) {
  printf("TODO: handle compile\n");
  // InterpretResult result = interpret((uint8_t*)compiler_lxobj);

  // if (result == INTERPRET_LOADOBJ_ERROR) exit(65);
  // if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

void handleVersion(int argc, const char* argv[]) {
  printf("lx version %s\n", LX_VERSION);
}

Option options[] = {
  {"run",        "Run a lxobj file",           handleRun},
  {"compile",    "Compile Lx source to lxobj", handleCompile},
  {"version",    "Print Lx version",           handleVersion},
  {"help",       "Print Lx help screen",       handleHelp},
};

void handleHelp(int argc, const char* argv[]) {
  int optionCount = sizeof(options) / sizeof(Option);
  fprintf(stderr,
      "Usage:\n\n"
      "\t%s <command> [arguments]\n\n"
      "The commands are:\n",
      argv[0]
      );
  for (int i = 0; i < optionCount; i++) {
    Option opt = options[i];
    fprintf(stderr, "\t%-12s %s\n", opt.name, opt.desc);
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
  bool handled = false;
  for (int i = 0; i < optionCount; i++) {
    Option opt = options[i];
    if (strcmp(cmd, opt.name) == 0) {
      handled =  true;
      opt.handler(argc, argv);
      break;
    }
  }
  if (!handled) {
    handleHelp(argc, argv);
    return 128;
  }

  // XXX: plan
  // embed lxlox compiler
  // to be able to run source file directly,
  // we would need to
  // 1. use lxlox, running by clox VM, to compile the source code
  // 2. extract data from VM, before VM returns or find a more general way
  //    to let the VM to return values to c
  // 3. run the VM again, using bytecodes emitted from above step
  //
  // XXX: plan
  // use lx sources, to implement major features
  // clox/clxo will have lx sources enhanced commands;
  // small utitilties could be in their own sources;
  // large utitiles may use single source to support multiple commands,
  // to reduce overheads

  return 0;
}
