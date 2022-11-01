#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

int main(int argc, const char* argv[]) {
  initVM();

  Chunk chunk;
  initChunk(&chunk);

  writeChunk(&chunk, OP_CONST_BYTE, 123);
  writeChunk(&chunk, 101, 123);

  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, addConstant(&chunk, 1.2), 123);

  writeChunk(&chunk, OP_NEGATE, 123);

  writeChunk(&chunk, OP_ADD, 123);

  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, addConstant(&chunk, 5.6), 123);

  writeChunk(&chunk, OP_DIVIDE, 123);

  writeChunk(&chunk, OP_CONST_BYTE, 130);
  writeChunk(&chunk, 19, 130);
  writeChunk(&chunk, OP_CONST_BYTE, 130);
  writeChunk(&chunk, 10, 130);
  writeChunk(&chunk, OP_MOD, 130);

  for (uint32_t i = 0; i < 1e8; ++i) {
    writeChunk(&chunk, OP_CONST_BYTE, 130);
    writeChunk(&chunk, 19, 130);
    writeChunk(&chunk, OP_ADD, 200);
  }

  writeChunk(&chunk, OP_RETURN, 200);

  interpret(&chunk);
  freeVM();
  freeChunk(&chunk);

  return 0;
}
