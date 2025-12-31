#include <stdio.h>
#include <string.h>
#include <math.h>

#include "debug.h"
#include "objloader.h"
#include "object.h"

// functions contains actual function, or a index ref (-> functions[index])
ValueArray functions;
ChunkIndexes chunkIndexes;

// obj layout
// LX:        2
// VERSION:   1
// FLAGS:     1 , 8 bits
//     0000 0001 -> debug
// OBJSIZE:   4 little endian
// CHUNKS:    4 little endian
// MAIN_ID:   4 little endian
// TBD:       32 - (2+1+1+4+4+4) = 16
// # chunk layout
// CHUNK_SIZE: 4 little endian
//    CHUNK_TYPE: 1: 0 as REF, 1 as actual
//    CHUNK_ID: 4 little endian
//       REF_TARGET: 4, little endian, function id
//    FUNCTION_ARITY: 1
//     UPVALUE_COUNT: 1
//   CHUNK_NAME_SIZE: 2 little endian
//        CHUNK_NAME: string, vary length, aka function name
// CODE_SECTION: same as v0
// CONST_SECTION: same as v0
//   OBJ: FUNCTION payload is 4-byte function id
// DEBUG_SECTION: same as v0

double readDouble(const uint8_t* bytes) {
  double n = 0;
  memcpy(&n, bytes, sizeof(double));
  return n;
}

static Value numberToValueCanonical(double num) {
#ifdef NAN_BOXING
  if (isfinite(num) && num >= (double)INT64_MIN && num <= (double)INT64_MAX) {
    int64_t i = (int64_t)num;
    if ((double)i == num && fixnumFitsInt64(i)) {
      return FIXNUM_VAL(i);
    }
  }
#endif
  return NUMBER_VAL(num);
}

size_t getSize(const uint8_t* bytes) {
  size_t size = 0;
  size += bytes[0];
  size += bytes[1] << 8;
  size += bytes[2] << 16;
  size += bytes[3] << 24;
  return size;
}

size_t getShortSize(const uint8_t* bytes) {
  size_t size = 0;
  size += bytes[0];
  size += bytes[1] << 8;
  return size;
}

static void writeValueArrayAt(ValueArray* array, int index, Value value) {
  if (array->capacity < index + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(index + 1);
    array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
  }
  if (array->count <= index) {
    for (int i = array->count; i <= index; i++) {
      array->values[i] = NIL_VAL;
    }
    array->count = index + 1;
  }
  array->values[index] = value;
}

