#ifndef clox_objloader_h
#define clox_objloader_h
#include "vm.h"
#include "memory.h"
#include "object.h"

typedef struct {
  Chunk* chunk;
  int index;
} ChunkValueIndex;

typedef struct {
  uint32_t count;
  uint32_t capacity;
  ChunkValueIndex* values;
} ChunkIndexes;

ObjFunction* loadObj(uint8_t* bytes);

#endif
