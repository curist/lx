#include <stdio.h>

#include "common.h"
#include "objloader.h"
#include "debug.h"
#include "vm.h"

VM vm;

static void resetStack() {
  vm.stackTop = vm.stack;
}

void initVM() {
  resetStack();
}

void freeVM() {
}

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define BINARY_OP(op) \
    do { \
      double b = pop(); \
      *(vm.stackTop - 1) = *(vm.stackTop - 1) op b; \
    } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          [ ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printValue(*slot);
      printf(" ");
    }
    printf("]\n");
    disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_CONST_BYTE: {
        Value constant = READ_BYTE();
        push(constant);
        break;
      }
      case OP_ADD:      BINARY_OP(+); break;
      case OP_SUBTRACT: BINARY_OP(-); break;
      case OP_MULTIPLY: BINARY_OP(*); break;
      case OP_DIVIDE:   BINARY_OP(/); break;
      case OP_MOD: {
        // % only applies to int :(
        int b = pop();
        int a = pop();
        push(a % b);
        break;
      }
      case OP_NEGATE: {
        *(vm.stackTop - 1) = -*(vm.stackTop - 1);
        break;
      }
      case OP_RETURN: {
        printValue(pop());
        printf("\n");
        return INTERPRET_OK;
      }
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(uint8_t* obj) {
  Chunk chunk;
  initChunk(&chunk);

  if (!loadObj(obj, &chunk)) {
    freeChunk(&chunk);
    return INTERPRET_LOADOBJ_ERROR;
  }

  vm.chunk = &chunk;
  vm.ip = vm.chunk->code;

  InterpretResult result = run();

  freeChunk(&chunk);
  return result;
}
