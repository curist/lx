#include <stdarg.h>
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
  vm.localsTop = vm.locals;
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
  freeTable(&vm.globals);
  freeTable(&vm.strings);
  freeObjects();
}

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}

static Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}

static void push_local(Value value) {
  *vm.localsTop = value;
  vm.localsTop++;
}

static Value pop_local() {
  vm.localsTop--;
  return *vm.localsTop;
}

static Value peek_local(int distance) {
  return vm.localsTop[-1 - distance];
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

  // Discard extra args from locals
  for (int i = 0; i < argCount - arity; i++) {
    pop_local();
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.localsTop - arity - 1;  // Points into vm.locals
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_CLOSURE:
        return call(AS_CLOSURE(callee), argCount);
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee)->function;
        if (native(argCount, vm.localsTop - argCount)) {
          vm.localsTop -= argCount;
          push(pop_local());
          return true;
        } else {
          runtimeError(AS_STRING(vm.localsTop[-argCount - 1])->chars);
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

static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static bool isFalsey(Value value) {
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

static InterpretResult run() {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];
  ObjClosure* closure = frame->closure;
  Value* slots = frame->slots;

  static void* dispatch_table[] = {
    &&DO_OP_NOP,
    &&DO_OP_CONSTANT,
    &&DO_OP_CONST_BYTE,
    &&DO_OP_NIL,
    &&DO_OP_TRUE,
    &&DO_OP_FALSE,
    &&DO_OP_EQUAL,
    &&DO_OP_POP,
    &&DO_OP_DUP,
    &&DO_OP_SWAP,
    &&DO_OP_NEW_LOCAL,
    &&DO_OP_POP_LOCAL,
    &&DO_OP_GET_LOCAL,
    &&DO_OP_SET_LOCAL,
    &&DO_OP_GET_GLOBAL,
    &&DO_OP_DEFINE_GLOBAL,
    &&DO_OP_SET_GLOBAL,
    &&DO_OP_GET_UPVALUE,
    &&DO_OP_SET_UPVALUE,
    &&DO_OP_GET_BY_INDEX,
    &&DO_OP_SET_BY_INDEX,
    &&DO_OP_GREATER,
    &&DO_OP_LESS,
    &&DO_OP_ADD,
    &&DO_OP_SUBTRACT,
    &&DO_OP_MULTIPLY,
    &&DO_OP_DIVIDE,
    &&DO_OP_NOT,
    &&DO_OP_MOD,
    &&DO_OP_NEGATE,
    &&DO_OP_BIT_AND,
    &&DO_OP_BIT_OR,
    &&DO_OP_BIT_XOR,
    &&DO_OP_BIT_LSHIFT,
    &&DO_OP_BIT_RSHIFT,
    &&DO_OP_JUMP,
    &&DO_OP_JUMP_IF_TRUE,
    &&DO_OP_JUMP_IF_FALSE,
    &&DO_OP_LOOP,
    &&DO_OP_ASSOC,
    &&DO_OP_APPEND,
    &&DO_OP_HASHMAP,
    &&DO_OP_ARRAY,
    &&DO_OP_LENGTH,
    &&DO_OP_CALL,
    &&DO_OP_CLOSURE,
    &&DO_OP_CLOSE_UPVALUE,
    &&DO_OP_RETURN,
  };

#ifdef DEBUG_TRACE_EXECUTION
#define DISPATCH() goto DO_DEBUG_PRINT
#else
#define DISPATCH() goto *dispatch_table[(*frame->ip++)]
#endif
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, \
    (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)
#define BIT_BINARY_OP(op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      int b = AS_NUMBER(pop()); \
      int a = AS_NUMBER(pop()); \
      push(NUMBER_VAL(a op b)); \
    } while (false)

  DISPATCH();
  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
DO_DEBUG_PRINT:
    printf("        |       \x1b[1;32m[ ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printValue(stdout, *slot);
      printf(" ");
    }
    printf("]\x1b[0m\n");
    printf("        |     \x1b[0;33mL:[ ");
    for (Value* slot = vm.locals; slot < vm.localsTop; slot++) {
      printValue(stdout, *slot);
      printf(" ");
    }
    printf("]\x1b[0m\n");
    disassembleInstruction(&frame->closure->function->chunk,
        (int)(ip - frame->closure->function->chunk.code), false);
    goto *dispatch_table[(*ip++)];
#endif
DO_OP_NOP:
    DISPATCH();
DO_OP_CONSTANT:
    push(READ_CONSTANT());
    DISPATCH();
DO_OP_CONST_BYTE:
    push(NUMBER_VAL(READ_BYTE()));
    DISPATCH();
DO_OP_NIL:
    push(NIL_VAL);
    DISPATCH();
DO_OP_TRUE:
    push(BOOL_VAL(true));
    DISPATCH();
DO_OP_FALSE:
    push(BOOL_VAL(false));
    DISPATCH();
DO_OP_POP:
    pop();
    DISPATCH();
DO_OP_DUP:
    push(peek(0));
    DISPATCH();
DO_OP_SWAP:
    {
      Value a = pop();
      Value b = pop();
      push(a);
      push(b);
      DISPATCH();
    }
DO_OP_NEW_LOCAL:
    push_local(pop());
    DISPATCH();
DO_OP_POP_LOCAL:
    pop_local();
    DISPATCH();
DO_OP_GET_LOCAL:
    push(slots[READ_BYTE()]);
    DISPATCH();
DO_OP_SET_LOCAL:
    slots[READ_BYTE()] = peek(0);
    DISPATCH();
DO_OP_GET_GLOBAL:
    {
      ObjString* name = READ_STRING();
      Value value;
      if (!tableGet(&vm.globals, OBJ_VAL(name), &value)) {
        runtimeError("Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
      push(value);
      DISPATCH();
    }
DO_OP_DEFINE_GLOBAL:
    {
      ObjString* name = READ_STRING();
      tableSet(&vm.globals, OBJ_VAL(name), peek(0));
      pop();
      DISPATCH();
    }
DO_OP_SET_GLOBAL:
    {
      ObjString* name = READ_STRING();
      if (tableSet(&vm.globals, OBJ_VAL(name), peek(0))) {
        tableDelete(&vm.globals, OBJ_VAL(name));
        runtimeError("Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
      DISPATCH();
    }
DO_OP_GET_UPVALUE:
    {
      uint8_t slot = READ_BYTE();
      push(*closure->upvalues[slot]->location);
      DISPATCH();
    }
DO_OP_SET_UPVALUE:
    {
      uint8_t slot = READ_BYTE();
      *closure->upvalues[slot]->location = peek(0);
      DISPATCH();
    }
DO_OP_GET_BY_INDEX:
    {
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
        int index = AS_NUMBER(key);
        if (index != AS_NUMBER(key)) {
          // check if the number is an int
          runtimeError("Can only use integer index to access array.");
          return INTERPRET_RUNTIME_ERROR;

        }

        ValueArray* array = &AS_ARRAY(peek(1));
        Value value = NIL_VAL;
        if (index >= 0 && index < array->count) {
          value = array->values[index];
        }
        pop();
        pop();
        push(value);

      } else if (IS_HASHMAP(peek(1))){
        if (!IS_NUMBER(key) && !IS_STRING(key)) {
          runtimeError("Hashmap key type must be number or string.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Table* table = &AS_HASHMAP(peek(1));
        Value value;
        if (!tableGet(table, key, &value)) {
          value = NIL_VAL;
        }
        pop();
        pop();
        push(value);
      } else {
        // is string
        if (!IS_NUMBER(key)) {
          runtimeError("String index type must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* s = AS_STRING(peek(1));
        char* ch = NULL;
        size_t index = AS_NUMBER(key);
        if (index < s->length) {
          ch = &AS_STRING(peek(1))->chars[index];
        }
        pop();
        pop();
        if (ch != NULL) {
          push(OBJ_VAL(copyString(ch, 1)));
        } else {
          push(NIL_VAL);
        }
      }
      DISPATCH();
    }
DO_OP_SET_BY_INDEX:
    {
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
        int index = AS_NUMBER(key);
        if (index != AS_NUMBER(key)) {
          // check if the number is an int
          runtimeError("Can only use integer index to access array.");
          return INTERPRET_RUNTIME_ERROR;

        }

        ValueArray* array = &AS_ARRAY(peek(2));
        if (index >= 0 && index < array->count) {
          array->values[index] = value;
        } else {
          value = NIL_VAL;
        }
        pop();
        pop();
        pop();
        push(value);

      } else {
        // is hashmap
        if (!IS_NUMBER(key) && !IS_STRING(key)) {
          runtimeError("Hashmap key type must be number or string.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Table* table = &AS_HASHMAP(peek(2));
        tableSet(table, key, value);
        pop();
        pop();
        pop();
        push(value);
      }
      DISPATCH();
    }
DO_OP_EQUAL:
    {
      Value b = pop();
      Value a = pop();
      push(BOOL_VAL(valuesEqual(a, b)));
      DISPATCH();
    }
DO_OP_GREATER:
    BINARY_OP(BOOL_VAL, >);
    DISPATCH();
DO_OP_LESS:
    BINARY_OP(BOOL_VAL, <);
    DISPATCH();
DO_OP_ADD:
    {
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
      DISPATCH();
    }
DO_OP_SUBTRACT:
    BINARY_OP(NUMBER_VAL, -);
    DISPATCH();
DO_OP_MULTIPLY:
    BINARY_OP(NUMBER_VAL, *);
    DISPATCH();
DO_OP_DIVIDE:
    BINARY_OP(NUMBER_VAL, /);
    DISPATCH();
DO_OP_MOD:
    {
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
        runtimeError("Operands must be numbers.");
        return INTERPRET_RUNTIME_ERROR;
      }
      // % only applies to int :(
      int b = AS_NUMBER(pop());
      int a = AS_NUMBER(pop());
      push(NUMBER_VAL(a % b));
      DISPATCH();
    }
DO_OP_NOT:
    push(BOOL_VAL(isFalsey(pop())));
    DISPATCH();
DO_OP_NEGATE:
    {
      if (!IS_NUMBER(peek(0))) {
        runtimeError("Operand must be a number.");
        return INTERPRET_RUNTIME_ERROR;
      }
      push(NUMBER_VAL(-AS_NUMBER(pop())));
      DISPATCH();
    }
DO_OP_BIT_AND:
    BIT_BINARY_OP(&);
    DISPATCH();
DO_OP_BIT_OR:
    BIT_BINARY_OP(|);
    DISPATCH();
DO_OP_BIT_XOR:
    BIT_BINARY_OP(^);
    DISPATCH();
DO_OP_BIT_LSHIFT:
    BIT_BINARY_OP(<<);
    DISPATCH();
DO_OP_BIT_RSHIFT:
    BIT_BINARY_OP(>>);
    DISPATCH();
DO_OP_ASSOC:
    {
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

      pop();
      pop();
      DISPATCH();
    }
DO_OP_APPEND:
    {
      Value array = peek(1);
      Value value = peek(0);
      if (!IS_ARRAY(array)) {
        runtimeError("Can only append to array.");
        return INTERPRET_RUNTIME_ERROR;
      }

      writeValueArray(&AS_ARRAY(array), value);
      pop();
      DISPATCH();
    }
DO_OP_HASHMAP:
    push(OBJ_VAL(newHashmap()));
    DISPATCH();
DO_OP_ARRAY:
    push(OBJ_VAL(newArray()));
    DISPATCH();
DO_OP_LENGTH:
    {
      if (IS_STRING(peek(0))) {
        push(NUMBER_VAL(AS_STRING(pop())->length));
      } else if (IS_ARRAY(peek(0))) {
        push(NUMBER_VAL(AS_ARRAY(pop()).count));
      } else {
        runtimeError("Operand must be string or array.");
        return INTERPRET_RUNTIME_ERROR;
      }
      DISPATCH();
    }
DO_OP_JUMP:
    {
      uint16_t offset = READ_SHORT();
      frame->ip += offset;
      DISPATCH();
    }
DO_OP_JUMP_IF_TRUE:
    {
      uint16_t offset = READ_SHORT();
      if (!isFalsey(pop())) frame->ip += offset;
      DISPATCH();
    }
DO_OP_JUMP_IF_FALSE:
    {
      uint16_t offset = READ_SHORT();
      if (isFalsey(pop())) frame->ip += offset;
      DISPATCH();
    }
DO_OP_LOOP:
    {
      uint16_t offset = READ_SHORT();
      frame->ip -= offset;
      DISPATCH();
    }
DO_OP_CALL:
    {
      int argCount = READ_BYTE();
      // Push function & its args to locals stack
      // + 1 to include the function itself
      for (int i = argCount; i >= 0; i--) push_local(peek(i));
      for (int i = 0; i < argCount + 1; i++) pop();

      if (!callValue(peek_local(argCount), argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      closure = frame->closure;
      slots = frame->slots;
      DISPATCH();
    }
DO_OP_CLOSURE:
    {
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
      DISPATCH();
    }
DO_OP_CLOSE_UPVALUE:
    closeUpvalues(vm.localsTop - 1);
    pop_local();
    DISPATCH();
DO_OP_RETURN:
    {
      closeUpvalues(frame->slots);
      vm.frameCount--;
      if (vm.frameCount == 0) {
        vm.lastResult = pop();

        // Ensure next interpret() starts from a clean slate.
        vm.stackTop = vm.stack;
        vm.localsTop = vm.locals;
        vm.openUpvalues = NULL;

        return INTERPRET_OK;
      }
      vm.localsTop = frame->slots;
      frame = &vm.frames[vm.frameCount - 1];
      closure = frame->closure;
      slots = frame->slots;
      DISPATCH();
    }
  }

#undef DISPATCH
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
  push_local(OBJ_VAL(closure));
  call(closure, 0);

  return run();
}
