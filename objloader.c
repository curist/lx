#include <stdio.h>
#include <string.h>

#include "common.h"
#include "objloader.h"
#include "chunk.h"


// obj layout
// LXOBJ:     5
// VERSION:   1
// OBJSIZE:   4 little endian
// TBD:       32 - 10 = 22
// CODE_SECTION: ?
//      CONST_COUNT: 1
//      SIZE: 4 little endian
// CONST_SECTION: follow right after a CODE_SECTION (XXX: or should we do some padding?)
//      SIZE: 4
//   every const is like
//      TYPE:  1
//      VALUE: various length
//      currently we only have TYPE 00, as double, and it takes 8 bytes
// constants section should be defined at TBD: later


double readDouble(const uint8_t* bytes) {
  double n = 0;
  memcpy(&n, bytes, sizeof(double));

  return n;
}

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
  code_size += code[33];
  code_size += code[34] << 8;
  code_size += code[35] << 16;
  code_size += code[36] << 24;

  // XXX: naive size check, only works for 1 code chunk/section
  if (code_size > obj_size - 37) {
    fprintf(stderr, "Code size larger than obj size.\n");
    return false;
  }

  for (int i = 0; i < code_size; i++) {
    writeChunk(chunk, code[37 + i], 1);
  }

  uint8_t* constSection = &code[37 + code_size];
  for (int i = 0; i < code[32]; i++) {
    // we are going to ^ read this many constants
    uint8_t type = constSection[0];
    // XXX  ^ should be 0 atm, which means double
    double value = readDouble(&constSection[1]);
    constSection += (1 + 8); // type + double 8 bytes
    addConstant(chunk, value);
  }

  return true;
}
