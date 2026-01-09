#include <stdio.h>

#include "debug.h"
#include "object.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* filename, const char* name, bool printCode) {
  printf("%s -> %s\n================================\n", filename, name);

  for (size_t offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(chunk, offset, printCode);
  }
  printf("\n");
}

static int simpleInstruction(const char* name, int offset) {
  printf("%s\n", name);
  return offset + 1;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t slot = chunk->code[offset + 1];
  printf("%-16s %4d\n", name, slot);
  return offset + 2; 
}

static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset) {
  uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  printf("%-17s %4d -> %d\n", name, offset,
         offset + 3 + sign * jump);
  return offset + 3;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t constant = chunk->code[offset + 1];
  printf("%-16s %4d '", name, constant);
  printValue(stdout, chunk->constants.values[constant]);
  printf("'\n");
  return offset + 2;
}

static int constantLongInstruction(const char* name, Chunk* chunk, int offset) {
  uint16_t constant = (uint16_t)(chunk->code[offset + 1] << 8);
  constant |= chunk->code[offset + 2];
  printf("%-16s %4d '", name, constant);
  printValue(stdout, chunk->constants.values[constant]);
  printf("'\n");
  return offset + 3;
}

static int byteLongInstruction(const char* name, Chunk* chunk, int offset) {
  uint16_t slot = (uint16_t)(chunk->code[offset + 1] << 8);
  slot |= chunk->code[offset + 2];
  printf("%-16s %4d\n", name, slot);
  return offset + 3;
}

static int constByteInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t constant = chunk->code[offset + 1];
  printf("%-16s %4d\n", name, constant);
  return offset + 2;
}

static int unwindInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t count = chunk->code[offset + 1];
  uint8_t keep = chunk->code[offset + 2];
  printf("%-16s %4d %4d\n", name, count, keep);
  return offset + 3;
}

static int twoByteInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t byte1 = chunk->code[offset + 1];
  uint8_t byte2 = chunk->code[offset + 2];
  printf("%-16s %4d %4d\n", name, byte1, byte2);
  return offset + 3;
}

static int threeByteInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t byte1 = chunk->code[offset + 1];
  uint8_t byte2 = chunk->code[offset + 2];
  uint8_t byte3 = chunk->code[offset + 3];
  printf("%-16s %4d %4d %4d\n", name, byte1, byte2, byte3);
  return offset + 4;
}

static int forLoopInstruction(const char* name, int sign, Chunk* chunk, int offset) {
  uint8_t i_slot = chunk->code[offset + 1];
  uint8_t limit_slot = chunk->code[offset + 2];
  uint8_t cmp_kind = chunk->code[offset + 3];

  // Check if this is a stepped variant (OP_FORPREP/OP_FORLOOP)
  OpCode op = chunk->code[offset];
  bool has_step = (op == OP_FORPREP || op == OP_FORLOOP);

  int8_t step = 1;
  int offset_idx = 4;
  if (has_step) {
    step = (int8_t)chunk->code[offset + 4];  // Signed step
    offset_idx = 5;
  }

  uint16_t jump = (uint16_t)(chunk->code[offset + offset_idx] << 8);
  jump |= chunk->code[offset + offset_idx + 1];

  const char* cmp_str;
  switch (cmp_kind) {
    case 0: cmp_str = "<"; break;
    case 1: cmp_str = "<="; break;
    case 2: cmp_str = ">"; break;
    case 3: cmp_str = ">="; break;
    default: cmp_str = "?"; break;
  }

  int instruction_length = has_step ? 7 : 6;
  printf("%-19s i=%d limit=%d %s step=%d -> %d\n", name, i_slot, limit_slot,
         cmp_str, step, offset + instruction_length + sign * jump);
  return offset + instruction_length;
}

