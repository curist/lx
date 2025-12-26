#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

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
  // XXX: we probably won't have classes?
  runtimeError("Can only call functions and classes.");
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

static InterpretResult run(void) {
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
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                \
      runtimeError("Operands must be numbers.");                     \
      return INTERPRET_RUNTIME_ERROR;                                \
    }                                                                \
    double b = AS_NUMBER(pop());                                     \
    double a = AS_NUMBER(pop());                                     \
    push(valueType(a op_ b));                                        \
  } while (false)

#define BIT_BINARY_OP(op_)                                           \
  do {                                                               \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                \
      runtimeError("Operands must be numbers.");                     \
      return INTERPRET_RUNTIME_ERROR;                                \
    }                                                                \
    int b = (int)AS_NUMBER(pop());                                   \
    int a = (int)AS_NUMBER(pop());                                   \
    push(NUMBER_VAL(a op_ b));                                       \
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
        push(NUMBER_VAL(READ_BYTE()));
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
        if (!IS_HASHMAP(peek(1)) && !IS_ARRAY(peek(1)) && !IS_STRING(peek(1))) {
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
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
          runtimeError("Operands must be numbers.");
          return INTERPRET_RUNTIME_ERROR;
        }
        int b = (int)AS_NUMBER(pop());
        int a = (int)AS_NUMBER(pop());
        push(NUMBER_VAL(a % b));
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

      case OP_BIT_AND:
        BIT_BINARY_OP(&);
        break;

      case OP_BIT_OR:
        BIT_BINARY_OP(|);
        break;

      case OP_BIT_XOR:
        BIT_BINARY_OP(^);
        break;

      case OP_BIT_LSHIFT:
        BIT_BINARY_OP(<<);
        break;

      case OP_BIT_RSHIFT:
        BIT_BINARY_OP(>>);
        break;

      case OP_ASSOC: {
        Value hashmap = peek(2);
        Value key     = peek(1);
        Value value   = peek(0);

        if (!IS_HASHMAP(hashmap)) {
          runtimeError("Can only assoc to hashmap.");
          return INTERPRET_RUNTIME_ERROR;
        }
        if (!IS_NUMBER(key) && !IS_STRING(key)) {
          runtimeError("Hashmap key type must be number or string.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Table* table = &AS_HASHMAP(hashmap);
        tableSet(table, key, value);

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
        Value result = NUMBER_VAL(AS_NUMBER(local) + (double)imm);
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

  return run();
}
