# Plan: Eliminate Unnecessary Temporary Variables

**Status: ✅ COMPLETED**

## Implementation Summary

Successfully implemented both ANF optimization and bytecode superinstructions:

### Approach 1: ANF Optimization (✅ Completed)
- Implemented `anfAssignmentValue()` to avoid temps for simple expressions
- Modified `anfAssignment()` to use new function
- Reduced `i = i + 1` from 8 ops to 6 ops (eliminated block wrapper)
- All tests passing

### Bytecode Superinstructions (✅ Completed)
Added three superinstructions to further optimize common patterns:

1. **ADD_LOCAL_IMM** - `i = i + n` (n < 256)
   - Replaces: GET_LOCAL + CONST_BYTE + ADD + SET_LOCAL (4 ops → 1 op)
   - 75% reduction

2. **STORE_LOCAL** - Local assignment in statement context
   - Replaces: SET_LOCAL + POP (2 ops → 1 op)
   - 50% reduction

3. **STORE_IDX_LOCAL** - `arr[i] = val` (all locals)
   - Replaces: GET_LOCAL×3 + SET_BY_INDEX (4 ops → 1 op)
   - 75% reduction

### Combined Impact
- `i = i + 1`: 8 ops → 1 op (87.5% reduction)
- `arr[i] = i`: 8 ops → 1 op (87.5% reduction)
- All tests passing, optimizations verified in bytecode

---

## Original Problem

ANF transformation creates temporary variables for ALL non-atomic expressions, even simple ones:

**Source:**
```lx
i = i + 1
```

**After ANF:**
```lx
{
  let $temp = i + 1
  i = $temp
}
```

**Bytecode (per assignment):**
- 8 instructions including UNWIND+POP
- Creates temp local slot
- Waste for 50M iterations = 200M+ wasted instructions

---

## Approach 1: Fix ANF to Be Smarter About Temps

### Core Idea

Don't create temps for "simple" expressions that can be directly consumed by their parent operation.

### What's "Simple"?

An expression is simple for assignment RHS if:
1. It's atomic (identifier, literal, function)
2. OR it's a simple binary op with atomic operands: `i + 1`, `acc + i`
3. OR it's a simple unary op with atomic operand: `!flag`, `-x`

**Key insight:** These can be compiled directly without intermediate storage.

### Changes Required

#### In `anf.lx`:

**1. Add `anfAssignmentValue` function (new)**
```lx
fn anfAssignmentValue(anf, node) {
  // Special handling for assignment RHS
  // Returns: .{ pre: [...], expr: ... }

  if isAtomic(node) {
    return .{ pre: [], expr: node }
  }

  // Simple binary: both operands atomic
  if node.type == "Binary" {
    let left = anfExpr(anf, node.left)
    let right = anfExpr(anf, node.right)
    if isAtomic(left) and isAtomic(right) {
      let newNode = copyNodeShallow(anf, node)
      newNode.left = left
      newNode.right = right
      return .{ pre: [], expr: newNode }
    }
  }

  // Simple unary: operand atomic
  if node.type == "Unary" {
    let operand = anfExpr(anf, node.operand)
    if isAtomic(operand) {
      let newNode = copyNodeShallow(anf, node)
      newNode.operand = operand
      return .{ pre: [], expr: newNode }
    }
  }

  // Fall back to normal anfToAtomic
  return anfToAtomic(anf, node)
}
```

**2. Modify `anfAssignment` to use it**
```lx
fn anfAssignment(anf, node) {
  // ... existing target handling ...

  // Use special assignment value handling
  let value = anfAssignmentValue(anf, node.value)

  let newNode = copyNodeShallow(anf, node)
  newNode.target = targetNode
  newNode.value = value.expr

  let pre = concat(targetPre, value.pre)

  // Only wrap in block if there's actually prelude
  if len(pre) == 0 {
    return newNode  // No block needed!
  }
  wrapWithPrelude(anf, node, pre, newNode)
}
```

### Expected Impact

**Before:**
```
i = i + 1  →  { let $temp = i + 1; i = $temp }  →  8 ops
```

**After:**
```
i = i + 1  →  i = i + 1  →  4 ops (no block, no UNWIND)
```

**For sum_loop:** Eliminate ~200M ops (UNWIND+POP for each of 2 assignments × 50M iterations)

### Pros
- ✓ Clean solution at the source
- ✓ Benefits all code, not just loops
- ✓ Easier to reason about generated code
- ✓ Reduces memory pressure (fewer slots)

### Cons
- ⚠️ Changes ANF invariants (need to verify correctness)
- ⚠️ Must ensure codegen can handle non-atomic RHS
- ⚠️ Need comprehensive testing

