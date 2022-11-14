#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "objloader.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"
#include "native_fn.h"

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

void initVM() {
  resetStack();
  vm.objects = NULL;
  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;

  initTable(&vm.globals);
  initTable(&vm.strings);

  defineBuiltinNatives();
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

void push_local(Value value) {
  *vm.localsTop = value;
  vm.localsTop++;
}

Value pop_local() {
  vm.localsTop--;
  return *vm.localsTop;
}

static Value peek_local(int distance) {
  return vm.localsTop[-1 - distance];
}

static bool call(ObjClosure* closure, int argCount) {
  if (argCount < closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.",
        closure->function->arity, argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.localsTop - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_CLOSURE:
        return call(AS_CLOSURE(callee), argCount);
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
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

static void concatenate() {
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
  register uint8_t* ip = frame->ip;
#define READ_BYTE() (*ip++)
#define READ_SHORT() \
    (ip += 2, \
    (uint16_t)((ip[-2] << 8) | ip[-1]))

#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        frame->ip = ip; \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("        |       \x1b[1;32m[ ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printValue(*slot);
      printf(" ");
    }
    printf("]\x1b[0m\n");
    printf("        |     \x1b[0;33mL:[ ");
    for (Value* slot = vm.locals; slot < vm.localsTop; slot++) {
      printValue(*slot);
      printf(" ");
    }
    printf("]\x1b[0m\n");
    disassembleInstruction(&frame->closure->function->chunk,
        (int)(ip - frame->closure->function->chunk.code), false);
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_NOP: break;
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_CONST_BYTE: {
        Value constant = NUMBER_VAL(READ_BYTE());
        push(constant);
        break;
      }
      case OP_NIL: push(NIL_VAL); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
      case OP_POP: pop(); break;
      case OP_DUP: push(peek(0)); break;
      case OP_NEW_LOCAL: push_local(pop()); break;
      case OP_POP_LOCAL: pop_local(); break;
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        break;
      }
      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, OBJ_VAL(name), &value)) {
          frame->ip = ip;
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
          frame->ip = ip;
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(*frame->closure->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(0);
        break;
      }
      case OP_GET_PROPERTY: {
        if (!IS_HASHMAP(peek(0))) {
          frame->ip = ip;
          runtimeError("Only hashmap have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        Table* table = &AS_HASHMAP(peek(0));
        ObjString* name = READ_STRING();

        pop(); // That hashmap

        Value value;
        if (tableGet(table, OBJ_VAL(name), &value)) {
          push(value);
        } else {
          push(NIL_VAL);
        }
        break;
      }
      case OP_SET_PROPERTY: {
        if (!IS_HASHMAP(peek(1))) {
          frame->ip = ip;
          runtimeError("Only hashmap can set properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        Table* table = &AS_HASHMAP(peek(1));
        tableSet(table, OBJ_VAL(READ_STRING()), peek(0));

        Value value = pop();
        pop();
        push(value);

        break;
      }
      case OP_GET_BY_INDEX: {
        if (!IS_HASHMAP(peek(1)) && !IS_ARRAY(peek(1))) {
          frame->ip = ip;
          runtimeError("Only array or hashmap can get value by index.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Value key = peek(0);
        if (IS_ARRAY(peek(1))) {
          if (!IS_NUMBER(key)) {
            frame->ip = ip;
            runtimeError("Can only use number index to access array.");
            return INTERPRET_RUNTIME_ERROR;
          }
          int index = AS_NUMBER(key);
          if (index != AS_NUMBER(key)) {
            // check if the number is an int
            frame->ip = ip;
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

        } else {
          // is hashmap
          if (!IS_NUMBER(key) && !IS_STRING(key)) {
            frame->ip = ip;
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
        }
        break;
      }
      case OP_SET_BY_INDEX: {
        if (!IS_HASHMAP(peek(2)) && !IS_ARRAY(peek(2))) {
          frame->ip = ip;
          runtimeError("Only array or hashmap can get value by index.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Value key = peek(1);
        Value value = peek(0);
        if (IS_ARRAY(peek(2))) {
          if (!IS_NUMBER(key)) {
            frame->ip = ip;
            runtimeError("Can only use number index to access array.");
            return INTERPRET_RUNTIME_ERROR;
          }
          int index = AS_NUMBER(key);
          if (index != AS_NUMBER(key)) {
            // check if the number is an int
            frame->ip = ip;
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
            frame->ip = ip;
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
        break;
      }
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
      case OP_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          frame->ip = ip;
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
      case OP_MOD: {
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
          frame->ip = ip;
          runtimeError("Operands must be numbers.");
          return INTERPRET_RUNTIME_ERROR;
        }
        // % only applies to int :(
        int b = AS_NUMBER(pop());
        int a = AS_NUMBER(pop());
        push(NUMBER_VAL(a % b));
        break;
      }
      case OP_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        break;
      case OP_NEGATE: {
        if (!IS_NUMBER(peek(0))) {
          frame->ip = ip;
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      }
      case OP_ASSOC: {
        Value hashmap = peek(2);
        Value key     = peek(1);
        Value value   = peek(0);
        if (!IS_HASHMAP(hashmap)) {
          frame->ip = ip;
          runtimeError("Can only assoc to hashmap.");
          return INTERPRET_RUNTIME_ERROR;
        }
        if (!IS_NUMBER(key) && !IS_STRING(key)) {
          frame->ip = ip;
          runtimeError("Hashmap key type must be number or string.");
          return INTERPRET_RUNTIME_ERROR;
        }
        Table* table = &AS_HASHMAP(hashmap);
        tableSet(table, key, value);

        pop();
        pop();
        break;
      }
      case OP_APPEND: {
        Value array = peek(1);
        Value value = peek(0);
        if (!IS_ARRAY(array)) {
          frame->ip = ip;
          runtimeError("Can only append to array.");
          return INTERPRET_RUNTIME_ERROR;
        }

        writeValueArray(&AS_ARRAY(array), value);
        pop();
        break;
      }
      case OP_HASHMAP: push(OBJ_VAL(newHashmap())); break;
      case OP_ARRAY: push(OBJ_VAL(newArray())); break;
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        ip += offset;
        break;
      }
      case OP_JUMP_IF_TRUE: {
        uint16_t offset = READ_SHORT();
        if (!isFalsey(pop())) ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(pop())) ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        ip -= offset;
        break;
      }
      case OP_CALL: {
        int argCount = READ_BYTE();
        // pushing function & its args to locals stack
        // + 1 to include the function it self
        for (int i = argCount; i >= 0; i--) push_local(peek(i));
        for (int i = 0; i < argCount + 1; i++) pop();

        frame->ip = ip;
        if (!callValue(peek_local(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        break;
      }
      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));
        for (int i = 0; i < closure->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (isLocal) {
            closure->upvalues[i] = captureUpvalue(frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        break;
      }
      case OP_CLOSE_UPVALUE:
        closeUpvalues(vm.localsTop - 1);
        pop_local();
        break;
      case OP_RETURN: {
        closeUpvalues(frame->slots);
        vm.frameCount--;
        if (vm.frameCount == 0) {
          pop();
          return INTERPRET_OK;
        }

        vm.localsTop = frame->slots;
        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        break;
      }
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(uint8_t* obj) {
  ObjFunction* function = loadObj(obj);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  push(OBJ_VAL(function));
  ObjClosure* closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  call(closure, 0);

  return run();
}
