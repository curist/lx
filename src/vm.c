#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "objloader.h"
#include "object.h"
#include "memory.h"
#include "vm.h"
#include "native_fn.h"
#include "lx/lxglobals.h"

#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

VM vm;

static void enterDirectMode() {
  vm.currentFiber = NULL;

  vm.stack = vm.mainStack;
  vm.stackTop = vm.mainStack;
  vm.stackCapacity = STACK_MAX;

  vm.frames = vm.mainFrames;
  vm.frameCount = 0;
  vm.frameCapacity = FRAMES_MAX;

  vm.openUpvalues = NULL;
  vm.errorJmp = NULL;
  vm.nonYieldableDepth = 0;
  vm.shouldYield = false;
}

static void syncFromVM() {
  ObjFiber* f = vm.currentFiber;
  if (!f) return;

  f->stackTop = vm.stackTop;
  f->frameCount = vm.frameCount;
  f->openUpvalues = vm.openUpvalues;
  f->lastError = vm.lastError;
}

__attribute__((unused))
static void switchToFiber(ObjFiber* f) {
  // Save outgoing context
  syncFromVM();

  vm.currentFiber = f;

  vm.stack = f->stack;
  vm.stackTop = f->stackTop;
  vm.stackCapacity = f->stackCapacity;

  vm.frames = (CallFrame*)f->frames;
  vm.frameCount = f->frameCount;
  vm.frameCapacity = f->frameCapacity;

  vm.openUpvalues = f->openUpvalues;
  vm.lastError = f->lastError;

  // Clear errorJmp to avoid longjmp into stale C stack
  // (pcall protection doesn't span fiber switches until per-fiber error handling is added)
  vm.errorJmp = NULL;
}

static Value buildRuntimeErrorValue(const char* message) {
  ObjHashmap* err = newHashmap();
  push(OBJ_VAL(err));

  // message
  push(CSTRING_VAL("message"));
  push(CSTRING_VAL(message));
  tableSet(&err->table, vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();

  // stack
  ObjArray* stackArr = newArray();
  push(OBJ_VAL(stackArr));
  push(CSTRING_VAL("stack"));
  push(OBJ_VAL(stackArr));
  tableSet(&err->table, vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->closure->function;
    size_t instruction = 0;
    if (frame->ip > function->chunk.code) {
      instruction = (size_t)(frame->ip - function->chunk.code - 1);
    }

    int line = 0;
    if (function->chunk.lines != NULL && function->chunk.count > 0) {
      if (instruction >= (size_t)function->chunk.count) {
        instruction = (size_t)function->chunk.count - 1;
      }
      line = (int)function->chunk.lines[instruction];
    }

    ObjHashmap* frameRec = newHashmap();
    push(OBJ_VAL(frameRec));

    // file
    push(CSTRING_VAL("file"));
    if (function->filename != NULL) {
      push(CSTRING_VAL(function->filename->chars));
    } else {
      push(NIL_VAL);
    }
    tableSet(&frameRec->table, vm.stackTop[-2], vm.stackTop[-1]);
    pop();
    pop();

    // line
    push(CSTRING_VAL("line"));
    push(NUMBER_VAL((double)line));
    tableSet(&frameRec->table, vm.stackTop[-2], vm.stackTop[-1]);
    pop();
    pop();

    // name
    push(CSTRING_VAL("name"));
    if (function->name != NULL) {
      push(CSTRING_VAL(function->name->chars));
    } else {
      push(CSTRING_VAL("script"));
    }
    tableSet(&frameRec->table, vm.stackTop[-2], vm.stackTop[-1]);
    pop();
    pop();

    // push record into stack array
    writeValueArray(&stackArr->array, OBJ_VAL(frameRec));
    pop(); // frameRec
  }

  pop(); // stackArr

  Value out = OBJ_VAL(err);
  pop(); // err
  return out;
}

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list args2;
  va_copy(args2, args);
  int n = vsnprintf(NULL, 0, format, args);
  va_end(args);

  char* message = NULL;
  if (n < 0) {
    message = (char*)malloc(6);
    if (message != NULL) memcpy(message, "Error", 6);
  } else {
    message = (char*)malloc((size_t)n + 1);
    if (message != NULL) {
      vsnprintf(message, (size_t)n + 1, format, args2);
    }
  }
  va_end(args2);

  if (message == NULL) {
    // Fall back to a fixed string if we can't allocate.
    message = (char*)malloc(6);
    if (message != NULL) memcpy(message, "Error", 6);
  }

  if (message == NULL) {
    // Worst case: print and bail.
    fputs("Error\n", stderr);
    enterDirectMode();
    return;
  }

  // Protected mode: capture structured error and abort back to the protected
  // boundary. The caller is responsible for restoring VM state.
  if (vm.errorJmp != NULL) {
    vm.lastError = buildRuntimeErrorValue(message);
    free(message);
    longjmp(*vm.errorJmp, 1);
  }

  // Default mode: print like before.
  fputs(message, stderr);
  fputs("\n", stderr);
  free(message);

  int skippedCount = 0;
  bool shouldTruncate = vm.frameCount > 16;

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    if (shouldTruncate) {
      if (i > 5 && vm.frameCount - i > 6) {
        skippedCount++;
        continue;
      }
      if (i == 5) {
        fprintf(stderr, "...skipped %d lines...\n", skippedCount);
      }
    }
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    if (function->filename != NULL) {
      fprintf(stderr, "[%s ", function->filename->chars);
    } else {
      fprintf(stderr, "[");
    }
    fprintf(stderr, "L%d] in ", function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }
  enterDirectMode();
}

static bool valueToInt64Exact(Value value, int64_t* out, const char* context) {
  if (IS_FIXNUM(value)) {
    *out = AS_FIXNUM(value);
    return true;
  }

  if (!IS_NUMBER(value)) {
    runtimeError("%s must be a number.", context);
    return false;
  }

  double num = AS_NUMBER(value);
  if (!isfinite(num)) {
    runtimeError("%s must be a finite number.", context);
    return false;
  }

  double truncNum = trunc(num);
  if (truncNum != num) {
    runtimeError("%s must be an integer.", context);
    return false;
  }

  if (num < (double)INT64_MIN || num > (double)INT64_MAX) {
    runtimeError("%s is out of range.", context);
    return false;
  }

  int64_t i = (int64_t)num;
  if ((double)i != num) {
    runtimeError("%s must be an integer exactly representable as a number.", context);
    return false;
  }

  *out = i;
  return true;
}

static bool valueToInt32Exact(Value value, int32_t* out, const char* context) {
  int64_t i64;
  if (!valueToInt64Exact(value, &i64, context)) return false;
  if (i64 < INT32_MIN || i64 > INT32_MAX) {
    runtimeError("%s is out of 32-bit integer range.", context);
    return false;
  }
  *out = (int32_t)i64;
  return true;
}

static bool pushInt64AsNumber(int64_t i, const char* context) {
  if (fixnumFitsInt64(i)) {
    push(FIXNUM_VAL(i));
    return true;
  }

  double d = (double)i;
  if ((int64_t)d != i) {
    runtimeError("%s result is out of representable range.", context);
    return false;
  }
  push(NUMBER_VAL(d));
  return true;
}

static void pushInt64AsNumberOrFlonum(int64_t i) {
  if (fixnumFitsInt64(i)) {
    push(FIXNUM_VAL(i));
    return;
  }
  push(NUMBER_VAL((double)i));
}

static inline bool tableCtrlIsFull(uint8_t c) {
  return c != CTRL_EMPTY && c != CTRL_TOMB;
}

static bool installExportsIntoGlobals(Value exportsVal) {
  if (!IS_HASHMAP(exportsVal)) {
    runtimeError("globals.lx must return a hashmap of exports.");
    return false;
  }

  Table* t = &AS_HASHMAP(exportsVal);

  // If exports is empty, nothing to do.
  if (t->count == 0) return true;

  // Iterate actual slot array. (Your other code uses `capacity` as slot count.)
  for (int i = 0; i < t->capacity; i++) {
    if (!tableCtrlIsFull(t->control[i])) continue;

    Entry* e = &t->entries[i];

    // enforce string-keyed exports for globals.
    if (!IS_STRING(e->key)) {
      runtimeError("globals.lx export keys must be strings.");
      return false;
    }

    tableSet(&vm.globals, e->key, e->value);
  }

  return true;
}

void initVM() {
  enterDirectMode();
  vm.objects = NULL;
  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;
  vm.gcRunning = false;

  vm.lastResult = NIL_VAL;
  vm.lastError = NIL_VAL;
  vm.errorJmp = NULL;
  vm.shouldYield = false;

  initTable(&vm.globals);
  initTable(&vm.strings);

#ifdef PROFILE_OPCODES
  for (int i = 0; i < 256; i++) {
    vm.opCounts[i] = 0;
  }
#endif

  defineBuiltinNatives();

  // include global fns
  InterpretResult r = interpret((uint8_t*)lxglobals_bytecode);
  if (r != INTERPRET_OK) {
    fprintf(stderr, "failed to load lx globals\n");
    return exit(31);
  }
  if (!installExportsIntoGlobals(vm.lastResult)) {
    fprintf(stderr, "failed to load lx globals\n");
    return exit(31);
  }
}

