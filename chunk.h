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
  OP_EQUAL,
  OP_POP,
  OP_POPN,
  OP_POP2,
  OP_POP2N,
  OP_DUP,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_GET_GLOBAL,
  OP_DEFINE_GLOBAL,
  OP_SET_GLOBAL,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  OP_GET_PROPERTY,
  OP_SET_PROPERTY,
  OP_GET_BY_INDEX,
  OP_SET_BY_INDEX,
  OP_GREATER,
  OP_LESS,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NOT,
  OP_MOD,
  OP_NEGATE,
  OP_JUMP,
  OP_JUMP_IF_TRUE,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_ASSOC,
  OP_APPEND,
  OP_HASHMAP,
  OP_ARRAY,
  OP_CALL,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
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
