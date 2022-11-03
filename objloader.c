#include <stdio.h>
#include <string.h>

#include "common.h"
#include "objloader.h"
#include "object.h"
#include "chunk.h"

// obj layout
// LX:        2
// VERSION:   1
// FLAGS:     1 , 8 bits, TBD, could indicate if this is a debug release or not?
// OBJSIZE:   4 little endian
// TBD:       16 - (2+1+1+4) = 8
// CODE_SECTION: ?
//      SIZE: 4 little endian
//      CODE: various length
//            CODE_SECTION guaranteed to be followed by 5 bytes of CONST_SECTION header
// CONST_SECTION: follow right after a CODE_SECTION
//      CONST_COUNT: 1
//      SIZE: 4  let's leave size here, so it's possible for us to jump to next chunk
//   every const is like
//      TYPE:  1
//      VALUE: various length

double readDouble(const uint8_t* bytes) {
  double n = 0;
  memcpy(&n, bytes, sizeof(double));
  return n;
}

size_t getSize(const uint8_t* bytes) {
  size_t size = 0;
  size += bytes[0];
  size += bytes[1] << 8;
  size += bytes[2] << 16;
  size += bytes[3] << 24;
  return size;
}

bool loadObj(uint8_t* bytes, Chunk* chunk) {
  if (!(bytes[0] == 'L' && bytes[1] == 'X')) {
    fprintf(stderr, "Invalid lxobj: malformed header.\n");
    return false;
  }
  // TODO: we could check all the (code) sections size at once

  // XXX: we are hanlding only the first code section for now

  size_t obj_size = getSize(&bytes[4]);
  size_t code_size = getSize(&bytes[16]);

  // XXX: naive size check, only works for 1 code chunk/section
  if (code_size > obj_size - (16+4+5)) {
    fprintf(stderr, "Code size larger than obj size.\n");
    return false;
  }

  for (int i = 0; i < code_size; i++) {
    // XXX: figure out how we can fit debug line info
    // in the lxobj layout
    writeChunk(chunk, bytes[20 + i], 1);
  }

  uint8_t* constSection = &bytes[16 + 4 + code_size];
  uint8_t constsCount = constSection[0];

  // skip reading consts total + total consts bytes size
  constSection += (1 + 4);
  for (int i = 0; i < constsCount; i++) {
    // we are going to ^ read this many constants
    uint8_t type = constSection[0];

    switch (type) {
      case VAL_BOOL:
        // probabaly don't need this
        // since bool is encoded in bytecode already
        addConstant(chunk, BOOL_VAL(constSection[1]));
        constSection += (1 + 1);
        break;

      case VAL_NIL: 
        // probabaly don't need this
        // since nil is encoded in bytecode already
        addConstant(chunk, NIL_VAL);
        constSection += 1;
        break;

      case VAL_NUMBER:
        addConstant(chunk, NUMBER_VAL(readDouble(&constSection[1])));
        constSection += (1 + 8); // type + double 8 bytes
        break;

      case VAL_OBJ:
        {
          ObjType objType = constSection[1];
          switch (objType) {
            case OBJ_STRING:
              {
                size_t ssize = getSize(&constSection[2]);
                addConstant(chunk,
                    OBJ_VAL(copyString((char*)&constSection[6], ssize)));
                // value type + obj type + 4(ssize) + actual string
                constSection += (1 + 1 + 4 + ssize);
              }
              break;
            default:
              fprintf(stderr, "Invalid object type %x\n", objType);
              return false;
          }
        }
        break;

      default:
        fprintf(stderr, "Invalid value type %x\n", type);
        return false;
    }

  }

  return true;
}
