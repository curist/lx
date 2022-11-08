#ifndef clox_objloader_h
#define clox_objloader_h
#include "vm.h"
#include "object.h"

ObjFunction* loadObj(uint8_t* bytes);

#endif
