#include <stdio.h>
#include <string.h>

#include "common.h"
#include "objloader.h"
#include "chunk.h"


// obj layout
// LX:        2
// VERSION:   1
// OBJSIZE:   4 little endian
// TBD:       16 - (2+1+4) = 9
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
  if (!(bytes[0] == 'L' && bytes[1] == 'X')) {
    fprintf(stderr, "Invalid lxobj: malformed header.\n");
    return false;
  }
  // TODO: we could check all the (code) sections size at once

  // XXX: we are hanlding only the first code section for now

  uint8_t* code = (uint8_t*)bytes;

  size_t obj_size = 0;
  obj_size += code[3];
  obj_size += code[4] << 8;
  obj_size += code[5] << 16;
  obj_size += code[6] << 24;

  size_t code_size = 0;
  code_size += code[17];
  code_size += code[18] << 8;
  code_size += code[19] << 16;
  code_size += code[20] << 24;

  // XXX: naive size check, only works for 1 code chunk/section
  if (code_size > obj_size - 21) {
    fprintf(stderr, "Code size larger than obj size.\n");
    return false;
  }

  for (int i = 0; i < code_size; i++) {
    // XXX: figure out how we can fit debug line info
    // in the lxobj layout
    writeChunk(chunk, code[21 + i], 1);
  }

  uint8_t* constSection = &code[16 + 1 + 4 + code_size + 4];
  for (int i = 0; i < code[16]; i++) {
    // we are going to ^ read this many constants
    uint8_t type = constSection[0];

    switch (type) {
      case VAL_BOOL: {
        addConstant(chunk, BOOL_VAL(constSection[1]));
        constSection += (1 + 1);
        break;
      }
      case VAL_NIL: {
        addConstant(chunk, NIL_VAL);
        constSection += 1;
        break;
      }
      case VAL_NUMBER: {
        double value = readDouble(&constSection[1]);
        constSection += (1 + 8); // type + double 8 bytes
        addConstant(chunk, NUMBER_VAL(value));
        break;
      }
      default: {
        fprintf(stderr, "Invalid value type %x\n", type);
        return false;
      }
    }

  }

  return true;
}
