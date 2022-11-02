#ifndef clox_objloader_h
#define clox_objloader_h
#include "vm.h"

bool loadObj(const uint8_t* bytes, Chunk* chunk);

#endif