bool objIsValid(uint8_t* bytes) {
  // we verify all sections size in one go
  // total bytes size should equals objSize read in bytes,
  // which should be checked upfront
  if (!(bytes[0] == 'L' && bytes[1] == 'X')) {
    fprintf(stderr, "Invalid lxobj: malformed header.\n");
    return false;
  }
  uint8_t version = bytes[2];
  if (version != 1) {
    fprintf(stderr, "Invalid lxobj: unsupported version %u.\n", version);
    return false;
  }
  uint8_t flags = bytes[3];
  bool debug = (flags & 0b00000001) > 0;

  size_t total_size = 16; // header size
  size_t obj_size = getSize(&bytes[4]);
  if (obj_size < 36) { // header size(32) + first chunk length size(4)
    fprintf(stderr, "Invalid lxobj: bad obj size.\n");
    return false;
  }
  // we have this many of chunks
  size_t chunks_count = getSize(&bytes[8]);

  if (chunks_count < 1) {
    fprintf(stderr, "Invalid lxobj: should at least have 1 chunk.\n");
    return false;
  }

  uint8_t* chunk_start = &bytes[32];

  for (size_t i = 0; i < chunks_count; i++) {
    // this chunk is this big
    uint8_t* ptr = chunk_start;
    size_t chunk_size = getSize(ptr);
    size_t chunk_size_sofar = 0;
    ptr += 4; // chunk size
    chunk_size_sofar += 4;

    // check chunk type
    uint8_t chunkType = ptr[0];
    ptr += 1;
    chunk_size_sofar += 1;

    ptr += 4;
    chunk_size_sofar += 4;

    if (chunkType == CHUNK_TYPE_REF) {
      // set chunk_start to next chunk
      chunk_start += 4 + chunk_size;
      total_size += 4 + chunk_size;
      continue;
    }

    // function_arity + upvalue count = 1 + 1= 2
    ptr += 2;
    chunk_size_sofar += 2;

    if (total_size + 4 + chunk_size > obj_size) {
      // ensure this chunk won't exceed total object size
      fprintf(stderr, "Invalid lxobj: chunk %zu size is too big(%zu).\n", i, chunk_size);
      return false;
    }

    // chunk name
    size_t chunkNameLength = getShortSize(ptr);
    ptr += 2 + chunkNameLength;
    chunk_size_sofar += 2 + chunkNameLength;
    if (chunk_size_sofar > chunk_size) {
      fprintf(stderr, "Invalid lxobj: chunk %zu name length is too big(%zu).\n", i, chunkNameLength);
      return false;
    }

    size_t code_size = getSize(ptr);
    chunk_size_sofar += code_size;
    ptr += 4 + code_size;
    if (chunk_size_sofar > chunk_size) {
      fprintf(stderr, "Invalid lxobj: chunk %zu code size is too big(%zu).\n", i, code_size);
      return false;
    }

    size_t constant_size = getSize(ptr);
    chunk_size_sofar += 4 + constant_size;
    ptr += 4 + constant_size;
    if (chunk_size_sofar > chunk_size) {
      fprintf(stderr, "Invalid lxobj: chunk %zu constants size is too big(%zu).\n", i, constant_size);
      return false;
    }

    if (debug) {
      size_t debug_section_size = getSize(ptr);
      chunk_size_sofar += 4 + debug_section_size;
      ptr += 4 + debug_section_size;
      if (chunk_size_sofar > chunk_size) {
        fprintf(stderr, "Invalid lxobj: chunk %zu debug section size is too big(%zu).\n", i, debug_section_size);
        return false;
      }
    }

    if (chunk_size != chunk_size_sofar) {
      fprintf(stderr, "Invalid lxobj: chunk %zu size mismatch.\n", i);
      return false;
    }

    // all good, set chunk_start to next chunk
    chunk_start += 4 + chunk_size;
    total_size += 4 + chunk_size;
  }

  return true;
}

void initChunkIndexes(ChunkIndexes* array) {
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}

void writeChunkIndexes(ChunkIndexes* array, ChunkValueIndex value) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values = GROW_ARRAY(ChunkValueIndex, array->values,
                               oldCapacity, array->capacity);
  }
  array->values[array->count] = value;
  array->count++;
}

void freeChunkIndexes(ChunkIndexes* array) {
  FREE_ARRAY(ChunkValueIndex, array->values, array->capacity);
  initChunkIndexes(array);
}