int disassembleInstruction(Chunk* chunk, int offset, bool printCode) {
  printf("%04d ", offset);

  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    if (printCode) {
      printf("   |  ");
    } else {
      printf("   |--");
    }
  } else {
    printf("%4d--", chunk->lines[offset]);
  }

  uint8_t instruction = chunk->code[offset];
  printf("%02x ", instruction);
  switch (instruction) {
    case OP_NOP:
      return simpleInstruction("OP_NOP", offset);
    case OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);
    case OP_CONSTANT_LONG:
      return constantLongInstruction("OP_CONSTANT_LONG", chunk, offset);
    case OP_CONST_BYTE:
      return constByteInstruction("OP_CONST_BYTE", chunk, offset);
    case OP_NIL:
      return simpleInstruction("OP_NIL", offset);
    case OP_TRUE:
      return simpleInstruction("OP_TRUE", offset);
    case OP_FALSE:
      return simpleInstruction("OP_FALSE", offset);
    case OP_POP:
      return simpleInstruction("OP_POP", offset);
    case OP_DUP:
      return simpleInstruction("OP_DUP", offset);
    case OP_GET_LOCAL:
      return byteInstruction("OP_GET_LOCAL", chunk, offset);
    case OP_SET_LOCAL:
      return byteInstruction("OP_SET_LOCAL", chunk, offset);
    case OP_GET_GLOBAL:
      return constantInstruction("OP_GET_GLOBAL", chunk, offset);
    case OP_GET_GLOBAL_LONG:
      return constantLongInstruction("OP_GET_GLOBAL_LONG", chunk, offset);
    case OP_DEFINE_GLOBAL:
      return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
    case OP_DEFINE_GLOBAL_LONG:
      return constantLongInstruction("OP_DEFINE_GLOBAL_LONG", chunk, offset);
    case OP_SET_GLOBAL:
      return constantInstruction("OP_SET_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL_LONG:
      return constantLongInstruction("OP_SET_GLOBAL_LONG", chunk, offset);
    case OP_GET_UPVALUE:
      return byteInstruction("OP_GET_UPVALUE", chunk, offset);
    case OP_GET_UPVALUE_LONG:
      return byteLongInstruction("OP_GET_UPVALUE_LONG", chunk, offset);
    case OP_SET_UPVALUE:
      return byteInstruction("OP_SET_UPVALUE", chunk, offset);
    case OP_SET_UPVALUE_LONG:
      return byteLongInstruction("OP_SET_UPVALUE_LONG", chunk, offset);
    case OP_GET_BY_INDEX:
      return simpleInstruction("OP_GET_BY_INDEX", offset);
    case OP_SET_BY_INDEX:
      return simpleInstruction("OP_SET_BY_INDEX", offset);
    case OP_GET_BY_CONST:
      return constantInstruction("OP_GET_BY_CONST", chunk, offset);
    case OP_GET_BY_CONST_LONG:
      return constantLongInstruction("OP_GET_BY_CONST_LONG", chunk, offset);
    case OP_SET_BY_CONST:
      return constantInstruction("OP_SET_BY_CONST", chunk, offset);
    case OP_SET_BY_CONST_LONG:
      return constantLongInstruction("OP_SET_BY_CONST_LONG", chunk, offset);
    case OP_EQUAL:
      return simpleInstruction("OP_EQUAL", offset);
    case OP_GREATER:
      return simpleInstruction("OP_GREATER", offset);
    case OP_LESS:
      return simpleInstruction("OP_LESS", offset);
    case OP_ADD:
      return simpleInstruction("OP_ADD", offset);
    case OP_SUBTRACT:
      return simpleInstruction("OP_SUBTRACT", offset);
    case OP_MULTIPLY:
      return simpleInstruction("OP_MULTIPLY", offset);
    case OP_DIVIDE:
      return simpleInstruction("OP_DIVIDE", offset);
    case OP_MOD:
      return simpleInstruction("OP_MOD", offset);
    case OP_NOT:
      return simpleInstruction("OP_NOT", offset);
    case OP_NEGATE:
      return simpleInstruction("OP_NEGATE", offset);
    case OP_ADD_INT:
      return simpleInstruction("OP_ADD_INT", offset);
    case OP_SUBTRACT_INT:
      return simpleInstruction("OP_SUBTRACT_INT", offset);
    case OP_MULTIPLY_INT:
      return simpleInstruction("OP_MULTIPLY_INT", offset);
    case OP_NEGATE_INT:
      return simpleInstruction("OP_NEGATE_INT", offset);
    case OP_ADD_NUM:
      return simpleInstruction("OP_ADD_NUM", offset);
    case OP_ADD_STR:
      return simpleInstruction("OP_ADD_STR", offset);
    case OP_ASSOC:
      return simpleInstruction("OP_ASSOC", offset);
    case OP_APPEND:
      return simpleInstruction("OP_APPEND", offset);
    case OP_HASHMAP:
      return simpleInstruction("OP_HASHMAP", offset);
    case OP_ENUM:
      return simpleInstruction("OP_ENUM", offset);
    case OP_ARRAY:
      return simpleInstruction("OP_ARRAY", offset);
    case OP_LENGTH:
      return simpleInstruction("OP_LENGTH", offset);
    case OP_JUMP:
      return jumpInstruction("OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_TRUE:
      return jumpInstruction("OP_JUMP_IF_TRUE", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
      return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OP_LOOP:
      return jumpInstruction("OP_LOOP", -1, chunk, offset);
    case OP_CALL:
      return byteInstruction("OP_CALL", chunk, offset);
    case OP_CLOSURE: {
      offset++;
      uint8_t constant = chunk->code[offset++];
      printf("%-16s %4d ", "OP_CLOSURE", constant);
      printValue(stdout, chunk->constants.values[constant]);
      printf("\n");

      ObjFunction* function = AS_FUNCTION(
          chunk->constants.values[constant]);
      for (int j = 0; j < function->upvalueCount; j++) {
        int isLocal = chunk->code[offset++];
        int index = chunk->code[offset++];
        printf("%04d      |                       %s %d\n",
               offset - 2, isLocal ? "local  " : "upvalue", index);
      }

      return offset;
    }
    case OP_CLOSURE_LONG: {
      offset++;
      uint16_t constant = (uint16_t)(chunk->code[offset] << 8);
      constant |= chunk->code[offset + 1];
      offset += 2;

      printf("%-16s %4d ", "OP_CLOSURE_LONG", constant);
      printValue(stdout, chunk->constants.values[constant]);
      printf("\n");

      ObjFunction* function = AS_FUNCTION(
          chunk->constants.values[constant]);
      for (int j = 0; j < function->upvalueCount; j++) {
        int isLocal = chunk->code[offset++];
        int index = chunk->code[offset++];
        printf("%04d      |                       %s %d\n",
               offset - 2, isLocal ? "local  " : "upvalue", index);
      }

      return offset;
    }
    case OP_CLOSE_UPVALUE:
      return simpleInstruction("OP_CLOSE_UPVALUE", offset);
    case OP_UNWIND:
      return unwindInstruction("OP_UNWIND", chunk, offset);
    case OP_ADD_LOCAL_IMM:
      return twoByteInstruction("OP_ADD_LOCAL_IMM", chunk, offset);
    case OP_STORE_LOCAL:
      return byteInstruction("OP_STORE_LOCAL", chunk, offset);
    case OP_GETI:
      return twoByteInstruction("OP_GETI", chunk, offset);
    case OP_SETI:
      return threeByteInstruction("OP_SETI", chunk, offset);
    case OP_ADD_LOCALS:
      return threeByteInstruction("OP_ADD_LOCALS", chunk, offset);
    case OP_SUB_LOCALS:
      return threeByteInstruction("OP_SUB_LOCALS", chunk, offset);
    case OP_MUL_LOCALS:
      return threeByteInstruction("OP_MUL_LOCALS", chunk, offset);
    case OP_DIV_LOCALS:
      return threeByteInstruction("OP_DIV_LOCALS", chunk, offset);
    case OP_GET_PROPERTY:
      return twoByteInstruction("OP_GET_PROPERTY", chunk, offset);
    case OP_SET_PROPERTY:
      return threeByteInstruction("OP_SET_PROPERTY", chunk, offset);
    case OP_COALESCE_CONST:
      return constantInstruction("OP_COALESCE_CONST", chunk, offset);
    case OP_COALESCE_CONST_LONG:
      return constantLongInstruction("OP_COALESCE_CONST_LONG", chunk, offset);
    case OP_MOD_CONST_BYTE:
      return byteInstruction("OP_MOD_CONST_BYTE", chunk, offset);
    case OP_EQ_CONST_BYTE:
      return byteInstruction("OP_EQ_CONST_BYTE", chunk, offset);
    case OP_FORPREP_1:
      return forLoopInstruction("OP_FORPREP_1", 1, chunk, offset);
    case OP_FORLOOP_1:
      return forLoopInstruction("OP_FORLOOP_1", -1, chunk, offset);
    case OP_FORPREP:
      return forLoopInstruction("OP_FORPREP", 1, chunk, offset);
    case OP_FORLOOP:
      return forLoopInstruction("OP_FORLOOP", -1, chunk, offset);
    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);
    default:
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}
