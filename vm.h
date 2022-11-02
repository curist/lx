#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
  Chunk* chunk;
  uint8_t* ip;
  Value stack[STACK_MAX];
  Value* stackTop;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

#define INTERPRET_LOADOBJ_ERROR INTERPRET_COMPILE_ERROR

void initVM();
void freeVM();
InterpretResult interpret(uint8_t* obj);
void push(Value value);
Value pop();

#endif
