# Phase 2 Handoff: General Loop Semantics

## Current Status

**Completed:**
- ✅ **Phase 1** - MVP Fusion (step=+1, integer literal init, variable limits)
- ✅ **Phase 3** - Fixnum-first numeric producers (len/Math.floor/tonumber/range return fixnums)

**Next:**
- ⏭ **Phase 2** - Generalize loop semantics (arbitrary limit expressions, signed constant step)

---

## Phase 1 & 3 Summary

### What Works Now

Loops matching this pattern get fused:
```lx
fn example(n) {
  let sum = 0
  for let i = 0; i < n; i = i + 1 {  // ✓ Fused
    sum = sum + i
  }
  sum
}
```

Requirements for fusion (Phase 1):
- Init: Integer literal (`0`, `1`, etc.)
- Limit: Variable in stable slot (parameter or local)
- Step: Exactly `i = i + 1`
- Comparison: `<` or `<=`

Phase 3 made more loops fusible:
```lx
let arr = [1, 2, 3]
let n = len(arr)  // ✓ Returns fixnum now!
for let i = 0; i < n; i = i + 1 {  // ✓ Fused
  println(arr[i])
}
```

### What Doesn't Work Yet

```lx
// ❌ Literal limit (no variable)
for let i = 0; i < 10; i = i + 1 { }

// ❌ Expression limit (not a simple variable)
for let i = 0; i < len(arr); i = i + 1 { }

// ❌ Variable step
for let i = 0; i < n; i = i + step { }

// ❌ Negative step
for let i = n; i > 0; i = i - 1 { }

// ❌ Non-literal init from variable
for let i = start; i < end; i = i + 1 { }
```

---

## Phase 2 Goals

### 1. General Limit Expressions

**Problem:** Limits must currently be simple variables.

**Solution:** Rewrite limit expressions into init-locals:
```lx
// Before (doesn't fuse):
for let i = 0; i < len(arr); i = i + 1 { body }

// After compiler rewrite:
{
  let __limit = len(arr)
  for let i = 0; i < __limit; i = i + 1 { body }
}
```

**Implementation:**
- Detect when `condition` is not a simple variable reference
- Insert a hidden local `__limit` before the loop
- Evaluate limit expression once, store in `__limit`
- Rewrite condition to use `__limit`
- Continue with normal fusion

**Location:** `lx/src/passes/backend/codegen.lx` in `compileFor()`

### 2. Signed Constant Step

**Problem:** Only `i = i + 1` currently supported.

**Solution:** Support `i = i + K` and `i = i - K` where K is a constant.

**Implementation:**
- Add new opcodes: `OP_FORPREP`, `OP_FORLOOP` with step operand
- Pattern match: `i = i + <const>` or `i = i - <const>`
- Derive comparison direction from step sign:
  - `step > 0` → use `<` or `<=`
  - `step < 0` → use `>` or `>=`
- Emit appropriate comparison kind

**Opcodes needed:**
```c
// include/chunk.h
OP_FORPREP,  // General numeric loop prepare (variable step)
OP_FORLOOP,  // General numeric loop iterate (variable step)
```

**Encoding:**
```
[opcode]
[i_slot:u8]
[limit_slot:u8]
[step:s8 or s16]  // Signed step value
[cmp_kind:u8]     // 0=LT, 1=LE, 2=GT, 3=GE
[offset:u16]
```

### 3. Break/Continue

Already supported in Phase 1! No additional work needed.

---

## Current Codebase State

### Key Files

**VM Runtime (C):**
- `src/vm.c:1504-1596` - OP_FORPREP_1/OP_FORLOOP_1 handlers
- `include/chunk.h:102-103` - Opcode definitions
- `include/native_fn.h:154-319, 558-615, 1245-1266` - Fixnum natives

**Compiler (lx):**
- `lx/src/passes/backend/codegen.lx:991-1218` - Loop pattern matching & codegen
  - `matchNumericForLoop()` - AST pattern matcher
  - `compileNumericForLoop()` - Fused loop code generator
  - `compileFor()` - Main loop compilation entry point
- `lx/src/types.lx:184-185` - Opcode enum (mirrored from chunk.h)
- `lx/src/passes/backend/verify-bytecode.lx:116-117, 206-207, 360-361` - Bytecode verification

**Tests:**
- `lx/test/fused-for-loop.test.lx` - Runtime edge cases (Phase 1)
- `lx/test/fused-for-loop-disasm.test.lx` - Bytecode verification (Phase 1)
- `lx/test/fixnum-natives.test.lx` - Fixnum behavior (Phase 3)
- `lx/test/phase3-fused-loops.test.lx` - Native limit fusion (Phase 3)

**Documentation:**
- `lx/docs/fused-numeric-for-loop.md` - Design spec (all phases)