void freeVM() {
#ifdef PROFILE_OPCODES
  // Print top 15 opcodes by frequency
  fprintf(stderr, "\n=== Opcode Profile ===\n");

  // Create sorted list of (opcode, count) pairs
  typedef struct { uint8_t op; uint64_t count; } OpCount;
  OpCount sorted[256];
  for (int i = 0; i < 256; i++) {
    sorted[i].op = i;
    sorted[i].count = vm.opCounts[i];
  }

  // Simple bubble sort (good enough for 256 entries)
  for (int i = 0; i < 255; i++) {
    for (int j = i + 1; j < 256; j++) {
      if (sorted[j].count > sorted[i].count) {
        OpCount tmp = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = tmp;
      }
    }
  }

  // Print top 15
  uint64_t total = 0;
  for (int i = 0; i < 256; i++) total += vm.opCounts[i];

  for (int i = 0; i < 15 && sorted[i].count > 0; i++) {
    double pct = 100.0 * sorted[i].count / total;
    fprintf(stderr, "%2d. OP_%02x: %12llu (%5.2f%%)\n",
            i+1, sorted[i].op, sorted[i].count, pct);
  }
  fprintf(stderr, "Total ops: %llu\n", total);
  fprintf(stderr, "======================\n\n");
#endif

  freeTable(&vm.globals);
  freeTable(&vm.strings);
  freeObjects();
}

static bool call(ObjClosure* closure, int argCount) {
  int arity = closure->function->arity;

  if (vm.frameCount >= vm.frameCapacity) {
    runtimeError("Stack overflow.");
    return false;
  }

  // push nil for insufficient args passed
  for (int i = 0; i < arity - argCount; i++) {
    push(NIL_VAL);
  }

  // Discard extra args from value stack
  for (int i = 0; i < argCount - arity; i++) {
    pop();
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  // After dropping extras, stack is [... callee arg0..arg(arity-1)]
  frame->slots = vm.stackTop - arity - 1;  // Points at callee (slot 0)
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_CLOSURE:
        return call(AS_CLOSURE(callee), argCount);
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee)->function;
        // Stack layout: [... callee arg0 arg1 ... argN]
        // args points to first argument (stackTop - argCount)
        // Native function writes result to args[-1] (the callee position)
        Value* stackBase = vm.stackTop - argCount - 1; // points to callee slot
#ifdef DEBUG_TRACE_EXECUTION
        Value* stackTopBefore = vm.stackTop;
#endif
        bool success = native(argCount, vm.stackTop - argCount);
#ifdef DEBUG_TRACE_EXECUTION
        if (vm.stackTop != stackTopBefore) {
          runtimeError("Native function must not mutate vm.stackTop (push/pop forbidden)");
          return false;
        }
#endif
        if (success) {
          // Result is now at stackBase (callee slot)
          // Set stackTop to callee+1 (pop callee+args, leave 1 result)
          vm.stackTop = stackBase + 1;

          // Check if native function requested a yield (Fiber.yield)
          if (vm.shouldYield) {
            vm.shouldYield = false;

            // The yielded value is already on the stack (result of native call)
            Value yieldedValue = *stackBase;
            vm.lastResult = yieldedValue;

            // Pop the result from stack
            vm.stackTop = stackBase;

            // Save current state to fiber
            syncFromVM();

            // Transition fiber to SUSPENDED state
            if (vm.currentFiber) {
              vm.currentFiber->state = FIBER_SUSPENDED;
            }

            // This will cause the OP_CALL to complete, then runUntil returns
            // The caller (fiberResume) will see fiber state = SUSPENDED
          }

          return true;
        } else {
          // Error message is in callee slot (stackBase)
          runtimeError(AS_STRING(*stackBase)->chars);
          return false;
        }
      }
      default:
        break; // Non-callable object type.
    }
  }
  runtimeError("Can only call functions.");
  return false;
}

static inline bool insertCalleeBelowArgs(Value callee, int argCount) {
  if ((vm.stackTop - vm.stack) >= vm.stackCapacity) {
    runtimeError("Stack overflow.");
    return false;
  }

  Value* argsBase = vm.stackTop - argCount;  // Points at arg0 (or stackTop if argCount==0)
  for (int i = argCount; i > 0; i--) {
    argsBase[i] = argsBase[i - 1];
  }
  argsBase[0] = callee;
  vm.stackTop++;
  return true;
}

static ObjUpvalue* captureUpvalue(Value* local) {
  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue* createdUpvalue = newUpvalue(local);
  createdUpvalue->next = upvalue;

  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

inline static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static InterpretResult runUntil(int stopFrameCount);

static void pcallSetField(ObjHashmap* map, const char* key, Value value) {
  push(CSTRING_VAL(key));
  push(value);
  tableSet(&map->table, vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();
}

static Value pcallResult(bool ok, Value value, Value error) {
  ObjHashmap* out = newHashmap();
  push(OBJ_VAL(out));
  pcallSetField(out, "ok", BOOL_VAL(ok));
  pcallSetField(out, "value", value);
  pcallSetField(out, "error", error);
  pop();
  return OBJ_VAL(out);
}

// Fiber.resume result builder - tagged union for yield/return/error
// Returns: {tag: "yield"|"return"|"error", value: ..., error: ...}
// Note: 'value' will become 'values' array when multi-value yield/resume is added
static Value fiberResult(const char* tag, Value value, Value error) {
  ObjHashmap* out = newHashmap();
  push(OBJ_VAL(out));

  ObjString* tagKey = COPY_CSTRING("tag");
  push(OBJ_VAL(tagKey));
  ObjString* tagVal = COPY_CSTRING(tag);
  push(OBJ_VAL(tagVal));
  tableSet(&out->table, vm.stackTop[-2], vm.stackTop[-1]);
  pop();
  pop();

  if (!IS_NIL(value)) {
    pcallSetField(out, "value", value);
  }

  if (!IS_NIL(error)) {
    pcallSetField(out, "error", error);
  }

  pop();
  return OBJ_VAL(out);
}

// Fiber.create(fn) - creates a new fiber from a function
static bool fiberCreateNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = CSTRING_VAL("Error: Fiber.create takes exactly 1 argument (function).");
    return false;
  }

  Value fn = args[0];
  if (!IS_CLOSURE(fn)) {
    args[-1] = CSTRING_VAL("Error: Fiber.create argument must be a function.");
    return false;
  }

  // Create new fiber
  ObjFiber* fiber = newFiber();
  fiber->state = FIBER_NEW;

  // Store the function to be called when fiber is first resumed
  // We'll set up the call frame in fiberResumeNative
  fiber->stack[0] = fn;
  fiber->stackTop = fiber->stack + 1;

  args[-1] = OBJ_VAL(fiber);
  return true;
}

