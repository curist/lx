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

VM vm;

static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
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
    resetStack();
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
  resetStack();
}

static bool valueToInt64Exact(Value value, int64_t* out, const char* context) {
  if (!IS_NUMBER(value)) {
    runtimeError("%s must be a number.", context);
    return false;
  }

  if (IS_FIXNUM(value)) {
    *out = AS_FIXNUM(value);
    return true;
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
  resetStack();
  vm.objects = NULL;
  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;

  vm.lastResult = NIL_VAL;
  vm.lastError = NIL_VAL;
  vm.errorJmp = NULL;

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

  if (argCount < arity) {
    runtimeError("Expected %d arguments but got %d.", arity, argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
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

      case OP_SWAP: {
        Value a = pop();
        Value b = pop();
        push(a);
        push(b);
        break;
      }

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

      case OP_GET_BY_INDEX: {
        if (!IS_ENUM(peek(1)) && !IS_HASHMAP(peek(1)) && !IS_ARRAY(peek(1)) && !IS_STRING(peek(1))) {
          runtimeError("Only array / hashmap / string can get value by index.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Value key = peek(0);

        if (IS_ARRAY(peek(1))) {
          if (!IS_NUMBER(key)) {
            runtimeError("Can only use number index to access array.");
            return INTERPRET_RUNTIME_ERROR;
          }
          int index = (int)AS_NUMBER(key);
          if ((double)index != AS_NUMBER(key)) {
            runtimeError("Can only use integer index to access array.");
            return INTERPRET_RUNTIME_ERROR;
          }

          ValueArray* array = &AS_ARRAY(peek(1));
          Value value = NIL_VAL;
          if (index >= 0 && index < array->count) {
            value = array->values[index];
          }
          pop(); // key
          pop(); // container
          push(value);

        } else if (IS_ENUM(peek(1))) {
          if (!IS_NUMBER(key) && !IS_STRING(key)) {
            runtimeError("Enum key type must be number or string.");
            return INTERPRET_RUNTIME_ERROR;
          }
          Table* table = &AS_ENUM_FORWARD(peek(1));
          Value value;
          if (!tableGet(table, key, &value)) value = NIL_VAL;
          pop(); // key
          pop(); // container
          push(value);

        } else if (IS_HASHMAP(peek(1))) {
          if (!IS_NUMBER(key) && !IS_STRING(key)) {
            runtimeError("Hashmap key type must be number or string.");
            return INTERPRET_RUNTIME_ERROR;
          }
          Table* table = &AS_HASHMAP(peek(1));
          Value value;
          if (!tableGet(table, key, &value)) value = NIL_VAL;
          pop(); // key
          pop(); // container
          push(value);

        } else {
          // string
          if (!IS_NUMBER(key)) {
            runtimeError("String index type must be a number.");
            return INTERPRET_RUNTIME_ERROR;
          }
          ObjString* s = AS_STRING(peek(1));
          char* ch = NULL;
          size_t index = (size_t)AS_NUMBER(key);
          if (index < s->length) {
            ch = &s->chars[index];
          }
          pop(); // key
          pop(); // string
          if (ch != NULL) push(OBJ_VAL(copyString(ch, 1)));
          else            push(NIL_VAL);
        }
        break;
      }

      case OP_SET_BY_INDEX: {
        if (IS_ENUM(peek(2))) {
          runtimeError("Enum is immutable.");
          return INTERPRET_RUNTIME_ERROR;
        }
        if (!IS_HASHMAP(peek(2)) && !IS_ARRAY(peek(2))) {
          runtimeError("Only array or hashmap can set value by index.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Value key = peek(1);
        Value value = peek(0);

        if (IS_ARRAY(peek(2))) {
          if (!IS_NUMBER(key)) {
            runtimeError("Can only use number index to access array.");
            return INTERPRET_RUNTIME_ERROR;
          }
          int index = (int)AS_NUMBER(key);
          if ((double)index != AS_NUMBER(key)) {
            runtimeError("Can only use integer index to access array.");
            return INTERPRET_RUNTIME_ERROR;
          }

          ValueArray* array = &AS_ARRAY(peek(2));
          if (index >= 0 && index < array->count) {
            array->values[index] = value;
          } else {
            value = NIL_VAL;
          }
          pop(); // value
          pop(); // key
          pop(); // container
          push(value);

        } else {
          if (!IS_NUMBER(key) && !IS_STRING(key)) {
            runtimeError("Hashmap key type must be number or string.");
            return INTERPRET_RUNTIME_ERROR;
          }
          Table* table = &AS_HASHMAP(peek(2));
          tableSet(table, key, value);
          pop(); // value
          pop(); // key
          pop(); // container
          push(value);
        }
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
        // Superinstruction: GET_LOCAL + CONST_BYTE + ADD + SET_LOCAL
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
        push(result); // Like SET_LOCAL, leaves value on stack
        break;
      }

      case OP_STORE_LOCAL: {
        // Superinstruction: SET_LOCAL + POP
        uint8_t slot = READ_BYTE();
        slots[slot] = pop();
        break;
      }

      case OP_STORE_BY_IDX: {
        // Superinstruction: GET_LOCAL + GET_LOCAL + GET_LOCAL + SET_BY_INDEX
        uint8_t arrSlot = READ_BYTE();
        uint8_t idxSlot = READ_BYTE();
        uint8_t valSlot = READ_BYTE();

        Value object = slots[arrSlot];
        Value key = slots[idxSlot];
        Value value = slots[valSlot];

        if (IS_ENUM(object)) {
          runtimeError("Enum is immutable.");
          return INTERPRET_RUNTIME_ERROR;
        }
        if (!IS_HASHMAP(object) && !IS_ARRAY(object)) {
          runtimeError("Only array or hashmap can set value by index.");
          return INTERPRET_RUNTIME_ERROR;
        }

        if (IS_ARRAY(object)) {
          if (!IS_NUMBER(key)) {
            runtimeError("Can only use number index to access array.");
            return INTERPRET_RUNTIME_ERROR;
          }
          int index = (int)AS_NUMBER(key);
          if ((double)index != AS_NUMBER(key)) {
            runtimeError("Can only use integer index to access array.");
            return INTERPRET_RUNTIME_ERROR;
          }

          ValueArray* array = &AS_ARRAY(object);
          if (index >= 0 && index < array->count) {
            array->values[index] = value;
          } else {
            value = NIL_VAL;
          }
          push(value);
        } else {
          if (!IS_NUMBER(key) && !IS_STRING(key)) {
            runtimeError("Hashmap key type must be number or string.");
            return INTERPRET_RUNTIME_ERROR;
          }
          Table* table = &AS_HASHMAP(object);
          tableSet(table, key, value);
          push(value);
        }
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

      case OP_IS_EVEN: {
        // Test if TOS integer is even (hot-path for x % 2 == 0)
        // Uses same truncation semantics as OP_MOD for consistency
        Value value = pop();
        if (!IS_NUMBER(value)) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        double num = AS_NUMBER(value);
        int intVal = (int)num;  // Truncate to int like MOD does
        push(BOOL_VAL((intVal & 1) == 0));
        break;
      }

      case OP_RETURN: {
        Value result = pop();
        closeUpvalues(frame->slots);
        vm.frameCount--;

        if (vm.frameCount == 0) {
          vm.lastResult = result;
          vm.stackTop = vm.stack;
          vm.openUpvalues = NULL;
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
