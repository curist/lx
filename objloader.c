#include <stdio.h>
#include <string.h>

#include "common.h"
#include "debug.h"
#include "objloader.h"
#include "object.h"

ObjFunction* currentFunction = NULL;

static Chunk* currentChunk() {
  return &currentFunction->chunk;
}

// obj layout
// LX:        2
// VERSION:   1
// FLAGS:     1 , 8 bits
//     0000 0001 -> debug
// OBJSIZE:   4 little endian
// CHUNKS:    2 little endian -> we can have up to 65536 chunks
// TBD:       16 - (2+1+1+4+2) = 6
// # chunk layout
// CHUNK_SIZE: 4 little endian
// CODE_SECTION:
//      SIZE: 4 little endian
//      CODE: various length
//            CODE_SECTION guaranteed to be followed by 5 bytes of CONST_SECTION header
// CONST_SECTION: follow right after a CODE_SECTION
//      SIZE: 4  let's leave size here, so it's possible for us to jump to next chunk
//      CONST_COUNT: 1
//   every const is like
//      TYPE:  1
//      VALUE: 1 bit type + type dependent layout(length)
//        BOOL:   1 + 1
//        NIL:    1
//        NUMBER: 1 + 8 (double)
//        OBJ:    1 + 1 (obj type) + obj type dependent layout(length)
//          STRING: 4 little endian size + actual string
// DEBUG_SECTION:
//      SIZE: 4 little endian
//      FILEPATH_LENGTH: 2 file path length
//      FILEPATH: vary length
//      TOKEN_LINE_NUMBER: 2 bytes each (which means we would only support line no. up to 65535)

// NOTE: we should use TBD to store a chunks jump table address
// and chunks jump table will have layout like
// CHUNKS: 2 little endian
//   ADDR(s): 4 little endian, each

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

uint16_t getShortSize(const uint8_t* bytes) {
  size_t size = 0;
  size += bytes[0];
  size += bytes[1] << 8;
  return size;
}

bool objIsValid(uint8_t* bytes) {
  // we verify all sections size in one go
  // total bytes size should equals objSize read in bytes,
  // which should be checked upfront
  if (!(bytes[0] == 'L' && bytes[1] == 'X')) {
    fprintf(stderr, "Invalid lxobj: malformed header.\n");
    return false;
  }
  uint8_t flags = bytes[3];
  bool debug = (flags & 0b00000001) > 0;

  size_t total_size = 16; // header size
  size_t obj_size = getSize(&bytes[4]);
  if (obj_size < 20) { // header size(16) + first chunk length size(4)
    fprintf(stderr, "Invalid lxobj: bad obj size.\n");
    return false;
  }
  // we have this many of chunks
  uint16_t chunks_count = getShortSize(&bytes[8]);

  if (chunks_count < 1) {
    fprintf(stderr, "Invalid lxobj: should at least have 1 chunk.\n");
    return false;
  }

  uint8_t* chunk_start = &bytes[16];

  for (int i = 0; i < chunks_count; i++) {
    // this chunk is this big
    uint8_t* ptr = chunk_start;
    size_t chunk_size = getSize(ptr);

    size_t chunk_size_sofar = 4;
    ptr += 4;

    if (total_size + 4 + chunk_size > obj_size) {
      fprintf(stderr, "Invalid lxobj: chunk %d size is too big.\n", i);
      return false;
    }

    size_t code_size = getSize(ptr);
    chunk_size_sofar += code_size;
    ptr += 4 + code_size;
    if (chunk_size_sofar > chunk_size) {
      fprintf(stderr, "Invalid lxobj: chunk %d code size is too big.\n", i);
      return false;
    }

    size_t constant_size = getSize(ptr);
    chunk_size_sofar += 4 + constant_size;
    ptr += constant_size;
    if (chunk_size_sofar > chunk_size) {
      fprintf(stderr, "Invalid lxobj: chunk %d constants size is too big.\n", i);
      return false;
    }

    if (debug) {
      size_t debug_section_size = getSize(ptr);
      chunk_size_sofar += 4 + debug_section_size;
      ptr += debug_section_size;
      if (chunk_size_sofar > chunk_size) {
        fprintf(stderr, "Invalid lxobj: chunk %d debug section size is too big.\n", i);
        return false;
      }
    }

    if (chunk_size != chunk_size_sofar) {
      fprintf(stderr, "Invalid lxobj: chunk %d size mismatch.\n", i);
    }

    // all good, set chunk_start to next chunk
    chunk_start += chunk_size;
    total_size += chunk_size;
  }

  return true;
}

