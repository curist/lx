#include <stdio.h>

#include "common.h"
#include "objloader.h"
#include "chunk.h"

// obj layout
// LXOBJ:     5
// VERSION:   1
// OBJSIZE:   4 little endian
// TBD:       32 - 10 = 22
// CODE_SECTION: ?
//      SIZE: 4 little endian
// constants section should be defined at TBD: later
bool loadObj(const uint8_t* bytes, Chunk* chunk) {
  if (!(bytes[0] == 'L' && bytes[1] == 'X' &&
        bytes[2] == 'O' && bytes[3] == 'B' && bytes[4] == 'J')) {
    fprintf(stderr, "Invalid lxobj: malformed header.\n");
    return false;
  }
  // TODO: we could check all the (code) sections size at once

  // XXX: we are hanlding only the first code section for now

  uint8_t* code = (uint8_t*)bytes;

  size_t obj_size = 0;
  obj_size += code[6];
  obj_size += code[7] << 8;
  obj_size += code[8] << 16;
  obj_size += code[9] << 24;

  size_t code_size = 0;
  code_size += code[32];
  code_size += code[33] << 8;
  code_size += code[34] << 16;
  code_size += code[35] << 24;

  // XXX: naive size check, only works for 1 code chunk/section
  if (code_size > obj_size - 36) {
    fprintf(stderr, "Code size larger than obj size.\n");
    return false;
  }

  for (int i = 0; i < code_size; i++) {
    writeChunk(chunk, code[36 + i], 1);
  }

  return true;
}
