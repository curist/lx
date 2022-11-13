#include <stdio.h>

#include "debug.h"
#include "object.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* filename, const char* name, bool printCode) {
  printf("\n%s -> %s\n================================\n", filename, name);

  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(chunk, offset, printCode);
  }
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
  printf("%-16s %4d -> %d\n", name, offset,
         offset + 3 + sign * jump);
  return offset + 3;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t constant = chunk->code[offset + 1];
  printf("%-16s %4d '", name, constant);
  printValue(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 2;
}

static int constByteInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t constant = chunk->code[offset + 1];
  printf("%-16s %4d\n", name, constant);
  return offset + 2;
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
  switch (instruction) {
    case OP_NOP:
      return simpleInstruction("OP_NOP", offset);
    case OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);
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
    case OP_NEW_LOCAL:
      return simpleInstruction("OP_NEW_LOCAL", offset);
    case OP_POP_LOCAL:
      return simpleInstruction("OP_POP_LOCAL", offset);
    case OP_GET_LOCAL:
      return byteInstruction("OP_GET_LOCAL", chunk, offset);
    case OP_SET_LOCAL:
      return byteInstruction("OP_SET_LOCAL", chunk, offset);
    case OP_GET_GLOBAL:
      return constantInstruction("OP_GET_GLOBAL", chunk, offset);
    case OP_DEFINE_GLOBAL:
      return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL:
      return constantInstruction("OP_SET_GLOBAL", chunk, offset);
    case OP_GET_UPVALUE:
      return byteInstruction("OP_GET_UPVALUE", chunk, offset);
    case OP_SET_UPVALUE:
      return byteInstruction("OP_SET_UPVALUE", chunk, offset);
    case OP_GET_PROPERTY:
      return constantInstruction("OP_GET_PROPERTY", chunk, offset);
    case OP_SET_PROPERTY:
      return constantInstruction("OP_SET_PROPERTY", chunk, offset);
    case OP_GET_BY_INDEX:
      return simpleInstruction("OP_GET_BY_INDEX", offset);
    case OP_SET_BY_INDEX:
      return simpleInstruction("OP_SET_BY_INDEX", offset);
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
    case OP_HASHMAP:
      return simpleInstruction("OP_HASHMAP", offset);
    case OP_ARRAY:
      return simpleInstruction("OP_ARRAY", offset);
    case OP_ASSOC:
      return simpleInstruction("OP_ASSOC", offset);
    case OP_APPEND:
      return simpleInstruction("OP_APPEND", offset);
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
      printValue(chunk->constants.values[constant]);
      printf("\n");

      ObjFunction* function = AS_FUNCTION(
          chunk->constants.values[constant]);
      for (int j = 0; j < function->upvalueCount; j++) {
        int isLocal = chunk->code[offset++];
        int index = chunk->code[offset++];
        printf("%04d      |                     %s %d\n",
               offset - 2, isLocal ? "local" : "upvalue", index);
      }

      return offset;
    }
    case OP_CLOSE_UPVALUE:
      return simpleInstruction("OP_CLOSE_UPVALUE", offset);
    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);
    default:
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}
