#ifndef clox_vm_h
#define clox_vm_h

#include <setjmp.h>

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 1024
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct {
  // Fiber context
  ObjFiber* currentFiber;

  // Execution registers (pointers to either fiber or direct-mode storage)
  Value* stack;
  Value* stackTop;
  int stackCapacity;

  CallFrame* frames;
  int frameCount;
  int frameCapacity;

  ObjUpvalue* openUpvalues;

  Value lastError;
  jmp_buf* errorJmp;
  int nonYieldableDepth;

  // VM-global result (not per-fiber)
  Value lastResult;

  // Direct-mode backing storage (Phase 2A only)
  Value mainStack[STACK_MAX];
  CallFrame mainFrames[FRAMES_MAX];

  // Global tables
  Table globals;
  Table strings;

  // GC state
  size_t bytesAllocated;
  size_t nextGC;
  Obj* objects;
  int grayCount;
  int grayCapacity;
  Obj** grayStack;
  bool gcRunning;

#ifdef PROFILE_OPCODES
  uint64_t opCounts[256];
#endif
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

#define INTERPRET_LOADOBJ_ERROR INTERPRET_COMPILE_ERROR

void initVM();
void freeVM();
InterpretResult interpret(uint8_t* obj);

// Inline stack operations for performance
static inline void push(Value value) {
  *vm.stackTop++ = value;
}

static inline Value pop(void) {
  return *--vm.stackTop;
}

static inline Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}

#endif