// Fiber.resume(fiber, ...args) - resumes a suspended or new fiber
static bool fiberResumeNative(int argCount, Value* args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: Fiber.resume takes at least 1 argument (fiber).");
    return false;
  }

  // MVP: Disallow resuming from inside a fiber (no nested fiber calls)
  if (vm.currentFiber != NULL) {
    args[-1] = CSTRING_VAL("Error: Cannot call Fiber.resume from inside a fiber (nested fibers not supported yet).");
    return false;
  }

  Value fiberVal = args[0];
  if (!IS_FIBER(fiberVal)) {
    args[-1] = CSTRING_VAL("Error: Fiber.resume first argument must be a fiber.");
    return false;
  }

  ObjFiber* fiber = AS_FIBER(fiberVal);

  // Check fiber state - API misuse should throw, not return error result
  if (fiber->state == FIBER_RUNNING) {
    args[-1] = CSTRING_VAL("Error: Cannot resume a running fiber.");
    return false;
  }

  if (fiber->state == FIBER_DONE) {
    args[-1] = CSTRING_VAL("Error: Cannot resume a completed fiber.");
    return false;
  }

  if (fiber->state == FIBER_ERROR) {
    args[-1] = CSTRING_VAL("Error: Cannot resume a fiber that errored.");
    return false;
  }

  // Save current context before switching
  int callerFrameCount = vm.frameCount;
  Value* callerStackTop = vm.stackTop;
  ObjUpvalue* callerOpenUpvalues = vm.openUpvalues;

  // Set up error handler for fiber execution
  jmp_buf jb;
  jmp_buf* prev = vm.errorJmp;
  vm.errorJmp = &jb;
  vm.lastError = NIL_VAL;

  int jumped = setjmp(jb);
  if (jumped != 0) {
    // Fiber errored
    Value err = vm.lastError;

    // Mark fiber as errored and close its upvalues
    if (vm.currentFiber) {
      vm.currentFiber->state = FIBER_ERROR;
      // Close all fiber upvalues (vm.openUpvalues is the fiber's chain)
      closeUpvalues(vm.stack);
    }

    // Restore caller context (direct mode)
    vm.currentFiber = NULL;
    vm.stack = vm.mainStack;
    vm.stackTop = callerStackTop;
    vm.stackCapacity = STACK_MAX;
    vm.frames = vm.mainFrames;
    vm.frameCount = callerFrameCount;
    vm.frameCapacity = FRAMES_MAX;
    vm.openUpvalues = callerOpenUpvalues;
    vm.errorJmp = prev;

    args[-1] = fiberResult("error", NIL_VAL, err);
    return true;
  }

  // Switch to fiber
  switchToFiber(fiber);

  // Restore error handler (switchToFiber clears it for safety, but we need it for Fiber.resume)
  vm.errorJmp = &jb;

  // Handle NEW fiber: set up the initial call frame
  if (fiber->state == FIBER_NEW) {
    // Stack should have [closure] at position 0
    Value fn = vm.stack[0];
    if (!IS_CLOSURE(fn)) {
      // Invariant violation: Fiber.create should have validated this
      // This is an internal consistency check (should never happen)
      // Restore caller context and throw (API misuse / precondition failure)
      vm.currentFiber = NULL;
      vm.stack = vm.mainStack;
      vm.stackTop = callerStackTop;
      vm.stackCapacity = STACK_MAX;
      vm.frames = vm.mainFrames;
      vm.frameCount = callerFrameCount;
      vm.frameCapacity = FRAMES_MAX;
      vm.openUpvalues = callerOpenUpvalues;
      vm.errorJmp = prev;
      args[-1] = CSTRING_VAL("Error: Fiber function is not a closure.");
      return false;
    }

    ObjClosure* closure = AS_CLOSURE(fn);

    // Push any resume arguments after the closure
    for (int i = 1; i < argCount; i++) {
      push(args[i]);
    }

    // Set up the call frame
    if (!call(closure, argCount - 1)) {
      // call() failed (e.g., stack overflow) - treat as fiber execution error
      // Mark fiber as errored and close its upvalues
      fiber->state = FIBER_ERROR;
      closeUpvalues(vm.stack);

      // Restore caller context (don't use enterDirectMode - it corrupts saved state)
      vm.currentFiber = NULL;
      vm.stack = vm.mainStack;
      vm.stackTop = callerStackTop;
      vm.stackCapacity = STACK_MAX;
      vm.frames = vm.mainFrames;
      vm.frameCount = callerFrameCount;
      vm.frameCapacity = FRAMES_MAX;
      vm.openUpvalues = callerOpenUpvalues;
      vm.errorJmp = prev;

      // Return error result (use lastError if set, or synthesize message)
      Value err = IS_NIL(vm.lastError) ? CSTRING_VAL("Failed to call fiber function.") : vm.lastError;
      args[-1] = fiberResult("error", NIL_VAL, err);
      return true;
    }
  } else if (fiber->state == FIBER_SUSPENDED) {
    // MVP: Only support single-value resume for suspended fibers
    // (multi-value yield/resume not implemented yet)
    if (argCount > 2) {
      // API misuse - restore caller context and throw
      // (don't use enterDirectMode - it corrupts saved state)
      vm.currentFiber = NULL;
      vm.stack = vm.mainStack;
      vm.stackTop = callerStackTop;
      vm.stackCapacity = STACK_MAX;
      vm.frames = vm.mainFrames;
      vm.frameCount = callerFrameCount;
      vm.frameCapacity = FRAMES_MAX;
      vm.openUpvalues = callerOpenUpvalues;
      vm.errorJmp = prev;
      args[-1] = CSTRING_VAL("Error: Fiber.resume currently only supports 1 resume value for suspended fibers.");
      return false;
    }

    // Push resume value for suspended fiber (becomes return value of Fiber.yield())
    // If no value provided, push nil
    if (argCount >= 2) {
      push(args[1]);
    } else {
      push(NIL_VAL);
    }
  }

  fiber->state = FIBER_RUNNING;

  // Run the fiber until it yields, returns, or errors
  InterpretResult result = runUntil(0);

  // Check result
  if (result != INTERPRET_OK) {
    // Error already handled by setjmp above
    // Should not reach here, but handle gracefully
    Value err = vm.lastError;

    // Mark fiber as errored and close its upvalues
    if (vm.currentFiber) {
      vm.currentFiber->state = FIBER_ERROR;
      // Close all fiber upvalues (vm.openUpvalues is the fiber's chain)
      closeUpvalues(vm.stack);
    }

    // Restore direct mode
    vm.currentFiber = NULL;
    vm.stack = vm.mainStack;
    vm.stackTop = callerStackTop;
    vm.stackCapacity = STACK_MAX;
    vm.frames = vm.mainFrames;
    vm.frameCount = callerFrameCount;
    vm.frameCapacity = FRAMES_MAX;
    vm.openUpvalues = callerOpenUpvalues;
    vm.errorJmp = prev;

    args[-1] = fiberResult("error", NIL_VAL, err);
    return true;
  }

  // Fiber either yielded or completed
  Value returnValue = vm.lastResult;
  FiberState finalState = fiber->state;

  // Switch back to direct mode (restore original VM state)
  vm.currentFiber = NULL;
  vm.stack = vm.mainStack;
  vm.stackTop = callerStackTop;
  vm.stackCapacity = STACK_MAX;
  vm.frames = vm.mainFrames;
  vm.frameCount = callerFrameCount;
  vm.frameCapacity = FRAMES_MAX;
  vm.openUpvalues = callerOpenUpvalues;
  vm.errorJmp = prev;

  // Return tagged result based on fiber state
  if (finalState == FIBER_SUSPENDED) {
    // Yielded - return {tag: "yield", value: ...}
    args[-1] = fiberResult("yield", returnValue, NIL_VAL);
  } else if (finalState == FIBER_DONE) {
    // Completed - return {tag: "return", value: ...}
    args[-1] = fiberResult("return", returnValue, NIL_VAL);
  } else {
    // Unexpected state - API invariant violation, should throw
    args[-1] = CSTRING_VAL("Error: Fiber in unexpected state after execution.");
    return false;
  }

  return true;
}

// Fiber.yield(value) - yields from current fiber
static bool fiberYieldNative(int argCount, Value* args) {
  // Check that we're in a fiber
  if (!vm.currentFiber) {
    args[-1] = CSTRING_VAL("Error: Fiber.yield can only be called inside a fiber.");
    return false;
  }

  // Check non-yieldable depth
  // Note: This native function itself is called from non-yieldable context,
  // but we'll allow it for now as a special case. Proper solution is to
  // make the compiler emit OP_YIELD directly.
  // For now, we set a flag and handle it after the native returns.

  // Get yielded value (default to nil if no args)
  Value yieldedValue = (argCount > 0) ? args[0] : NIL_VAL;

  // Set yield flag for runUntil() to handle after this native returns
  vm.shouldYield = true;

  // Store the yielded value
  args[-1] = yieldedValue;
  return true;
}

// Fiber.status(fiber) - returns fiber state as string
static bool fiberStatusNative(int argCount, Value* args) {
  if (argCount != 1) {
    args[-1] = CSTRING_VAL("Error: Fiber.status takes exactly 1 argument (fiber).");
    return false;
  }

  Value fiberVal = args[0];
  if (!IS_FIBER(fiberVal)) {
    args[-1] = CSTRING_VAL("Error: Fiber.status argument must be a fiber.");
    return false;
  }

  ObjFiber* fiber = AS_FIBER(fiberVal);

  const char* statusStr;
  switch (fiber->state) {
    case FIBER_NEW:       statusStr = "new"; break;
    case FIBER_RUNNING:   statusStr = "running"; break;
    case FIBER_SUSPENDED: statusStr = "suspended"; break;
    case FIBER_DONE:      statusStr = "done"; break;
    case FIBER_ERROR:     statusStr = "error"; break;
    default:              statusStr = "unknown"; break;
  }

  args[-1] = CSTRING_VAL(statusStr);
  return true;
}

static bool pcallNative(int argCount, Value* args) {
  if (argCount < 1) {
    args[-1] = CSTRING_VAL("Error: Lx.pcall takes at least 1 arg (fn).");
    return false;
  }

  Value fn = args[0];
  int fnArgCount = argCount - 1;

  int baseFrameCount = vm.frameCount;
  Value* baseStackTop = vm.stackTop;

  jmp_buf jb;
  jmp_buf* prev = vm.errorJmp;
  vm.errorJmp = &jb;
  vm.lastError = NIL_VAL;

  int jumped = setjmp(jb);
  if (jumped != 0) {
    Value err = vm.lastError;
    closeUpvalues(baseStackTop);
    vm.stackTop = baseStackTop;
    vm.frameCount = baseFrameCount;
    vm.errorJmp = prev;
    args[-1] = pcallResult(false, NIL_VAL, err);
    return true;
  }

  // Arrange a normal VM call: [ ... fn arg0..argN ]
  push(fn);
  for (int i = 1; i < argCount; i++) {
    push(args[i]);
  }

  if (!callValue(peek(fnArgCount), fnArgCount)) {
    // Should longjmp via runtimeError(), but keep a fallback.
    Value err = vm.lastError;
    closeUpvalues(baseStackTop);
    vm.stackTop = baseStackTop;
    vm.frameCount = baseFrameCount;
    vm.errorJmp = prev;
    args[-1] = pcallResult(false, NIL_VAL, err);
    return true;
  }

  InterpretResult r = runUntil(baseFrameCount);
  if (r != INTERPRET_OK) {
    // Should longjmp via runtimeError(), but keep a fallback.
    Value err = vm.lastError;
    closeUpvalues(baseStackTop);
    vm.stackTop = baseStackTop;
    vm.frameCount = baseFrameCount;
    vm.errorJmp = prev;
    args[-1] = pcallResult(false, NIL_VAL, err);
    return true;
  }

  Value result = pop();
  vm.errorJmp = prev;
  args[-1] = pcallResult(true, result, NIL_VAL);
  return true;
}

