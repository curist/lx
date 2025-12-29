## Bytecode Stack-Height Verifier

A JVM-style bytecode verifier that validates stack discipline before execution.

### Purpose

Validates compiled bytecode for:
- **No stack underflow** - operations never pop from empty stack
- **Consistent join heights** - control-flow joins have matching stack heights
- **Correct return heights** - functions return with expected stack state

This catches codegen bugs early, especially critical when adding new instructions like `UNWIND`.

### Algorithm

Dataflow analysis over instruction addresses:

1. **Initialization**: Entry point starts at initial height (currently 0 for value stack)
2. **Propagation**: For each instruction:
   - Compute `outHeight = inHeight + stackEffect(opcode)`
   - Check for underflow (`outHeight < 0`)
   - Propagate height to successor instructions
3. **Join consistency**: If instruction reached multiple ways, heights must match
4. **Return validation**: `OP_RETURN` must have at least 1 value available

### Stack Effects

Current effects (dual-stack model):

```
Push 1:    CONST, NIL, TRUE, FALSE, GET_LOCAL, GET_GLOBAL, ...
Pop 1:     POP, POP_LOCAL, NOT, NEGATE, CLOSE_UPVALUE
Neutral:   SET_LOCAL, SWAP, LENGTH (pop 1, push 1)
Binary:    -1 (pop 2, push 1): ADD, MUL, EQUAL, ...
Call:      -argCount (pop callee+args, push result)
Jumps:     JUMP (0), JUMP_IF_TRUE/FALSE (-1)
```

### Migration to Single-Stack

When migrating to single-stack VM:

1. **Keep current effects initially** - `NEW_LOCAL: -1`, `POP_LOCAL: -1`
2. **Verify during migration** - catches height mismatches
3. **Tighten return rule** - eventually require `inHeight == baseHeight + 1`
4. **Consider `NEW_LOCAL: 0`** - after codegen restructuring

### Integration Points

#### Option A: Compiler Integration (Recommended)

Call from `codegen()` after chunk generation:

```lx
// In src/passes/backend/codegen.lx or cmd/mlx.lx
let verify = import "src/passes/backend/verify-bytecode.lx"

fn codegen(ast, resolveResult) {
  // ... existing codegen ...

  if !verify.verifyChunk(chunk, arity) {
    // Verification failed - errors already printed
    return nil
  }

  return chunk
}
```

#### Option B: VM Integration

Add to VM before execution (C code in `vm.c`):

```c
// Before interpret():
if (!verifyChunk(&function->chunk, function->arity)) {
  return INTERPRET_COMPILE_ERROR;
}
```

Requires implementing verifier in C (see `src/passes/backend/verify-bytecode.lx` for logic).

#### Option C: Debug Flag

Add `--verify-bytecode` flag to run verification optionally:

```bash
lx run --verify-bytecode program.lx
```

### Current Limitations

1. **CLOSURE length calculation** - requires accessing function constants
2. **Weak return rule** - only checks `height >= 1`, not `height == baseHeight + 1`
3. **No type checking** - only validates heights, not value types

### Future Enhancements

1. **Stronger return invariant**: Require exact height at `OP_RETURN`
2. **Statement region validation**: Prove statement blocks are stack-neutral
3. **Type verification**: Track value types on stack (JVM-style)
4. **UNWIND support**: Validate unwind targets and preserve semantics

### Testing

```bash
LX=../out/lx make test  # includes test/verify-bytecode.test.lx
```

Unit tests validate stack effect calculations. Integration tests will come from running on real compiled code.

### Why This Matters for UNWIND

`UNWIND` will manipulate stack height across control flow. The verifier will:
- Ensure `UNWIND` restores correct height
- Catch block value preservation bugs
- Validate break/continue stack discipline
- Prove locals are properly cleaned up

**Recommendation**: Wire verifier into compiler **before** implementing `UNWIND`. Run full test suite with verification enabled. Fix any height inconsistencies. Then add `UNWIND` with confidence.
