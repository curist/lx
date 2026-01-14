#ifndef clox_vm_h
#define clox_vm_h

#include <stdio.h>
#include <setjmp.h>

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 1024
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

// Per-fiber error handler stack for nested error boundaries (pcall, fiber.resume)
typedef struct ErrorHandler {
  jmp_buf buf;                    // longjmp target for error unwinding
  struct ErrorHandler* prev;      // previous handler in stack (NULL at bottom)
} ErrorHandler;

typedef struct CallFrame {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct {
  // Fiber context
  ObjFiber* currentFiber;
  ObjFiber* mainFiber;  // Main execution fiber (root of caller chain)

  // Main fiber direct-mode storage (avoids heap allocation)
  Value mainStack[STACK_MAX];
  CallFrame mainFrames[FRAMES_MAX];

  // Execution registers (pointers to either fiber or direct-mode storage)
  Value* stack;
  Value* stackTop;
  int stackCapacity;

  CallFrame* frames;
  int frameCount;
  int frameCapacity;

  ObjUpvalue* openUpvalues;

  Value lastError;
  int nonYieldableDepth;

  // Yield support (for native Fiber.yield())
  bool shouldYield;

  // VM-global result (not per-fiber)
  Value lastResult;

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

#ifdef PROFILE_STACKS
  FILE* stackSampleFile;
  uint32_t stackSampleRate;
  uint32_t stackSampleCountdown;
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