### Important Functions

**Pattern Matching:**
```lx
// lx/src/passes/backend/codegen.lx:991-1065
fn matchNumericForLoop(gen, node) -> match object or nil
```

Returns:
```lx
.{
  i_name: "i",           // Induction variable name
  limit_node: <AST>,     // Limit identifier AST node
  cmp_op: "<" or "<=",   // Comparison operator
}
```

**Code Generation:**
```lx
// lx/src/passes/backend/codegen.lx:1079-1197
fn compileNumericForLoop(gen, node, match) -> bool
```

Returns `true` if fusion succeeded, `false` to fall back to generic loop.

**Slot Resolution:**
```lx
// Critical: Use gen.emittedLocals, NOT gen.resolvedNames
let i_slot = gen.emittedLocals[node.init.id]
let limit_binding = gen.resolvedNames[match.limit_node.id]
let limit_slot = gen.emittedLocals[limit_binding.declaredAt]
```

---

## Known Issues & Gotchas

### 1. Slot Resolution

**WRONG:**
```lx
let limit_slot = gen.resolvedNames[match.limit_node.id]
```

**CORRECT:**
```lx
let limit_binding = gen.resolvedNames[match.limit_node.id]
let limit_slot = gen.emittedLocals[limit_binding.declaredAt]
```

**Why:** `gen.resolvedNames` maps identifier uses to bindings. The binding's `declaredAt` points to the declaration node ID, which is the key in `gen.emittedLocals`.

### 2. AST Structure

**Let statement:**
```lx
node.init.type == NODE.Let
node.init.name        // Identifier node
node.init.init        // Initialization expression (NOT .value!)
```

**Binary expression:**
```lx
node.condition.type == NODE.Binary
node.condition.left   // Left operand
node.condition.right  // Right operand
node.condition.op     // Operator token
```

### 3. Bootstrap Required

After modifying compiler code or adding opcodes:
```bash
# Clean build
make clean && make

# Bootstrap (rebuild compiler with itself)
LX=./out/lx make -B prepare build

# Verify
make test
```

### 4. Opcode Numbering

**CRITICAL:** Always append new opcodes at the END of the enum to avoid renumbering existing opcodes.

```c
// include/chunk.h - CORRECT
// ... existing opcodes ...
OP_FORPREP_1,   // 68
OP_FORLOOP_1,   // 69
OP_FORPREP,     // 70 (Phase 2 - append here)
OP_FORLOOP,     // 71 (Phase 2 - append here)
```

Mirror changes in `lx/src/types.lx`.

### 5. Bytecode Verifier

When adding new opcodes, update THREE tables in `verify-bytecode.lx`:

1. **STACK_MIN** (line ~30) - Minimum stack inputs required
2. **STACK_EFFECTS** (line ~118) - Net stack delta
3. **INSTRUCTION_LENGTHS** (line ~276) - Byte length of instruction

---

## Phase 2 Implementation Plan

### Step 1: General Limit Expressions ✅ **IMPLEMENTED**

**Goal:** Enable `for let i = 0; i < len(arr); i = i + 1`

**Implementation Approach:** Separate loop lowering pass (Option 2)

A dedicated pass `lx/src/passes/backend/lower-loops.lx` runs after ANF and before codegen:

**Pipeline:**
```
parse → lower → anf → resolve → anf-inline → **lower-loops** → codegen
```

**Transformation:**
```lx
// Before:
for let i = 0; i < len(arr); i = i + 1 { body }

// After:
{
  let __loop_limit_0 = len(arr)
  for let i = 0; i < __loop_limit_0; i = i + 1 { body }
}
```

**Key Design Decisions:**

1. **Why not ANF?**
   - ANF is for expression normalization (evaluation order)
   - Loop limit materialization is for opcode constraints
   - Keeps ANF backend-agnostic

