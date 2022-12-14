#ifndef clox_debug_h
#define clox_debug_h

#include "chunk.h"

void disassembleChunk(Chunk* chunk, const char* filename, const char* name, bool printCode);
int disassembleInstruction(Chunk* chunk, int offset, bool printCode);

#endif
