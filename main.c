#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"


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
  obj_size += buffer[3];
  obj_size += buffer[4] << 8;
  obj_size += buffer[5] << 16;
  obj_size += buffer[6] << 24;

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

int main(int argc, const char* argv[]) {
  initVM();

  if (argc == 2) {
    runFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: clox <lxobj>\n");
    exit(64);
  }

  freeVM();

  return 0;
}