ObjFunction* loadFunction(uint8_t* bytes, uint8_t flags) {
  bool debug = (flags & 0b00000001) > 0;

  ObjFunction* func = newFunction();
  push(OBJ_VAL(func));
  Chunk* chunk = &func->chunk;

  uint8_t* code_start = bytes;

  func->arity = code_start[0];
  func->upvalueCount = code_start[1];
  code_start += 2;

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
    for (size_t i = 0; i < code_size; i++) {
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
    // size_t debugLinesSize = getSize(ptr);
    ptr += 4;

    size_t filenameSize = getShortSize(ptr);
    ptr += 2;
    func->filename = copyString((char*)ptr, filenameSize);
    ptr += filenameSize;
    // ptr is now at the start of line numbers!!!

    uint8_t repeatTimes = ptr[0];
    uint8_t repeated = 0;
    ptr++;
    size_t line = getShortSize(ptr);

    for (size_t i = 0; i < code_size; i++) {
      if (++repeated > repeatTimes) {
        ptr += 2;
        repeatTimes = ptr[0];
        repeated = 1;
        ptr++;
        line = getShortSize(ptr);
      }
      writeChunk(chunk, code_start[i], line);
    }
  }

  uint8_t* constSection = &code_start[code_size];
  uint8_t constsCount = constSection[4];

  // skip reading consts total + total consts bytes size
  constSection += (1 + 4);
  for (size_t i = 0; i < constsCount; i++) {
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
        addConstant(chunk, numberToValueCanonical(readDouble(&constSection[1])));
        constSection += (1 + 8); // type + double 8 bytes
        break;

      case VAL_OBJ: {
        ObjType objType = constSection[1];
        switch (objType) {
          case OBJ_FUNCTION: {
            int functionId = (int)getSize(&constSection[2]);
            // add nil constant, & save the value pointer to an array
            // after all chunks(functions) are loaded and saved to global functions var,
            // we will iterate through value pointer,
            // and do valuePointer[i] = OBJ_VAL(functions[i])
            int index = addConstant(chunk, NIL_VAL);
            writeChunkIndexes(&chunkIndexes, (ChunkValueIndex){chunk, index, functionId});
            // value type + obj type + function id
            constSection += (1 + 1 + 4);
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

ObjFunction* loadObj(uint8_t* bytes, bool printCode) {
  if (!objIsValid(bytes)) {
    return NULL;
  }
  ObjFunction* main = NULL;
  initValueArray(&functions);
  initChunkIndexes(&chunkIndexes);

  uint8_t flags = bytes[3];
  int mainId = (int)getSize(&bytes[12]);

  size_t chunks_count = getSize(&bytes[8]);
  uint8_t* chunk_start = &bytes[32];

  uint8_t shared_module_count = 0;

  for (size_t i = 0; i < chunks_count; i++) {
    size_t chunk_size = getSize(chunk_start);
    uint8_t* ptr = &chunk_start[4];
    ChunkType chunkType = ptr[0];
    ptr += 1;

    int chunkId = (int)getSize(ptr);
    ptr += 4;

    if (chunkType == CHUNK_TYPE_REF) {
      int targetId = (int)getSize(ptr);
      writeValueArrayAt(&functions, chunkId, NUMBER_VAL(targetId));
      shared_module_count++;
    } else {
      ObjFunction* func = loadFunction(ptr, flags);
      if (func == NULL) return NULL;
      if (chunkId == mainId) main = func;
      writeValueArrayAt(&functions, chunkId, OBJ_VAL(func));
    }
    chunk_start += 4 + chunk_size;
  }

  for (size_t i = 0; i < chunkIndexes.count; i++) {
    ChunkValueIndex chunkIndex = chunkIndexes.values[i];
    int functionId = chunkIndex.functionId;
    if (functionId >= functions.count) {
      fprintf(stderr, "Invalid lxobj: function index out of bounds.\n");
      return NULL;
    }

    Value funcValue = functions.values[functionId];
    if (IS_NUMBER(funcValue)) {
      int index = AS_NUMBER(funcValue);
      funcValue = functions.values[index];
    }
    chunkIndex.chunk->constants.values[chunkIndex.index] = funcValue;
  }

  for (size_t i = 0; i < chunks_count - shared_module_count; i++) pop();

  if (printCode) {
    for (int i = 0; i < functions.count; i++) {
      if (IS_NUMBER(functions.values[i])) {
        int index = AS_NUMBER(functions.values[i]);
        ObjFunction* func = AS_FUNCTION(functions.values[index]);
        printf("[%d] %s -> [%d] \n\n",
            i, func->filename != NULL ? func->filename->chars : "[unknown]", index);
        continue;
      }

      ObjFunction* func = AS_FUNCTION(functions.values[i]);

      printf("[%d] ", i);
      disassembleChunk(&func->chunk,
          func->filename != NULL ? func->filename->chars : "[unknown]",
          func->name != NULL ? func->name->chars : "[script]", true);
    }
  }

  freeValueArray(&functions);
  freeChunkIndexes(&chunkIndexes);
  return main;
}
