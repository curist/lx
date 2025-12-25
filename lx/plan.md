# Single-Stack VM Migration Plan

Based on our ANF work enforcing Invariant S, here's a comprehensive plan to migrate to a single-stack VM.

## Current State ✅

- **Invariant S enforced**: `Let` nodes only at statement position
- **Stack discipline guaranteed**: Predictable stack height at statement boundaries
- **Two-stack VM**: Separate `vm.stack[]` (eval) and `vm.locals[]` (variables)

---

## Phase 1: Design Decisions (Answer First)

Before writing code, we need to decide:

### 1.1 Frame Layout Strategy

**Option A: Locals-before-frame (push-on-bind)**
```
Stack layout:
[... | local₀ local₁ ... localₙ | callee arg₀ arg₁ ... argₙ | ...]
       ^                          ^
       frameBase                  stackTop (during expr eval)
```
- Locals pushed as encountered during block execution
- Simple: just keep pushing
- Frame pointer = base of locals
- Slot access: `frame[slotIndex]`

**Option B: Frame-before-locals (traditional)**
```
Stack layout:
[... | callee arg₀ arg₁ ... argₙ | local₀ local₁ ... localₙ | ...]
       ^                            ^
       frameBase                    stackTop (during expr eval)
```
- Parameters come first, then locals
- More complex setup, but matches many VMs
- Slot access: `frame[arity + 1 + localIndex]`

**Recommendation**: **Option A** - simpler with our ANF guarantees

### 1.2 Local Lifetime Strategy

**Question**: When a local goes out of scope (end of block), do we:
- **A. Keep it in slot until function returns** (dead but present)
- **B. Pop it immediately** (reclaim stack space)

**Recommendation**: **B. Pop immediately** - ANF already tracks this with `endScope()`

### 1.3 Stack Cleanup Mechanism

**Question**: How to restore stack height at block boundaries?
- **A. New `OP_TRUNCATE n` opcode** - set `stackTop = frameBase + n`
- **B. Emit multiple `OP_POP`** - one per local
- **C. Track in codegen, emit adjustment at boundaries**

**Recommendation**: **A. New `OP_TRUNCATE`** - more efficient, clearer intent

---

## Phase 2: Codegen Changes

### 2.1 Remove `OP_NEW_LOCAL` and `OP_POP_LOCAL`

**Current bytecode:**
```
NIL
NEW_LOCAL        // Move from eval stack to locals stack
```

**New bytecode (push-on-bind):**
```
NIL              // Value stays on stack
                 // nextLocalSlot++, that's it
```

**Changes in `codegen.lx`:**

```lx
fn compileLet(gen, node, mode) {
  compileValue(gen, node.init or <nil-node>)

  let local = gen.localsByDecl[node.name.id]
  gen.emittedLocals[local.nodeId] = gen.nextLocalSlot
  gen.nextLocalSlot++

  if mode == MODE.STMT {
    // Value consumed by slot, nothing to do
  } else {
    emitByte(gen, OP.DUP, node.line)  // Duplicate for expression value
  }
}
```

### 2.2 Update `endScope()` for Stack Cleanup

**Current:**
```lx
fn endScope(gen, node) {
  for each local in reverse {
    if local.isCaptured {
      emitByte(gen, OP.CLOSE_UPVALUE)
    } else {
      emitByte(gen, OP.POP_LOCAL)
    }
    gen.nextLocalSlot--
  }
}
```

**New (with OP_TRUNCATE):**
```lx
fn endScope(gen, node) {
  let scope = gen.scopeInfo[node.id]
  let locals = scope.locals or []

  // Close upvalues first (they need to read stack slots)
  for let i = len(locals) - 1; i >= 0; i-- {
    let local = locals[i]
    if local.isCaptured {
      emitByte(gen, OP.CLOSE_UPVALUE, gen.currentLine)
      gen.nextLocalSlot--
    }
  }

  // Truncate stack to remove all non-captured locals
  let targetSlot = gen.nextLocalSlot
  for let i = len(locals) - 1; i >= 0; i-- {
    if !locals[i].isCaptured {
      targetSlot--
    }
  }

  if targetSlot < gen.nextLocalSlot {
    emitByte(gen, OP.TRUNCATE, gen.currentLine)
    emitByte(gen, targetSlot, gen.currentLine)
    gen.nextLocalSlot = targetSlot
  }
}
```