inline static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

inline static void concatenate() {
  ObjString* b = AS_STRING(peek(0));
  ObjString* a = AS_STRING(peek(1));

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);
  pop();
  pop();
  push(OBJ_VAL(result));
}

// Helper: Get value by index (shared logic for all get-by-index operations)
// Returns true on success, false on error (error message already set)
static bool getByIndexImpl(Value object, Value key, Value* out) {
  if (!IS_ENUM(object) && !IS_HASHMAP(object) && !IS_ARRAY(object) && !IS_STRING(object)) {
    runtimeError("Only array / hashmap / string can get value by index.");
    return false;
  }

  if (IS_ARRAY(object)) {
    if (!IS_NUMBER(key)) {
      runtimeError("Can only use number index to access array.");
      return false;
    }
    int index = (int)AS_NUMBER(key);
    if ((double)index != AS_NUMBER(key)) {
      runtimeError("Can only use integer index to access array.");
      return false;
    }

    ValueArray* array = &AS_ARRAY(object);
    if (index >= 0 && index < array->count) {
      *out = array->values[index];
    } else {
      *out = NIL_VAL;
    }
  } else if (IS_ENUM(object)) {
    if (!IS_NUMBER(key) && !IS_STRING(key)) {
      runtimeError("Enum key type must be number or string.");
      return false;
    }
    Table* table = &AS_ENUM_FORWARD(object);
    if (!tableGet(table, key, out)) {
      *out = NIL_VAL;
    }
  } else if (IS_HASHMAP(object)) {
    if (!IS_NUMBER(key) && !IS_STRING(key)) {
      runtimeError("Hashmap key type must be number or string.");
      return false;
    }
    Table* table = &AS_HASHMAP(object);
    if (!tableGet(table, key, out)) {
      *out = NIL_VAL;
    }
  } else {
    // String
    if (!IS_NUMBER(key)) {
      runtimeError("String index type must be a number.");
      return false;
    }
    ObjString* s = AS_STRING(object);
    char* ch = NULL;
    size_t index = (size_t)AS_NUMBER(key);
    if (index < s->length) {
      ch = &s->chars[index];
    }
    if (ch != NULL) {
      *out = OBJ_VAL(copyString(ch, 1));
    } else {
      *out = NIL_VAL;
    }
  }
  return true;
}

// Helper: Set value by index (shared logic for all set-by-index operations)
// Returns true on success, false on error (error message already set)
static bool setByIndexImpl(Value object, Value key, Value value, Value* out) {
  if (IS_ENUM(object)) {
    runtimeError("Enum is immutable.");
    return false;
  }
  if (!IS_HASHMAP(object) && !IS_ARRAY(object)) {
    runtimeError("Only array or hashmap can set value by index.");
    return false;
  }

  if (IS_ARRAY(object)) {
    if (!IS_NUMBER(key)) {
      runtimeError("Can only use number index to access array.");
      return false;
    }
    int index = (int)AS_NUMBER(key);
    if ((double)index != AS_NUMBER(key)) {
      runtimeError("Can only use integer index to access array.");
      return false;
    }

    ValueArray* array = &AS_ARRAY(object);
    if (index >= 0 && index < array->count) {
      array->values[index] = value;
      *out = value;
    } else {
      *out = NIL_VAL;
    }
  } else {
    // Hashmap
    if (!IS_NUMBER(key) && !IS_STRING(key)) {
      runtimeError("Hashmap key type must be number or string.");
      return false;
    }
    Table* table = &AS_HASHMAP(object);
    tableSet(table, key, value);
    *out = value;
  }
  return true;
}

static InterpretResult runUntil(int stopFrameCount) {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];
  ObjClosure* closure = frame->closure;
  Value* slots = frame->slots;

  uint8_t op = 0;

#define READ_BYTE()  (*frame->ip++)
#define READ_SHORT() \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
  (frame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())

#define BINARY_OP(valueType, op_)                                    \
  do {                                                               \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {              \
      runtimeError("Operands must be numbers.");                     \
      return INTERPRET_RUNTIME_ERROR;                                \
    }                                                                \
    double b = AS_NUMBER(pop());                                     \
    double a = AS_NUMBER(pop());                                     \
    push(valueType(a op_ b));                                        \
  } while (false)

#define BIT_BINARY_OP(op_, opName)                                   \
  do {                                                               \
    int32_t b;                                                       \
    int32_t a;                                                       \
    if (!valueToInt32Exact(pop(), &b, opName)) return INTERPRET_RUNTIME_ERROR; \
    if (!valueToInt32Exact(pop(), &a, opName)) return INTERPRET_RUNTIME_ERROR; \
    uint32_t r = ((uint32_t)a) op_ ((uint32_t)b);                    \
    if (!pushInt64AsNumber((int64_t)(int32_t)r, opName)) return INTERPRET_RUNTIME_ERROR; \
  } while (false)

