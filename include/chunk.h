#ifndef clox_chunk_h
#define clox_chunk_h

#include "value.h"

typedef enum {
  // Control flow
  OP_NOP,
  OP_JUMP,
  OP_JUMP_IF_TRUE,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_CALL,
  OP_CLOSURE,
  OP_CLOSURE_LONG,
  OP_CLOSE_UPVALUE,
  OP_UNWIND,

  // Constants
  OP_CONSTANT,
  OP_CONSTANT_LONG,
  OP_CONST_BYTE,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,

  // Stack manipulation
  OP_POP,
  OP_DUP,
  OP_SWAP,

  // Variables
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_GET_GLOBAL,
  OP_GET_GLOBAL_LONG,
  OP_DEFINE_GLOBAL,
  OP_DEFINE_GLOBAL_LONG,
  OP_SET_GLOBAL,
  OP_SET_GLOBAL_LONG,
  OP_GET_UPVALUE,
  OP_GET_UPVALUE_LONG,
  OP_SET_UPVALUE,
  OP_SET_UPVALUE_LONG,

  // Arithmetic (baseline)
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_MOD,
  OP_NEGATE,

  // Arithmetic (specialized int)
  OP_ADD_INT,
  OP_SUBTRACT_INT,
  OP_MULTIPLY_INT,
  OP_NEGATE_INT,

  // Arithmetic (quickened)
  OP_ADD_NUM,
  OP_ADD_STR,

  // Comparison
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,

  // Logical
  OP_NOT,

  // Bitwise
  OP_BIT_AND,
  OP_BIT_OR,
  OP_BIT_XOR,
  OP_BIT_LSHIFT,
  OP_BIT_RSHIFT,

  // Data structures
  OP_ARRAY,
  OP_HASHMAP,
  OP_ENUM,
  OP_LENGTH,
  OP_GET_BY_INDEX,
  OP_SET_BY_INDEX,
  OP_ASSOC,
  OP_APPEND,

  // Superinstructions
  OP_ADD_LOCAL_IMM,       // GET_LOCAL + CONST_BYTE + ADD + SET_LOCAL
  OP_STORE_LOCAL,         // SET_LOCAL + POP
  OP_GETI,                // GET_LOCAL×2 + GET_BY_INDEX
  OP_SETI,                // GET_LOCAL×3 + SET_BY_INDEX

  // Special/optimization
  OP_COALESCE_CONST,      // Replace TOS with constant if TOS is falsy
  OP_COALESCE_CONST_LONG,
  OP_MOD_CONST_BYTE,      // TOS = TOS % imm8 (specialized for constant modulo)
  OP_EQ_CONST_BYTE,       // TOS = (TOS == imm8) (specialized for constant equality)

  // Fused numeric for loops (appended to avoid renumbering existing opcodes)
  OP_FORPREP_1,           // Numeric for loop prepare (step=1)
  OP_FORLOOP_1,           // Numeric for loop iterate (step=1)
  OP_FORPREP,             // Numeric for loop prepare (arbitrary signed step)
  OP_FORLOOP,             // Numeric for loop iterate (arbitrary signed step)

  OP_RETURN = 0xff,
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
