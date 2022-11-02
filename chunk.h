#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
  OP_CONSTANT,
  OP_CONST_BYTE,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NOT,
  OP_MOD,
  OP_NEGATE,
  OP_RETURN,
} OpCode;

typedef struct {
  uint32_t count;
  uint32_t capacity;
  uint8_t* code;
  uint32_t* lines;
  ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, uint32_t line);
int addConstant(Chunk* chunk, Value value);

#endif