2. **Why not codegen?**
   - Cleaner separation of concerns
   - Codegen stays mechanical (emit, don't transform)
   - Easier to test and reason about

3. **Pass characteristics:**
   - Input: ANF AST with arbitrary loop limits
   - Output: ANF AST where numeric loop limits are identifiers
   - Enabled: Always on when ANF is enabled
   - Preserves: Node provenance via origin map

**Pattern Matching:**
- Only transforms loops matching the numeric for-loop pattern
- Init: `let i = <expr>`
- Condition: `i < limit` (where limit is NOT an identifier)
- Update: `i = i + K` or `i = i - K`
- Comparisons: `<`, `<=`, `>`, `>=`

**Testing:**
- Verify `for let i = 0; i < len(arr)` fuses
- Verify `for let i = 0; i < 10` fuses (literal limit)
- Verify `for let i = 0; i < x + y` fuses (expression limit)
- Verify nested loops with expression limits
- Verify limit evaluated once (not per iteration)

### Step 2: Signed Constant Step

**Goal:** Enable `for let i = 0; i < 10; i = i + 2` and `for let i = 10; i > 0; i = i - 1`

**Changes:**

1. **Add new opcodes** (append to end):
```c
// include/chunk.h
OP_FORPREP,  // opcode + i_slot + limit_slot + step + cmp_kind + offset
OP_FORLOOP,  // opcode + i_slot + limit_slot + step + cmp_kind + offset
```

2. **Update pattern matcher**:
```lx
// Match: i = i + <const> or i = i - <const>
if node.update.value.op.type == TOKEN.PLUS {
  match.step = node.update.value.right.value
} else if node.update.value.op.type == TOKEN.MINUS {
  match.step = -node.update.value.right.value
}

// Verify comparison direction matches step sign
if match.step > 0 and !(match.cmp_op == "<" or match.cmp_op == "<=") {
  return nil  // Mismatch
}
if match.step < 0 and !(match.cmp_op == ">" or match.cmp_op == ">=") {
  return nil  // Mismatch
}
```

3. **Update code generator**:
```lx
// Emit FORPREP with step
emitByte(gen, OP.FORPREP)
emitByte(gen, localSlot(i_slot))
emitByte(gen, localSlot(limit_slot))
emitSignedByte(gen, match.step)  // New: step operand
emitByte(gen, cmp_kind)
emitShort(gen, forwardJump)
```

4. **VM implementation**:
```c
case OP_FORPREP: {
  uint8_t i_slot = READ_BYTE();
  uint8_t limit_slot = READ_BYTE();
  int8_t step = (int8_t)READ_BYTE();  // Signed!
  uint8_t cmp_kind = READ_BYTE();
  uint16_t offset = READ_SHORT();

  // Similar to FORPREP_1 but use variable step
  // ...
}

case OP_FORLOOP: {
  // Increment by step instead of 1
  i_int += step;

  // Check for overflow in both directions
  if (step > 0 && i_int > FIXNUM_MAX) { error }
  if (step < 0 && i_int < FIXNUM_MIN) { error }

  // Rest similar to FORLOOP_1
}
```

5. **Update verifier** (add to all 3 tables)

6. **Bootstrap** (rebuild compiler with new opcodes)

**Testing:**
- Verify `for i = 0; i < 10; i = i + 2` (step=2)
- Verify `for i = 10; i > 0; i = i - 1` (step=-1)
- Verify `for i = 0; i < 100; i = i + 10` (step=10)
- Verify overflow detection for negative steps
- Verify comparison operators match step direction

### Step 3: Integration Testing

Run full test suite after each step:
```bash
make test
./out/lx run benchmarks/lx/sum_loop.lx 1000000
```

---

## Debug Tips

### 1. Check if Loop Fuses

```bash
./out/lx disasm /tmp/test.lx 2>&1 | grep "OP_FOR"
```

Should show:
```
0004    |  44 OP_FORPREP_1        i=3 limit=1 <= -> 23
0017    3--45 OP_FORLOOP_1        i=3 limit=1 <= -> 10
```

### 2. Add Debug Output

In `compileNumericForLoop()`:
```lx
Lx.stderr.println("[FUSE] Attempting fusion for loop at line", node.line)
Lx.stderr.println("[FUSE] Match:", match)
Lx.stderr.println("[FUSE] i_slot=", i_slot, "limit_slot=", limit_slot)
```

### 3. Check Slot Numbering

```lx
Lx.stderr.println("gen.nextLocalSlot =", gen.nextLocalSlot)
Lx.stderr.println("loopContinueSlot =", loopContinueSlot)
Lx.stderr.println("i_slot < loopContinueSlot?", i_slot < loopContinueSlot)
```

### 4. Verify Bytecode

```bash
./out/lx compile test.lx -o test.lxobj
xxd test.lxobj | grep -A5 -B5 "44\|45"  # Look for opcodes 68/69
```

### 5. Runtime Errors

If you see "Loop variable must be an integer":
- Check that init is actually an integer literal
- Verify fixnum range (±35 trillion)

If you see "Unknown opcode":
- Update bytecode verifier tables
- Rebuild with `make clean && make`

---

## Performance Expectations

### Before Phase 2
```lx
// Only this pattern fuses:
let n = len(arr)
for let i = 0; i < n; i = i + 1 { }
```

### After Phase 2
```lx
// All these should fuse:
for let i = 0; i < len(arr); i = i + 1 { }
for let i = 0; i < 100; i = i + 1 { }
for let i = 0; i < x + y; i = i + 1 { }
for let i = 0; i < n; i = i + 2 { }
for let i = n; i > 0; i = i - 1 { }
```

### Benchmark Goals

Target: Match or beat Lua 5.4 on numeric for-loop benchmarks.

Current gap (estimate): 2-3x slower
After Phase 2: ~1.2-1.5x slower
After Phase 4 (deopt): ~1.0-1.2x slower

---

## Files to Modify (Phase 2)

### lx Compiler (Step 1: Loop Lowering)
- [x] `lx/src/passes/backend/lower-loops.lx` - **NEW:** Loop lowering pass
- [x] `lx/src/driver.lx` - Wire loop lowering into pipeline

### C Runtime (Step 2: Signed Steps)
- [ ] `include/chunk.h` - Add OP_FORPREP, OP_FORLOOP
- [ ] `src/vm.c` - Implement handlers with variable step
- [ ] `src/debug.c` - Add disassembler cases

### lx Compiler (Step 2: Signed Steps)
- [ ] `lx/src/types.lx` - Mirror opcode enum
- [ ] `lx/src/passes/backend/codegen.lx` - Update pattern matcher & codegen
- [ ] `lx/src/passes/backend/verify-bytecode.lx` - Add new opcodes to tables

### Tests
- [ ] Create `lx/test/phase2-general-limits.test.lx` - Test loop lowering
- [ ] Create `lx/test/phase2-signed-step.test.lx` - Test variable steps
- [ ] Update `lx/test/fused-for-loop-disasm.test.lx` for new opcodes

### Documentation
- [x] Update `lx/docs/phase2-handoff.md` - Document loop lowering design
- [ ] Update `lx/docs/fused-numeric-for-loop.md` - Mark Phase 2 complete

---

## Quick Start (Phase 2)

```bash
# 1. Start with limit rewrite (easier)
cd /Users/curist/playground/lx/lx-lang

# 2. Read current pattern matcher
cat lx/src/passes/backend/codegen.lx | grep -A50 "fn matchNumericForLoop"

# 3. Test current behavior
echo 'for let i = 0; i < len([1,2,3]); i = i + 1 { println(i) }' > /tmp/test.lx
./out/lx disasm /tmp/test.lx | grep OP_FOR
# (should be empty - not fused yet)

# 4. Implement limit rewrite in compileNumericForLoop()
# 5. Rebuild and test
make clean && make
./out/lx disasm /tmp/test.lx | grep OP_FOR
# (should show fused opcodes after implementation)

# 6. Move to signed step (harder)
# 7. Add opcodes, update VM, rebuild, test
```

---

## Questions to Resolve

1. **Step operand size:** s8 (±127) or s16 (±32767)?
   - Recommendation: Start with s8, upgrade to s16 if needed
   - Most loops use step=1, 2, -1, -2

2. **Comparison kind encoding:**
   - Current: 0=LT, 1=LE
   - Phase 2: Add 2=GT, 3=GE?
   - Or: Use signed step to imply direction?

3. **Limit rewrite scope:**
   - Create new scope or reuse loop scope?
   - Recommendation: Create hidden scope to avoid name collision

4. **Overflow behavior:**
   - Phase 2: Keep runtime error (same as Phase 1)
   - Phase 4: Add deopt/bailout mechanism

---

## Success Criteria

**Phase 2 Step 1 (Loop Lowering):**
- [x] Loop lowering pass created (`lx/src/passes/backend/lower-loops.lx`)
- [x] Wired into driver pipeline (runs after anf-inline, before codegen)
- [ ] Tests verify literal limits work (`i < 10`)
- [ ] Tests verify expression limits work (`i < len(arr)`)
- [ ] Tests verify limit evaluated once (not per iteration)
- [ ] All existing tests still pass
- [ ] Compiler successfully bootstraps

**Phase 2 Step 2 (Signed Steps):**
- [ ] Opcodes OP_FORPREP/OP_FORLOOP added
- [ ] VM handlers implemented
- [ ] Loops with step=+K fuse (e.g., `i = i + 2`)
- [ ] Loops with step=-K fuse (e.g., `i = i - 1`)
- [ ] Tests verify all step directions
- [ ] Disassembler shows step values
- [ ] Compiler successfully bootstraps

**Phase 2 Complete:**
- [ ] All step 1 and step 2 criteria met
- [ ] Benchmark shows performance improvement
- [ ] Documentation updated

---

## Contact & Context

This handoff was generated on 2026-01-02 after completing Phase 1 and Phase 3.

Git state:
- Last commit: `a6aff8f Implement Phase 3: Fixnum-first numeric producers`
- Branch: `main`
- Clean working directory

Environment:
- Repo: `/Users/curist/playground/lx/lx-lang`
- Compiler: `./out/lx`
- Tests: `make test`

For questions or clarification, refer to:
- Design doc: `lx/docs/fused-numeric-for-loop.md`
- This handoff: `lx/docs/phase2-handoff.md`