### Risk Assessment
**Medium** - ANF is a critical pass, but the change is localized to assignments

---

## Approach 2: Peephole Optimization in Codegen

### Core Idea

After compiling, scan bytecode for wasteful patterns and eliminate them.

### Target Patterns

**Pattern 1: Temp variable immediately consumed**
```
GET_LOCAL i
CONST_BYTE 1
ADD
GET_LOCAL $temp    ← just read the ADD result from stack!
SET_LOCAL i
UNWIND 1 1
POP
```

**Optimize to:**
```
GET_LOCAL i
CONST_BYTE 1
ADD
DUP               ← duplicate the result
SET_LOCAL i
POP
```

**Pattern 2: Block with single temp**
```
<expr that produces value>
GET_LOCAL slot
UNWIND 1 1
```

**Optimize to:**
```
<expr that produces value>
(value already on stack, no temp needed)
```

### Implementation

**Add `optimizeBytecode` pass in codegen:**

```lx
fn optimizeBytecode(chunk) {
  let bc = chunk.bytecode
  let i = 0
  let optimized = []

  while i < len(bc) {
    // Pattern: result on stack, GET_LOCAL temp, UNWIND
    if matchPattern(bc, i, TEMP_PATTERN) {
      // Skip the GET_LOCAL, keep the value on stack
      i = skipGetLocal(bc, i)
      // Transform UNWIND to just pop the temp slot
      i = transformUnwind(bc, i, optimized)
    } else {
      push(optimized, bc[i])
      i = i + 1
    }
  }

  chunk.bytecode = optimized
}
```

### Expected Impact

Same as Approach 1, but applied post-codegen.

### Pros
- ✓ Doesn't touch ANF (lower risk)
- ✓ Can optimize other patterns too
- ✓ Clear separation of concerns

### Cons
- ⚠️ More complex (need pattern matching on bytecode)
- ⚠️ Must update line numbers, debug info
- ⚠️ Harder to maintain (bytecode patterns change)
- ⚠️ Only optimizes what we think of (ANF approach is systematic)

### Risk Assessment
**Medium-High** - Bytecode manipulation is error-prone, but failures are caught by tests

---

## Recommendation

**Start with Approach 1 (Fix ANF)** for these reasons:

1. **Cleaner:** Solves the problem at the source
2. **Systematic:** Benefits all code patterns, not just the ones we optimize
3. **Simpler:** Less code than peephole optimizer
4. **Better errors:** ANF violations caught at compile time, not runtime

**Then add Approach 2 selectively** for patterns that can't be fixed in ANF.

---

## Implementation Plan (Approach 1) ✅

### Phase 1: Add `anfAssignmentValue` ✅
- [x] Add function in `anf.lx`
- [x] Handle atomic, simple binary, simple unary cases
- [x] Existing tests validate correctness

### Phase 2: Integrate into `anfAssignment` ✅
- [x] Modify `anfAssignment` to use new function
- [x] Only wrap in block if prelude exists
- [x] Assignments without temps don't create blocks

### Phase 3: Verify Codegen Handles It ✅
- [x] Verified `compileAssignment` handles non-atomic RHS
- [x] Works as expected (compiles `node.value` generically)

### Phase 4: Test & Profile ✅
- [x] All existing tests pass
- [x] Bytecode verified in benchmarks
- [x] Superinstructions added for further optimization

### Actual Changes
- anf.lx: +41 lines (`anfAssignmentValue` function)
- types.lx: +3 opcodes (superinstructions)
- codegen.lx: Pattern detection for superinstructions
- vm.c: VM implementation for superinstructions
- verify-bytecode.lx: Stack effects and instruction lengths

---

## Success Criteria ✅

### Achieved Results:

1. **Bytecode for `i = i + 1`:** ✅
   - Before ANF fix: 8 ops (with block wrapper)
   - After ANF fix: 6 ops (no block)
   - With ADD_LOCAL_IMM: **1 op** (87.5% reduction)

   ```
   ADD_LOCAL_IMM slot imm
   ```

2. **Bytecode for `arr[i] = val` (all locals):** ✅
   - Before: 8 ops (with block wrapper)
   - After ANF fix: 6 ops
   - With STORE_IDX_LOCAL: **1 op** (87.5% reduction)

   ```
   STORE_IDX_LOCAL arr_slot idx_slot val_slot
   ```

3. **All tests pass** ✅
4. **Optimizations verified in bytecode** ✅
   - Confirmed in benchmarks/lx/array_fill.lx disassembly

---

## Fallback Plan

If Approach 1 reveals unexpected issues:
- Revert ANF changes
- Implement Approach 2 (peephole) for assignment pattern only
- Re-evaluate after seeing results
