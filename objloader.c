#include <stdio.h>
#include <string.h>

#include "common.h"
#include "debug.h"
#include "objloader.h"
#include "object.h"

ValueArray functions;
ValuePointers valuePointers;

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
//    FUNCTION_ARITY: 1
//   CHUNK_NAME_SIZE: 2 little endian
//        CHUNK_NAME: string, vary length, aka function name
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
//          STRING:   4 little endian size + actual string
//          FUNCTION: 0, we rely on the build order to stay the same
// DEBUG_SECTION:
//      SIZE: 4 little endian
//      FILEPATH_LENGTH: 2 file path length
//      FILEPATH: vary length
//      TOKEN_LINE_NUMBER: 2 bytes each (which means we would only support line no. up to 65535)


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

    // chunk_size + function_arity = 4 + 1 = 5
    size_t chunk_size_sofar = 5;
    ptr += 5;

    if (total_size + 4 + chunk_size > obj_size) {
      // ensure this chunk won't exceed total object size
      fprintf(stderr, "Invalid lxobj: chunk %d size is too big(%ld).\n", i, chunk_size);
      return false;
    }

    // chunk name
    size_t chunkNameLength = getShortSize(ptr);
    ptr += 2 + chunkNameLength;
    chunk_size_sofar += 2 + chunkNameLength;
    if (chunk_size_sofar > chunk_size) {
      fprintf(stderr, "Invalid lxobj: chunk %d name length is too big(%ld).\n", i, chunkNameLength);
      return false;
    }

    size_t code_size = getSize(ptr);
    chunk_size_sofar += code_size;
    ptr += 4 + code_size;
    if (chunk_size_sofar > chunk_size) {
      fprintf(stderr, "Invalid lxobj: chunk %d code size is too big(%ld).\n", i, code_size);
      return false;
    }

    size_t constant_size = getSize(ptr);
    chunk_size_sofar += 4 + constant_size;
    ptr += 4 + constant_size;
    if (chunk_size_sofar > chunk_size) {
      fprintf(stderr, "Invalid lxobj: chunk %d constants size is too big(%ld).\n", i, constant_size);
      return false;
    }

    if (debug) {
      size_t debug_section_size = getSize(ptr);
      chunk_size_sofar += 4 + debug_section_size;
      ptr += 4 + debug_section_size;
      if (chunk_size_sofar > chunk_size) {
        fprintf(stderr, "Invalid lxobj: chunk %d debug section size is too big(%ld).\n", i, debug_section_size);
        return false;
      }
    }

    if (chunk_size != chunk_size_sofar) {
      fprintf(stderr, "Invalid lxobj: chunk %d size mismatch.\n", i);
      return false;
    }

    // all good, set chunk_start to next chunk
    chunk_start += 4 + chunk_size;
    total_size += 4 + chunk_size;
  }

  return true;
}

ObjFunction* loadFunction(uint8_t* bytes, uint8_t flags) {
  bool debug = (flags & 0b00000001) > 0;

  ObjFunction* func = newFunction();
  Chunk* chunk = &func->chunk;

  uint8_t* code_start = &bytes[4];
  func->arity = code_start[0];
  code_start += 1;

  // read function name
  size_t funcNameLength = getShortSize(code_start);

  code_start += 2;
  if (funcNameLength > 0) {
    func->name = copyString((char*)code_start, funcNameLength);
    code_start += funcNameLength;
  }

  size_t code_size = getSize(code_start);
  code_start += 4;

  if (!debug) {
    for (int i = 0; i < code_size; i++) {
      writeChunk(chunk, code_start[i], 1);
    }
  } else {
    // to gather debug line info, we must fastforward
    // to debug line section first, so we can write chunk with line info
    uint8_t* ptr = &code_start[code_size];
    size_t constSectionSize = getSize(ptr);

    // const section size (4) + actual consts
    ptr += 4 + constSectionSize;
    // debug lines size (4)
    size_t debugLinesSize = getSize(ptr);
    ptr += 4;

    uint16_t filenameSize = getShortSize(ptr);
    ptr += 2 + filenameSize;
    // ptr is now at the start of line numbers!!!

    for (int i = 0; i < code_size; i++) {
      uint16_t line = getShortSize(&ptr[i * 2]);
      writeChunk(chunk, code_start[i], line);
    }
  }

  uint8_t* constSection = &code_start[code_size];
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

      case VAL_OBJ: {
        ObjType objType = constSection[1];
        switch (objType) {
          case OBJ_FUNCTION: {
            // add nil constant, & save the value pointer to an array
            // after all chunks(functions) are loaded and saved to global functions var,
            // we will iterate through value pointer,
            // and do valuePointer[i] = OBJ_VAL(functions[i])
            int index = addConstant(chunk, NIL_VAL);
            Value* valuePointer = &chunk->constants.values[index];
            writeValuePointers(&valuePointers, valuePointer);
            // value type + obj type
            constSection += (1 + 1);
            break;
          }
          case OBJ_STRING: {
            size_t ssize = getSize(&constSection[2]);
            addConstant(chunk,
                OBJ_VAL(copyString((char*)&constSection[6], ssize)));
            // value type + obj type + 4(ssize) + actual string
            constSection += (1 + 1 + 4 + ssize);
            break;
          }
          default:
            fprintf(stderr, "Invalid object type %x\n", objType);
            return NULL;
        }
        break;
      }

      default:
        fprintf(stderr, "Invalid value type %x\n", type);
        return NULL;
    }
  }
  return func;
}

ObjFunction* loadObj(uint8_t* bytes) {
  if (!objIsValid(bytes)) {
    return NULL;
  }
  ObjFunction* main = NULL;
  initValueArray(&functions);
  initValuePointers(&valuePointers);

  uint8_t flags = bytes[3];
  bool debug = (flags & 0b00000001) > 0;

  uint16_t chunks_count = getShortSize(&bytes[8]);
  uint8_t* chunk_start = &bytes[16];

  for (int i = 0; i < chunks_count; i++) {
    size_t chunk_size = getSize(chunk_start);
    ObjFunction* func = loadFunction(chunk_start, flags);
    if (func == NULL) return NULL;
    if (i == 0) main = func;
    writeValueArray(&functions, OBJ_VAL(func));
    chunk_start += 4 + chunk_size;
  }

  if (valuePointers.count != chunks_count - 1) {
    // minus one, cuz first chunk is main
    fprintf(stderr, "Invalid lxobj: functions(%d)/chunks(%d) count mismatch.\n",
        valuePointers.count, chunks_count);
    return NULL;
  }

  for (int i = 0; i < valuePointers.count; i++) {
    // functions[0] is main, thus i + 1
    ObjFunction* func = AS_FUNCTION(functions.values[i + 1]);
    *valuePointers.values[i] = OBJ_VAL(func);
  }

#ifdef DEBUG_PRINT_CODE
  disassembleChunk(&main->chunk, main->name != NULL
      ? main->name->chars : "<script>");
#endif
  freeValueArray(&functions);
  freeValuePointers(&valuePointers);
  return main;
}