### 2.3 Update Block Value Preservation

**Problem**: Block in value mode must leave exactly 1 value, but may have locals on stack

**Solution**:
```lx
fn compileBlock(gen, node, mode) {
  beginScope(gen, node)

  // ... compile expressions ...

  if mode == MODE.VALUE {
    // Last expression left value on top
    // We need: [local₀ ... localₙ value]
    // Want:    [value]

    let numLocals = scope.locals.length
    if numLocals > 0 {
      // Swap value below locals, then truncate
      emitByte(gen, OP.SLIDE, line)     // New opcode: slide top value down
      emitByte(gen, numLocals, line)    // Slide past N locals
    }
  }

  endScope(gen, node)
}
```

---

## Phase 3: VM Changes

### 3.1 Remove Locals Stack from VM Struct

**`include/vm.h`:**
```c
typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;

  Value stack[STACK_MAX];
  Value* stackTop;
  // REMOVE: Value locals[STACK_MAX];
  // REMOVE: Value* localsTop;

  Value lastResult;
  // ... rest unchanged
} VM;
```

### 3.2 Update Frame Structure

**Current:**
```c
typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;  // Points into vm.locals
} CallFrame;
```

**New (Option A - locals-before-frame):**
```c
typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;  // Points into vm.stack (base of frame's locals)
} CallFrame;
```

### 3.3 Update `call()` Function

**New calling convention:**
```c
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

  // Stack has: [callee arg₀ arg₁ ... argₙ ...]
  // Discard extra args
  for (int i = 0; i < argCount - arity; i++) {
    pop();
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;

  // slots points to callee position
  frame->slots = vm.stackTop - arity - 1;

  return true;
}
```

### 3.4 Update Opcode Implementations

**OP_GET_LOCAL:**
```c
DO_OP_GET_LOCAL: {
  uint8_t slot = READ_BYTE();
  push(frame->slots[slot]);
  DISPATCH();
}
```

**OP_SET_LOCAL:**
```c
DO_OP_SET_LOCAL: {
  uint8_t slot = READ_BYTE();
  frame->slots[slot] = peek(0);
  DISPATCH();
}
```

**NEW: OP_TRUNCATE:**
```c
DO_OP_TRUNCATE: {
  uint8_t targetSlot = READ_BYTE();
  vm.stackTop = frame->slots + targetSlot;
  DISPATCH();
}
```

**NEW: OP_SLIDE (optional, for block values):**
```c
DO_OP_SLIDE: {
  uint8_t distance = READ_BYTE();
  Value value = pop();
  for (int i = 0; i < distance; i++) {
    pop();
  }
  push(value);
  DISPATCH();
}
```

**OP_CALL (updated):**
```c
DO_OP_CALL: {
  int argCount = READ_BYTE();
  // Stack: [... callee arg₀ ... argₙ]
  // All already on main stack, just call
  if (!callValue(peek(argCount), argCount)) {
    return INTERPRET_RUNTIME_ERROR;
  }
  frame = &vm.frames[vm.frameCount - 1];
  closure = frame->closure;
  slots = frame->slots;
  DISPATCH();
}
```

**OP_RETURN (updated):**
```c
DO_OP_RETURN: {
  Value result = pop();
  closeUpvalues(frame->slots);

  vm.frameCount--;
  if (vm.frameCount == 0) {
    pop();  // Pop script function
    vm.lastResult = result;
    return INTERPRET_OK;
  }

  // Restore stack to caller's state
  vm.stackTop = frame->slots;
  push(result);

  frame = &vm.frames[vm.frameCount - 1];
  closure = frame->closure;
  slots = frame->slots;
  DISPATCH();
}
```

### 3.5 Update `callValue()` for Natives