bool loadChunk(uint8_t* bytes, uint8_t flags) {
  bool debug = (flags & 0b00000001) > 0;

  size_t code_size = getSize(&bytes[4]);

  if (!debug) {
    for (int i = 0; i < code_size; i++) {
      writeChunk(currentChunk(), bytes[4 + 4 + i], 1);
    }
  } else {
    // to gather debug line info, we must fastforward
    // to debug line section first, so we can write chunk with line info
    uint8_t* ptr = &bytes[4 + 4 + code_size];
    size_t constSectionSize = getSize(ptr);

    // const section size (4) + const count (1) + actual consts
    ptr += 4 + 1 + constSectionSize;
    // debug lines size (4)
    ptr += 4;

    uint16_t filenameSize = getShortSize(ptr);
    ptr += 2 + filenameSize;
    // ptr is now at the start of line numbers!!!

    for (int i = 0; i < code_size; i++) {
      uint16_t line = getShortSize(&ptr[i * 2]);
      writeChunk(currentChunk(), bytes[4 + 4 + i], line);
    }
  }

  uint8_t* constSection = &bytes[4 + 4 + code_size];
  uint8_t constsCount = constSection[4];

  // skip reading consts total + total consts bytes size
  constSection += (1 + 4);
  for (int i = 0; i < constsCount; i++) {
    // we are going to ^ read this many constants
    uint8_t type = constSection[0];

    switch (type) {
      case VAL_BOOL:
        // probabaly don't need this
        // since bool is encoded in bytecode already
        addConstant(currentChunk(), BOOL_VAL(constSection[1]));
        constSection += (1 + 1);
        break;

      case VAL_NIL:
        // probabaly don't need this
        // since nil is encoded in bytecode already
        addConstant(currentChunk(), NIL_VAL);
        constSection += 1;
        break;

      case VAL_NUMBER:
        addConstant(currentChunk(), NUMBER_VAL(readDouble(&constSection[1])));
        constSection += (1 + 8); // type + double 8 bytes
        break;

      case VAL_OBJ: {
        ObjType objType = constSection[1];
        switch (objType) {
          case OBJ_STRING: {
            size_t ssize = getSize(&constSection[2]);
            addConstant(currentChunk(),
                OBJ_VAL(copyString((char*)&constSection[6], ssize)));
            // value type + obj type + 4(ssize) + actual string
            constSection += (1 + 1 + 4 + ssize);
            break;
          }
          // TODO: OBJ_FUNCTION
          // store addConstant index
          // then scan chunk and compose function
          // then set currentfunction.chunk.constants[index]
          // NOTE: function chunks loading should be recursive
          // chunks could contains other function chunk
          // but if we load chunks using the same order we write chunks,
          // we might have less trouble?
          // NOTE: use clox ARRAY to store function.chunk.constants pointer
          // and its correspond chunk number
          // then when we finished loading chunks (into another clox ARRAY)
          // we can go through first array, and replace the function value in place
          default:
            fprintf(stderr, "Invalid object type %x\n", objType);
            return false;
        }
        break;
      }

      default:
        fprintf(stderr, "Invalid value type %x\n", type);
        return false;
    }
  }
  return true;
}

ObjFunction* loadObj(uint8_t* bytes) {
  if (!objIsValid(bytes)) {
    return NULL;
  }

  // XXX: we are hanlding only the first code section for now
  currentFunction = newFunction();

  // ObjFunction* func = newFunction();
  // ObjFunction* enclosing = currentFunction;
  // currentFunction = func;
  // currentFunction = enclosing;

  uint8_t flags = bytes[3];
  bool debug = (flags & 0b00000001) > 0;

  uint16_t chunks_count = getShortSize(&bytes[8]);
  uint8_t* chunk_start = &bytes[16];
  printf("chunks count: %d\n", chunks_count);

  for (int i = 0; i < chunks_count; i++) {
    size_t chunk_size = getSize(chunk_start);
    if (!loadChunk(chunk_start, flags)) return NULL;
    chunk_start += chunk_size;
  }

#ifdef DEBUG_PRINT_CODE
  disassembleChunk(currentChunk(), currentFunction->name != NULL
      ? currentFunction->name->chars : "<script>");
#endif

  return currentFunction;
}
