#ifndef clox_objloader_h
#define clox_objloader_h
#include "vm.h"
#include "memory.h"
#include "object.h"

static bool objectLoaded = false;

typedef struct {
  Chunk* chunk;
  int index;
} ChunkValueIndex;

typedef struct {
  uint32_t count;
  uint32_t capacity;
  ChunkValueIndex* values;
} ChunkIndexes;

static void initChunkIndexes(ChunkIndexes* array) {
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}
static void writeChunkIndexes(ChunkIndexes* array, ChunkValueIndex value) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values = GROW_ARRAY(ChunkValueIndex, array->values,
                               oldCapacity, array->capacity);
  }
  array->values[array->count] = value;
  array->count++;
}
static void freeChunkIndexes(ChunkIndexes* array) {
  FREE_ARRAY(ChunkValueIndex, array->values, array->capacity);
  initChunkIndexes(array);
}

ObjFunction* loadObj(uint8_t* bytes);

#endif