```c
case OBJ_NATIVE: {
  NativeFn native = AS_NATIVE(callee)->function;
  Value* args = vm.stackTop - argCount;

  if (native(argCount, args)) {
    vm.stackTop -= argCount + 1;  // Remove args + callee
    push(args[-1]);                // Native wrote result to args[-1]
    return true;
  } else {
    runtimeError(AS_STRING(args[-1])->chars);
    return false;
  }
}
```

### 3.6 Update GC to Mark Single Stack

**`src/memory.c` - `markRoots()`:**
```c
static void markRoots() {
  // Mark single stack
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
    markValue(*slot);
  }

  // REMOVE: locals stack marking

  // Mark frames
  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj*)vm.frames[i].closure);
  }

  // Mark open upvalues
  for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL;
       upvalue = upvalue->next) {
    markObject((Obj*)upvalue);
  }

  markTable(&vm.globals);
  markValue(vm.lastResult);
}
```

---

## Phase 4: Testing Strategy

### 4.1 Incremental Validation

1. **Build and run existing tests** - should all pass
2. **Compare bytecode output** - verify `OP_NEW_LOCAL`/`OP_POP_LOCAL` removed
3. **Run simple programs**:
   ```lx
   let a = 1
   let b = 2
   println(a + b)
   ```
4. **Test scoping**:
   ```lx
   let x = 1
   { let x = 2; println(x) }  // 2
   println(x)                  // 1
   ```
5. **Test function calls**:
   ```lx
   fn add(a, b) { a + b }
   println(add(1, 2))
   ```
6. **Test closures and upvalues**
7. **Run full test suite**

### 4.2 Debug Aids

Add VM flag for stack tracing:
```c
#ifdef DEBUG_TRACE_STACK
  printf("Stack: ");
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
    printf("[ ");
    printValue(*slot);
    printf(" ]");
  }
  printf("\n");
#endif
```

---

## Phase 5: Migration Checklist

- [ ] Make design decisions (frame layout, cleanup strategy)
- [ ] Add new opcodes to `chunk.h` (`OP_TRUNCATE`, `OP_SLIDE`)
- [ ] Update `codegen.lx`:
  - [ ] Remove `OP_NEW_LOCAL` emission in `compileLet()`
  - [ ] Update `endScope()` to use `OP_TRUNCATE`
  - [ ] Update block value preservation
- [ ] Update `vm.h`:
  - [ ] Remove `locals[]` and `localsTop`
  - [ ] Update `CallFrame` docs
- [ ] Update `vm.c`:
  - [ ] Remove `push_local()`, `pop_local()`, `peek_local()`
  - [ ] Update `call()` for single-stack layout
  - [ ] Update `callValue()` natives
  - [ ] Implement `OP_TRUNCATE` and `OP_SLIDE`
  - [ ] Update `OP_GET_LOCAL`, `OP_SET_LOCAL`
  - [ ] Update `OP_CALL`, `OP_RETURN`
  - [ ] Update `OP_CLOSE_UPVALUE`
  - [ ] Remove `resetStack()` locals reset
- [ ] Update `memory.c`:
  - [ ] Remove `vm.locals` marking in `markRoots()`
- [ ] Rebuild and test incrementally
- [ ] Update `debug.c` if needed for disassembly

---

## Risk Assessment

**Low Risk** (Invariant S guarantees these):
- ✅ Stack discipline predictable
- ✅ Statement boundaries well-defined
- ✅ No hidden control flow from nested Lets

**Medium Risk** (Requires careful implementation):
- ⚠️ Upvalue capture pointing to correct stack positions
- ⚠️ Block value preservation with locals on stack
- ⚠️ Native function ABI compatibility

**High Risk** (Breaking changes):
- 🔴 Bytecode incompatibility (old .lxobj won't run)
- 🔴 Must rebuild entire compiler with new VM

---

## Estimated Effort

- **Design decisions**: 30 minutes
- **Codegen changes**: 2-3 hours
- **VM changes**: 3-4 hours
- **Testing and debugging**: 2-4 hours
- **Total**: ~1 day of focused work

---

## Next Steps

1. Review and confirm design decisions (Section 1)
2. Begin Phase 2: Codegen changes
3. Proceed incrementally with testing at each step