#define BIT_SHIFT_OP(opName, isRight)                                \
  do {                                                               \
    int32_t shift;                                                   \
    int32_t a;                                                       \
    if (!valueToInt32Exact(pop(), &shift, opName)) return INTERPRET_RUNTIME_ERROR; \
    if (!valueToInt32Exact(pop(), &a, opName)) return INTERPRET_RUNTIME_ERROR; \
    if (shift < 0 || shift > 31) {                                   \
      runtimeError("%s shift count must be in range 0..31.", opName); \
      return INTERPRET_RUNTIME_ERROR;                                \
    }                                                                \
    uint32_t ua = (uint32_t)a;                                       \
    uint32_t res;                                                    \
    if (!(isRight)) {                                                \
      res = ua << (uint32_t)shift;                                   \
    } else {                                                         \
      res = ua >> (uint32_t)shift;                                   \
      if (shift != 0 && a < 0) {                                     \
        res |= ~(UINT32_C(0xFFFFFFFF) >> (uint32_t)shift);           \
      }                                                              \
    }                                                                \
    if (!pushInt64AsNumber((int64_t)(int32_t)res, opName)) return INTERPRET_RUNTIME_ERROR; \
  } while (false)

  for (;;) {
    op = READ_BYTE();

#ifdef PROFILE_OPCODES
    vm.opCounts[op]++;
#endif

#ifdef DEBUG_TRACE_EXECUTION
    // Print stack
    printf("        |       \x1b[1;32m[ ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printValue(stdout, *slot);
      printf(" ");
    }
    printf("]\x1b[0m\n");

    // Disassemble current instruction.
    // ip has advanced by 1 (we already READ_BYTE), so instruction start is ip-1.
    const uint8_t* ip_at_op = frame->ip - 1;
    disassembleInstruction(
      &frame->closure->function->chunk,
      (int)(ip_at_op - frame->closure->function->chunk.code),
      false
    );
#endif

    switch (op) {
      case OP_NOP:
        break;

      case OP_CONSTANT:
        push(READ_CONSTANT());
        break;

      case OP_CONSTANT_LONG: {
        uint16_t index = READ_SHORT();
        push(frame->closure->function->chunk.constants.values[index]);
        break;
      }

      case OP_CONST_BYTE:
        push(FIXNUM_VAL(READ_BYTE()));
        break;

      case OP_NIL:
        push(NIL_VAL);
        break;

      case OP_TRUE:
        push(BOOL_VAL(true));
        break;

      case OP_FALSE:
        push(BOOL_VAL(false));
        break;

      case OP_POP:
        pop();
        break;

      case OP_DUP:
        push(peek(0));
        break;

      case OP_GET_LOCAL:
        push(slots[READ_BYTE()]);
        break;

      case OP_SET_LOCAL:
        slots[READ_BYTE()] = peek(0);
        break;

      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, OBJ_VAL(name), &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }

      case OP_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        tableSet(&vm.globals, OBJ_VAL(name), peek(0));
        pop();
        break;
      }

      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        if (tableSet(&vm.globals, OBJ_VAL(name), peek(0))) {
          tableDelete(&vm.globals, OBJ_VAL(name));
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }

      case OP_GET_GLOBAL_LONG: {
        uint16_t index = READ_SHORT();
        ObjString* name = AS_STRING(frame->closure->function->chunk.constants.values[index]);
        Value value;
        if (!tableGet(&vm.globals, OBJ_VAL(name), &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }

      case OP_DEFINE_GLOBAL_LONG: {
        uint16_t index = READ_SHORT();
        ObjString* name = AS_STRING(frame->closure->function->chunk.constants.values[index]);
        tableSet(&vm.globals, OBJ_VAL(name), peek(0));
        pop();
        break;
      }

      case OP_SET_GLOBAL_LONG: {
        uint16_t index = READ_SHORT();
        ObjString* name = AS_STRING(frame->closure->function->chunk.constants.values[index]);
        if (tableSet(&vm.globals, OBJ_VAL(name), peek(0))) {
          tableDelete(&vm.globals, OBJ_VAL(name));
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }

      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(*closure->upvalues[slot]->location);
        break;
      }

      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *closure->upvalues[slot]->location = peek(0);
        break;
      }

      case OP_GET_UPVALUE_LONG: {
        uint16_t slot = READ_SHORT();
        push(*closure->upvalues[slot]->location);
        break;
      }

      case OP_SET_UPVALUE_LONG: {
        uint16_t slot = READ_SHORT();
        *closure->upvalues[slot]->location = peek(0);
        break;
      }

      case OP_GET_BY_INDEX: {
        Value key = pop();
        Value object = pop();
        Value result;
        if (!getByIndexImpl(object, key, &result)) return INTERPRET_RUNTIME_ERROR;
        push(result);
        break;
      }

      case OP_SET_BY_INDEX: {
        Value value = pop();
        Value key = pop();
        Value object = pop();
        Value result;
        if (!setByIndexImpl(object, key, value, &result)) return INTERPRET_RUNTIME_ERROR;
        push(result);
        break;
      }

      case OP_GET_BY_CONST: {
        uint8_t constIdx = READ_BYTE();
        Value object = pop();
        Value key = frame->closure->function->chunk.constants.values[constIdx];
        Value result;
        if (!getByIndexImpl(object, key, &result)) return INTERPRET_RUNTIME_ERROR;
        push(result);
        break;
      }

      case OP_GET_BY_CONST_LONG: {
        uint16_t constIdx = READ_SHORT();
        Value object = pop();
        Value key = frame->closure->function->chunk.constants.values[constIdx];
        Value result;
        if (!getByIndexImpl(object, key, &result)) return INTERPRET_RUNTIME_ERROR;
        push(result);
        break;
      }

      case OP_SET_BY_CONST: {
        uint8_t constIdx = READ_BYTE();
        Value value = pop();
        Value object = pop();
        Value key = frame->closure->function->chunk.constants.values[constIdx];
        Value result;
        if (!setByIndexImpl(object, key, value, &result)) return INTERPRET_RUNTIME_ERROR;
        push(result);
        break;
      }

      case OP_SET_BY_CONST_LONG: {
        uint16_t constIdx = READ_SHORT();
        Value value = pop();
        Value object = pop();
        Value key = frame->closure->function->chunk.constants.values[constIdx];
        Value result;
        if (!setByIndexImpl(object, key, value, &result)) return INTERPRET_RUNTIME_ERROR;
        push(result);
        break;
      }

      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }

      case OP_GREATER:
        BINARY_OP(BOOL_VAL, >);
        break;

      case OP_LESS:
        BINARY_OP(BOOL_VAL, <);
        break;

      case OP_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }

      case OP_SUBTRACT:
        BINARY_OP(NUMBER_VAL, -);
        break;

      case OP_MULTIPLY:
        BINARY_OP(NUMBER_VAL, *);
        break;

      case OP_DIVIDE:
        BINARY_OP(NUMBER_VAL, /);
        break;

      case OP_MOD: {
        int64_t b;
        int64_t a;
        if (!valueToInt64Exact(pop(), &b, "Right operand of %")) return INTERPRET_RUNTIME_ERROR;
        if (!valueToInt64Exact(pop(), &a, "Left operand of %")) return INTERPRET_RUNTIME_ERROR;
        if (b == 0) {
          runtimeError("Division by zero.");
          return INTERPRET_RUNTIME_ERROR;
        }
        int64_t r = a % b;
        if (!pushInt64AsNumber(r, "%")) return INTERPRET_RUNTIME_ERROR;
        break;
      }

      case OP_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        break;

      case OP_NEGATE: {
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      }

      case OP_ADD_INT: {
        if (IS_FIXNUM(peek(0)) && IS_FIXNUM(peek(1))) {
          int64_t b = AS_FIXNUM(pop());
          int64_t a = AS_FIXNUM(pop());
          int64_t r;
          if (__builtin_add_overflow(a, b, &r)) {
            push(NUMBER_VAL((double)a + (double)b));
          } else {
            pushInt64AsNumberOrFlonum(r);
          }
          break;
        }
        // Fallback to the generic semantics (including string concatenation).
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }

      case OP_SUBTRACT_INT: {
        if (IS_FIXNUM(peek(0)) && IS_FIXNUM(peek(1))) {
          int64_t b = AS_FIXNUM(pop());
          int64_t a = AS_FIXNUM(pop());
          int64_t r;
          if (__builtin_sub_overflow(a, b, &r)) {
            push(NUMBER_VAL((double)a - (double)b));
          } else {
            pushInt64AsNumberOrFlonum(r);
          }
          break;
        }
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
          runtimeError("Operands must be numbers.");
          return INTERPRET_RUNTIME_ERROR;
        }
        double b = AS_NUMBER(pop());
        double a = AS_NUMBER(pop());
        push(NUMBER_VAL(a - b));
        break;
      }

      case OP_MULTIPLY_INT: {
        if (IS_FIXNUM(peek(0)) && IS_FIXNUM(peek(1))) {
          int64_t b = AS_FIXNUM(pop());
          int64_t a = AS_FIXNUM(pop());
          __int128 wide = (__int128)a * (__int128)b;
          if (wide >= (__int128)FIXNUM_MIN && wide <= (__int128)FIXNUM_MAX) {
            push(FIXNUM_VAL((int64_t)wide));
          } else {
            push(NUMBER_VAL((double)a * (double)b));
          }
          break;
        }
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
          runtimeError("Operands must be numbers.");
          return INTERPRET_RUNTIME_ERROR;
        }
        double b = AS_NUMBER(pop());
        double a = AS_NUMBER(pop());
        push(NUMBER_VAL(a * b));
        break;
      }

      case OP_NEGATE_INT: {
        if (IS_FIXNUM(peek(0))) {
          int64_t a = AS_FIXNUM(pop());
          if (a == FIXNUM_MIN) {
            push(NUMBER_VAL(-((double)a)));
          } else {
            pushInt64AsNumberOrFlonum(-a);
          }
          break;
        }
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      }

      case OP_ADD_NUM: {
        // Quickened opcode: assumes two numbers (guard + deopt).
        // When quickening infrastructure is implemented, guard failure will deopt.
        // For now, fall back to baseline OP_ADD semantics on guard failure.
        Value b = peek(0);
        Value a = peek(1);
        if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
          // Fast path: both are numbers
          pop(); pop();
          push(NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b)));
          break;
        }
        // Guard failed: fall back to baseline OP_ADD logic
        // TODO: when quickening is implemented, this will deopt and re-dispatch
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double numB = AS_NUMBER(pop());
          double numA = AS_NUMBER(pop());
          push(NUMBER_VAL(numA + numB));
        } else {
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }

      case OP_ADD_STR: {
        // Quickened opcode: assumes two strings (guard + deopt).
        // When quickening infrastructure is implemented, guard failure will deopt.
        // For now, fall back to baseline OP_ADD semantics on guard failure.
        Value b = peek(0);
        Value a = peek(1);
        if (LIKELY(IS_STRING(a) && IS_STRING(b))) {
          // Fast path: both are strings, concatenate them
          // Must match baseline OP_ADD concatenation semantics exactly
          concatenate();
          break;
        }
        // Guard failed: fall back to baseline OP_ADD logic
        // TODO: when quickening is implemented, this will deopt and re-dispatch
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double numB = AS_NUMBER(pop());
          double numA = AS_NUMBER(pop());
          push(NUMBER_VAL(numA + numB));
        } else {
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }

      case OP_BIT_AND:
        BIT_BINARY_OP(&, "&");
        break;

      case OP_BIT_OR:
        BIT_BINARY_OP(|, "|");
        break;

      case OP_BIT_XOR:
        BIT_BINARY_OP(^, "^");
        break;

      case OP_BIT_LSHIFT:
        BIT_SHIFT_OP("<<", false);
        break;

      case OP_BIT_RSHIFT:
        BIT_SHIFT_OP(">>", true);
        break;

      case OP_ASSOC: {
        Value hashmap = peek(2);
        Value key     = peek(1);
        Value value   = peek(0);

        if (IS_ENUM(hashmap)) {
          if (!IS_STRING(key)) {
            runtimeError("Enum member name must be a string.");
            return INTERPRET_RUNTIME_ERROR;
          }
          if (!IS_NUMBER(value)) {
            runtimeError("Enum member value must be a number.");
            return INTERPRET_RUNTIME_ERROR;
          }
          ObjEnum* e = AS_ENUM(hashmap);
          bool isNewName = tableSet(&e->forward, key, value);
          tableSet(&e->reverse, value, key);
          if (isNewName) {
            writeValueArray(&e->names, key);
          }

        } else if (IS_HASHMAP(hashmap)) {
          if (!IS_NUMBER(key) && !IS_STRING(key)) {
            runtimeError("Hashmap key type must be number or string.");
            return INTERPRET_RUNTIME_ERROR;
          }
          Table* table = &AS_HASHMAP(hashmap);
          tableSet(table, key, value);

        } else {
          runtimeError("Can only assoc to hashmap.");
          return INTERPRET_RUNTIME_ERROR;
        }

        pop(); // value
        pop(); // key
        break;
      }

      case OP_APPEND: {
        Value array = peek(1);
        Value value = peek(0);
        if (!IS_ARRAY(array)) {
          runtimeError("Can only append to array.");
          return INTERPRET_RUNTIME_ERROR;
        }
        writeValueArray(&AS_ARRAY(array), value);
        pop(); // value
        break;
      }

      case OP_HASHMAP:
        push(OBJ_VAL(newHashmap()));
        break;

      case OP_ENUM:
        push(OBJ_VAL(newEnum()));
        break;

      case OP_ARRAY:
        push(OBJ_VAL(newArray()));
        break;

      case OP_LENGTH: {
        if (IS_STRING(peek(0))) {
          push(NUMBER_VAL(AS_STRING(pop())->length));
        } else if (IS_ARRAY(peek(0))) {
          push(NUMBER_VAL(AS_ARRAY(pop()).count));
        } else {
          runtimeError("Operand must be string or array.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }

      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }

      case OP_JUMP_IF_TRUE: {
        uint16_t offset = READ_SHORT();
        if (!isFalsey(pop())) frame->ip += offset;
        break;
      }

      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(pop())) frame->ip += offset;
        break;
      }

      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }

      case OP_CALL: {
        int argCount = (int)READ_BYTE();
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        // Check if a yield occurred during the call (from Fiber.yield native)
        if (vm.currentFiber && vm.currentFiber->state == FIBER_SUSPENDED) {
          // Fiber yielded, exit runUntil
          return INTERPRET_OK;
        }

        frame = &vm.frames[vm.frameCount - 1];
        closure = frame->closure;
        slots = frame->slots;
        break;
      }

      case OP_CALL_LOCAL: {
        uint8_t calleeSlot = READ_BYTE();
        int argCount = (int)READ_BYTE();
        Value callee = slots[calleeSlot];

        if (!insertCalleeBelowArgs(callee, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        if (IS_OBJ(callee) && OBJ_TYPE(callee) == OBJ_CLOSURE) {
          if (!call(AS_CLOSURE(callee), argCount)) {
            return INTERPRET_RUNTIME_ERROR;
          }
        } else {
          if (!callValue(callee, argCount)) {
            return INTERPRET_RUNTIME_ERROR;
          }
        }

        // Check if a yield occurred during the call
        if (vm.currentFiber && vm.currentFiber->state == FIBER_SUSPENDED) {
          return INTERPRET_OK;
        }

        frame = &vm.frames[vm.frameCount - 1];
        closure = frame->closure;
        slots = frame->slots;
        break;
      }

      case OP_CALL_SELF: {
        int argCount = (int)READ_BYTE();
        ObjClosure* callee = frame->closure;

        if (!insertCalleeBelowArgs(OBJ_VAL(callee), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        if (!call(callee, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        // Check if a yield occurred during the call
        if (vm.currentFiber && vm.currentFiber->state == FIBER_SUSPENDED) {
          return INTERPRET_OK;
        }

        frame = &vm.frames[vm.frameCount - 1];
        closure = frame->closure;
        slots = frame->slots;
        break;
      }

      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* newClosureObj = newClosure(function);
        push(OBJ_VAL(newClosureObj));
        for (int i = 0; i < newClosureObj->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (isLocal) {
            newClosureObj->upvalues[i] = captureUpvalue(slots + index);
          } else {
            newClosureObj->upvalues[i] = closure->upvalues[index];
          }
        }
        break;
      }

      case OP_CLOSURE_LONG: {
        uint16_t index = READ_SHORT();
        ObjFunction* function = AS_FUNCTION(frame->closure->function->chunk.constants.values[index]);
        ObjClosure* newClosureObj = newClosure(function);
        push(OBJ_VAL(newClosureObj));
        // Upvalue encoding is the same regardless of short/long closure opcode
        for (int i = 0; i < newClosureObj->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (isLocal) {
            newClosureObj->upvalues[i] = captureUpvalue(slots + index);
          } else {
            newClosureObj->upvalues[i] = closure->upvalues[index];
          }
        }
        break;
      }

      case OP_CLOSE_UPVALUE:
        closeUpvalues(vm.stackTop - 1);
        pop();
        break;

      case OP_UNWIND: {
        uint8_t count = READ_BYTE();
        uint8_t keep = READ_BYTE();
        if (keep > 1) {
          runtimeError("Invalid UNWIND keep flag: %d (must be 0 or 1)", keep);
          return INTERPRET_RUNTIME_ERROR;
        }
        // Optimize UNWIND 0 K as noop: count=0 means no locals to pop,
        // so no upvalues to close. For keep=1, pop+push cancels out.
        // For keep=0, stackTop unchanged and closeUpvalues finds nothing.
        if (count == 0) {
          break;
        }
        if (keep == 0) {
          Value* newTop = vm.stackTop - count;
          closeUpvalues(newTop);
          vm.stackTop = newTop;
        } else {
          Value top = pop();
          Value* newTop = vm.stackTop - count;
          closeUpvalues(newTop);
          vm.stackTop = newTop;
          push(top);
        }
        break;
      }

      case OP_ADD_LOCAL_IMM: {
        // Superinstruction: i += imm (statement-only, no value on stack)
        uint8_t slot = READ_BYTE();
        uint8_t imm = READ_BYTE();
        Value local = slots[slot];
        if (!IS_NUMBER(local)) {
          runtimeError("ADD_LOCAL_IMM operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Value result;
        if (IS_FIXNUM(local)) {
          int64_t a = AS_FIXNUM(local);
          int64_t r;
          if (__builtin_add_overflow(a, (int64_t)imm, &r)) {
            result = NUMBER_VAL((double)a + (double)imm);
          } else if (fixnumFitsInt64(r)) {
            result = FIXNUM_VAL(r);
          } else {
            result = NUMBER_VAL((double)r);
          }
        } else {
          result = NUMBER_VAL(AS_NUMBER(local) + (double)imm);
        }
        slots[slot] = result;
        break;
      }

      case OP_STORE_LOCAL: {
        // Superinstruction: SET_LOCAL + POP
        uint8_t slot = READ_BYTE();
        slots[slot] = pop();
        break;
      }

      case OP_GETI: {
        // Superinstruction: GET_LOCAL + GET_LOCAL + GET_BY_INDEX
        uint8_t arrSlot = READ_BYTE();
        uint8_t idxSlot = READ_BYTE();

        Value object = slots[arrSlot];
        Value key = slots[idxSlot];
        Value result;
        if (!getByIndexImpl(object, key, &result)) return INTERPRET_RUNTIME_ERROR;
        push(result);
        break;
      }

      case OP_SETI: {
        // Superinstruction: arr[idx] = val (statement-only, no value on stack)
        uint8_t arrSlot = READ_BYTE();
        uint8_t idxSlot = READ_BYTE();
        uint8_t valSlot = READ_BYTE();

        Value object = slots[arrSlot];
        Value key = slots[idxSlot];
        Value value = slots[valSlot];
        Value result;
        if (!setByIndexImpl(object, key, value, &result)) return INTERPRET_RUNTIME_ERROR;
        break;
      }

      case OP_ADD_LOCALS: {
        // Superinstruction: dest = lhs + rhs (statement-only, no value on stack)
        uint8_t destSlot = READ_BYTE();
        uint8_t lhsSlot = READ_BYTE();
        uint8_t rhsSlot = READ_BYTE();

        Value lhs = slots[lhsSlot];
        Value rhs = slots[rhsSlot];
        Value result;

        // Optimize for fixnums
        if (IS_FIXNUM(lhs) && IS_FIXNUM(rhs)) {
          int64_t a = AS_FIXNUM(lhs);
          int64_t b = AS_FIXNUM(rhs);
          int64_t r;
          if (__builtin_add_overflow(a, b, &r)) {
            result = NUMBER_VAL((double)a + (double)b);
          } else {
            pushInt64AsNumberOrFlonum(r);
            result = pop();
          }
        } else if (IS_NUMBER(lhs) && IS_NUMBER(rhs)) {
          result = NUMBER_VAL(AS_NUMBER(lhs) + AS_NUMBER(rhs));
        } else {
          runtimeError("ADD_LOCALS operands must be numbers.");
          return INTERPRET_RUNTIME_ERROR;
        }

        slots[destSlot] = result;
        break;
      }

      case OP_SUB_LOCALS: {
        // Superinstruction: dest = lhs - rhs (statement-only, no value on stack)
        uint8_t destSlot = READ_BYTE();
        uint8_t lhsSlot = READ_BYTE();
        uint8_t rhsSlot = READ_BYTE();

        Value lhs = slots[lhsSlot];
        Value rhs = slots[rhsSlot];
        Value result;

        // Optimize for fixnums
        if (IS_FIXNUM(lhs) && IS_FIXNUM(rhs)) {
          int64_t a = AS_FIXNUM(lhs);
          int64_t b = AS_FIXNUM(rhs);
          int64_t r;
          if (__builtin_sub_overflow(a, b, &r)) {
            result = NUMBER_VAL((double)a - (double)b);
          } else {
            pushInt64AsNumberOrFlonum(r);
            result = pop();
          }
        } else if (IS_NUMBER(lhs) && IS_NUMBER(rhs)) {
          result = NUMBER_VAL(AS_NUMBER(lhs) - AS_NUMBER(rhs));
        } else {
          runtimeError("SUB_LOCALS operands must be numbers.");
          return INTERPRET_RUNTIME_ERROR;
        }

        slots[destSlot] = result;
        break;
      }

      case OP_MUL_LOCALS: {
        // Superinstruction: dest = lhs * rhs (statement-only, no value on stack)
        uint8_t destSlot = READ_BYTE();
        uint8_t lhsSlot = READ_BYTE();
        uint8_t rhsSlot = READ_BYTE();

        Value lhs = slots[lhsSlot];
        Value rhs = slots[rhsSlot];
        Value result;

        // Optimize for fixnums
        if (IS_FIXNUM(lhs) && IS_FIXNUM(rhs)) {
          int64_t a = AS_FIXNUM(lhs);
          int64_t b = AS_FIXNUM(rhs);
          int64_t r;
          if (__builtin_mul_overflow(a, b, &r)) {
            result = NUMBER_VAL((double)a * (double)b);
          } else {
            pushInt64AsNumberOrFlonum(r);
            result = pop();
          }
        } else if (IS_NUMBER(lhs) && IS_NUMBER(rhs)) {
          result = NUMBER_VAL(AS_NUMBER(lhs) * AS_NUMBER(rhs));
        } else {
          runtimeError("MUL_LOCALS operands must be numbers.");
          return INTERPRET_RUNTIME_ERROR;
        }

        slots[destSlot] = result;
        break;
      }

      case OP_DIV_LOCALS: {
        // Superinstruction: dest = lhs / rhs (statement-only, no value on stack)
        uint8_t destSlot = READ_BYTE();
        uint8_t lhsSlot = READ_BYTE();
        uint8_t rhsSlot = READ_BYTE();

        Value lhs = slots[lhsSlot];
        Value rhs = slots[rhsSlot];
        Value result;

        // Division always produces floating point result in lx
        if (IS_NUMBER(lhs) && IS_NUMBER(rhs)) {
          result = NUMBER_VAL(AS_NUMBER(lhs) / AS_NUMBER(rhs));
        } else {
          runtimeError("DIV_LOCALS operands must be numbers.");
          return INTERPRET_RUNTIME_ERROR;
        }

        slots[destSlot] = result;
        break;
      }

      case OP_ADD_LOCAL_K: {
        // Superinstruction: push(local + k)
        uint8_t slot = READ_BYTE();
        uint8_t k = READ_BYTE();
        Value local = slots[slot];
        if (!IS_NUMBER(local)) {
          runtimeError("ADD_LOCAL_K operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Value result;
        if (IS_FIXNUM(local)) {
          int64_t a = AS_FIXNUM(local);
          int64_t r;
          if (__builtin_add_overflow(a, (int64_t)k, &r)) {
            result = NUMBER_VAL((double)a + (double)k);
          } else if (fixnumFitsInt64(r)) {
            result = FIXNUM_VAL(r);
          } else {
            result = NUMBER_VAL((double)r);
          }
        } else {
          result = NUMBER_VAL(AS_NUMBER(local) + (double)k);
        }
        push(result);
        break;
      }

      case OP_SUB_LOCAL_K: {
        // Superinstruction: push(local - k)
        uint8_t slot = READ_BYTE();
        uint8_t k = READ_BYTE();
        Value local = slots[slot];
        if (!IS_NUMBER(local)) {
          runtimeError("SUB_LOCAL_K operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Value result;
        if (IS_FIXNUM(local)) {
          int64_t a = AS_FIXNUM(local);
          int64_t r;
          if (__builtin_sub_overflow(a, (int64_t)k, &r)) {
            result = NUMBER_VAL((double)a - (double)k);
          } else if (fixnumFitsInt64(r)) {
            result = FIXNUM_VAL(r);
          } else {
            result = NUMBER_VAL((double)r);
          }
        } else {
          result = NUMBER_VAL(AS_NUMBER(local) - (double)k);
        }
        push(result);
        break;
      }

      case OP_MUL_LOCAL_K: {
        // Superinstruction: push(local * k)
        uint8_t slot = READ_BYTE();
        uint8_t k = READ_BYTE();
        Value local = slots[slot];
        if (!IS_NUMBER(local)) {
          runtimeError("MUL_LOCAL_K operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Value result;
        if (IS_FIXNUM(local)) {
          int64_t a = AS_FIXNUM(local);
          int64_t r;
          if (__builtin_mul_overflow(a, (int64_t)k, &r)) {
            result = NUMBER_VAL((double)a * (double)k);
          } else if (fixnumFitsInt64(r)) {
            result = FIXNUM_VAL(r);
          } else {
            result = NUMBER_VAL((double)r);
          }
        } else {
          result = NUMBER_VAL(AS_NUMBER(local) * (double)k);
        }
        push(result);
        break;
      }

      case OP_DIV_LOCAL_K: {
        // Superinstruction: push(local / k)
        uint8_t slot = READ_BYTE();
        uint8_t k = READ_BYTE();
        Value local = slots[slot];
        if (!IS_NUMBER(local)) {
          runtimeError("DIV_LOCAL_K operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        if (k == 0) {
          runtimeError("Division by zero.");
          return INTERPRET_RUNTIME_ERROR;
        }
        // Division always produces floating point result in lx
        // Convert fixnum to double if needed
        double a = IS_FIXNUM(local) ? (double)AS_FIXNUM(local) : AS_NUMBER(local);
        Value result = NUMBER_VAL(a / (double)k);
        push(result);
        break;
      }

      case OP_CMP_LOCAL_K: {
        // Superinstruction: push(local cmp k)
        uint8_t slot = READ_BYTE();
        uint8_t k = READ_BYTE();
        uint8_t cmp_kind = READ_BYTE();
        Value local = slots[slot];

        if (!IS_NUMBER(local)) {
          runtimeError("CMP_LOCAL_K operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }

        // Convert both to double for comparison
        double a = IS_FIXNUM(local) ? (double)AS_FIXNUM(local) : AS_NUMBER(local);
        double b = (double)k;
        bool result = false;

        switch (cmp_kind) {
          case CMP_LT: result = (a < b); break;   // <
          case CMP_LE: result = (a <= b); break;  // <=
          case CMP_GT: result = (a > b); break;   // >
          case CMP_GE: result = (a >= b); break;  // >=
          case CMP_EQ: result = (a == b); break;  // ==
          case CMP_NE: result = (a != b); break;  // !=
          default:
            runtimeError("Invalid comparison kind in CMP_LOCAL_K.");
            return INTERPRET_RUNTIME_ERROR;
        }

        push(BOOL_VAL(result));
        break;
      }

      case OP_GET_PROPERTY: {
        // Superinstruction: GET_LOCAL + CONSTANT + GET_BY_INDEX
        uint8_t objSlot = READ_BYTE();
        uint8_t constIdx = READ_BYTE();

        Value object = slots[objSlot];
        Value key = frame->closure->function->chunk.constants.values[constIdx];
        Value result;
        if (!getByIndexImpl(object, key, &result)) return INTERPRET_RUNTIME_ERROR;
        push(result);
        break;
      }

      case OP_SET_PROPERTY: {
        // Superinstruction: obj.field = val or obj[const] = val (statement-only, no value on stack)
        uint8_t objSlot = READ_BYTE();
        uint8_t constIdx = READ_BYTE();
        uint8_t valSlot = READ_BYTE();

        Value object = slots[objSlot];
        Value key = frame->closure->function->chunk.constants.values[constIdx];
        Value value = slots[valSlot];
        Value result;
        if (!setByIndexImpl(object, key, value, &result)) return INTERPRET_RUNTIME_ERROR;
        break;
      }

      case OP_COALESCE_CONST: {
        // Replace TOS with constant if TOS is falsy (defaulting/fallback operation)
        uint8_t constantIdx = READ_BYTE();
        Value tos = peek(0);
        if (isFalsey(tos)) {
          pop();
          push(frame->closure->function->chunk.constants.values[constantIdx]);
        }
        break;
      }

      case OP_COALESCE_CONST_LONG: {
        uint16_t index = READ_SHORT();
        Value tos = peek(0);
        if (isFalsey(tos)) {
          pop();
          push(frame->closure->function->chunk.constants.values[index]);
        }
        break;
      }

      case OP_MOD_CONST_BYTE: {
        // TOS = TOS % imm8 (specialized modulo with constant)
        uint8_t modulus = READ_BYTE();
        int64_t a;
        if (!valueToInt64Exact(pop(), &a, "Left operand of %")) return INTERPRET_RUNTIME_ERROR;
        if (modulus == 0) {
          runtimeError("Division by zero.");
          return INTERPRET_RUNTIME_ERROR;
        }
        int64_t r = a % modulus;
        if (!pushInt64AsNumber(r, "%")) return INTERPRET_RUNTIME_ERROR;
        break;
      }

      case OP_EQ_CONST_BYTE: {
        // TOS = (TOS == imm8) (specialized equality with constant)
        uint8_t constant = READ_BYTE();
        Value value = pop();
        // Fast path for fixnum comparison
        if (IS_FIXNUM(value)) {
          push(BOOL_VAL(AS_FIXNUM(value) == (int64_t)constant));
        } else if (IS_NUMBER(value)) {
          push(BOOL_VAL(AS_NUMBER(value) == (double)constant));
        } else {
          // For non-numeric values, they're never equal to a numeric constant
          push(BOOL_VAL(false));
        }
        break;
      }

      case OP_FORPREP_1: {
        // Numeric for loop prepare (step=1)
        // Operands: i_slot(u8) limit_slot(u8) cmp_kind(u8) offset(u16)
        uint8_t i_slot = READ_BYTE();
        uint8_t limit_slot = READ_BYTE();
        uint8_t cmp_kind = READ_BYTE();  // 0=LT, 1=LE
        uint16_t offset = READ_SHORT();

        Value i = slots[i_slot];
        Value limit = slots[limit_slot];

        // Require i to be fixnum
        if (!IS_FIXNUM(i)) {
          runtimeError("Loop variable must be an integer.");
          return INTERPRET_RUNTIME_ERROR;
        }

        // Require limit to be numeric (fixnum or double)
        if (!IS_NUMBER(limit)) {
          runtimeError("Loop limit must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }

        // Check initial entry condition
        bool shouldEnter = false;
        if (IS_FIXNUM(limit)) {
          int64_t i_int = AS_FIXNUM(i);
          int64_t limit_int = AS_FIXNUM(limit);
          shouldEnter = (cmp_kind == 0) ? (i_int < limit_int) : (i_int <= limit_int);
        } else {
          double i_double = (double)AS_FIXNUM(i);
          double limit_double = AS_NUMBER(limit);
          shouldEnter = (cmp_kind == 0) ? (i_double < limit_double) : (i_double <= limit_double);
        }

        if (!shouldEnter) {
          // Skip loop body
          frame->ip += offset;
        }
        // Otherwise fall through to loop body
        break;
      }

      case OP_FORLOOP_1: {
        // Numeric for loop iterate (step=1)
        // Operands: i_slot(u8) limit_slot(u8) cmp_kind(u8) offset(u16)
        uint8_t i_slot = READ_BYTE();
        uint8_t limit_slot = READ_BYTE();
        uint8_t cmp_kind = READ_BYTE();  // 0=LT, 1=LE
        uint16_t offset = READ_SHORT();

        Value i = slots[i_slot];
        Value limit = slots[limit_slot];

        // i must remain fixnum throughout loop
        if (!IS_FIXNUM(i)) {
          runtimeError("Loop variable corrupted (must be integer).");
          return INTERPRET_RUNTIME_ERROR;
        }

        int64_t i_int = AS_FIXNUM(i);

        // Increment with overflow check
        if (i_int == FIXNUM_MAX) {
          runtimeError("Loop variable overflow.");
          return INTERPRET_RUNTIME_ERROR;
        }
        i_int++;

        // Store incremented value
        slots[i_slot] = FIXNUM_VAL(i_int);

        // Check loop condition
        bool shouldContinue = false;
        if (IS_FIXNUM(limit)) {
          int64_t limit_int = AS_FIXNUM(limit);
          shouldContinue = (cmp_kind == 0) ? (i_int < limit_int) : (i_int <= limit_int);
        } else if (IS_NUMBER(limit)) {
          double i_double = (double)i_int;
          double limit_double = AS_NUMBER(limit);
          shouldContinue = (cmp_kind == 0) ? (i_double < limit_double) : (i_double <= limit_double);
        } else {
          runtimeError("Loop limit must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }

        if (shouldContinue) {
          // Jump back to loop body
          frame->ip -= offset;
        }
        // Otherwise exit loop
        break;
      }

      case OP_FORPREP: {
        // Numeric for loop prepare (parametric step)
        // Operands: i_slot(u8) limit_slot(u8) cmp_kind(u8) step(i8) offset(u16)
        uint8_t i_slot = READ_BYTE();
        uint8_t limit_slot = READ_BYTE();
        uint8_t cmp_kind = READ_BYTE();  // 0=<, 1=<=, 2=>, 3=>=
        int8_t step = (int8_t)READ_BYTE();  // Signed step (-128 to 127)
        (void)step;  // Step not used in FORPREP, only needed to advance IP
        uint16_t offset = READ_SHORT();

        Value i = slots[i_slot];
        Value limit = slots[limit_slot];

        // Validate types
        if (!IS_FIXNUM(i)) {
          runtimeError("For loop variable must be a fixnum.");
          return INTERPRET_RUNTIME_ERROR;
        }
        if (!IS_NUMBER(limit)) {
          runtimeError("For loop limit must be numeric.");
          return INTERPRET_RUNTIME_ERROR;
        }

        // Check initial entry condition
        int64_t i_int = AS_FIXNUM(i);
        double limit_dbl = AS_NUMBER(limit);
        bool shouldEnter = false;

        switch (cmp_kind) {
          case CMP_LT: shouldEnter = ((double)i_int < limit_dbl); break;   // <
          case CMP_LE: shouldEnter = ((double)i_int <= limit_dbl); break;  // <=
          case CMP_GT: shouldEnter = ((double)i_int > limit_dbl); break;   // >
          case CMP_GE: shouldEnter = ((double)i_int >= limit_dbl); break;  // >=
          default:
            runtimeError("Invalid comparison kind in FORPREP.");
            return INTERPRET_RUNTIME_ERROR;
        }

        if (!shouldEnter) {
          // Skip loop body by jumping forward
          frame->ip += offset;
        }
        // Otherwise fall through to loop body
        break;
      }

      case OP_FORLOOP: {
        // Numeric for loop iterate (parametric step)
        // Operands: i_slot(u8) limit_slot(u8) cmp_kind(u8) step(i8) offset(u16)
        uint8_t i_slot = READ_BYTE();
        uint8_t limit_slot = READ_BYTE();
        uint8_t cmp_kind = READ_BYTE();  // 0=<, 1=<=, 2=>, 3=>=
        int8_t step = (int8_t)READ_BYTE();  // Signed step
        uint16_t offset = READ_SHORT();

        Value i = slots[i_slot];
        Value limit = slots[limit_slot];

        // Extract i as int64
        if (!IS_FIXNUM(i)) {
          runtimeError("Loop variable corrupted (must be fixnum).");
          return INTERPRET_RUNTIME_ERROR;
        }
        int64_t i_int = AS_FIXNUM(i);

        // Increment with signed step
        i_int += step;

        // Check for fixnum overflow
        if (!fixnumFitsInt64(i_int)) {
          runtimeError("For loop counter overflow.");
          return INTERPRET_RUNTIME_ERROR;
        }

        // Store incremented value
        slots[i_slot] = FIXNUM_VAL(i_int);

        // Check loop continuation condition
        if (!IS_NUMBER(limit)) {
          runtimeError("Loop limit must be numeric.");
          return INTERPRET_RUNTIME_ERROR;
        }
        double limit_dbl = AS_NUMBER(limit);
        bool shouldContinue = false;

        switch (cmp_kind) {
          case CMP_LT: shouldContinue = ((double)i_int < limit_dbl); break;   // <
          case CMP_LE: shouldContinue = ((double)i_int <= limit_dbl); break;  // <=
          case CMP_GT: shouldContinue = ((double)i_int > limit_dbl); break;   // >
          case CMP_GE: shouldContinue = ((double)i_int >= limit_dbl); break;  // >=
          default:
            runtimeError("Invalid comparison kind in FORLOOP.");
            return INTERPRET_RUNTIME_ERROR;
        }

        if (shouldContinue) {
          // Jump backward to loop body
          frame->ip -= offset;
        }
        // Otherwise exit loop
        break;
      }

      case OP_YIELD: {
        uint8_t yieldCount = READ_BYTE();

        // Validate: can only yield inside a fiber
        if (!vm.currentFiber) {
          runtimeError("Cannot yield outside of a fiber.");
          return INTERPRET_RUNTIME_ERROR;
        }

        // Validate: can't yield from non-yieldable context (native calls)
        if (vm.nonYieldableDepth > 0) {
          runtimeError("Cannot yield from non-yieldable context.");
          return INTERPRET_RUNTIME_ERROR;
        }

        // For MVP, only support yielding 1 value
        if (yieldCount != 1) {
          runtimeError("Fiber yield currently only supports 1 value.");
          return INTERPRET_RUNTIME_ERROR;
        }

        // Pop the yielded value and store it for the resumer
        Value yieldedValue = pop();
        vm.lastResult = yieldedValue;

        // Save current state to fiber
        syncFromVM();

        // Transition fiber to SUSPENDED state
        vm.currentFiber->state = FIBER_SUSPENDED;

        // Return to caller (fiberResume will handle switching back)
        return INTERPRET_OK;
      }

      case OP_RETURN: {
        Value result = pop();
        closeUpvalues(frame->slots);
        vm.frameCount--;

        if (vm.frameCount == 0) {
          vm.lastResult = result;
          vm.stackTop = vm.stack;
          vm.openUpvalues = NULL;

          // If returning from fiber's top level, mark fiber as done
          if (vm.currentFiber) {
            vm.currentFiber->state = FIBER_DONE;
          }

          return INTERPRET_OK;
        }

        vm.stackTop = frame->slots;
        push(result);

        if (vm.frameCount == stopFrameCount) {
          return INTERPRET_OK;
        }

        frame = &vm.frames[vm.frameCount - 1];
        closure = frame->closure;
        slots = frame->slots;
        break;
      }

      default:
        runtimeError("Invalid opcode %d.", (int)op);
        return INTERPRET_RUNTIME_ERROR;
    }
  }

  #undef READ_BYTE
  #undef READ_SHORT
  #undef READ_CONSTANT
  #undef READ_STRING
  #undef BINARY_OP
  #undef BIT_BINARY_OP
}

InterpretResult interpret(uint8_t* obj) {
  ObjFunction* function = loadObj(obj, false);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  push(OBJ_VAL(function));
  ObjClosure* closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  call(closure, 0);

  return runUntil(0);
}
