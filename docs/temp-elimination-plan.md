# Plan: Eliminate Unnecessary Temporary Variables

## Current Problem

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

## Implementation Plan (Approach 1)

### Phase 1: Add `anfAssignmentValue` (1 file)
- [ ] Add function in `anf.lx`
- [ ] Handle atomic, simple binary, simple unary cases
- [ ] Write unit tests

### Phase 2: Integrate into `anfAssignment` (1 file)
- [ ] Modify `anfAssignment` to use new function
- [ ] Only wrap in block if prelude exists
- [ ] Test that assignments without temps don't create blocks

### Phase 3: Verify Codegen Handles It (check existing)
- [ ] Verify `compileAssignment` handles non-atomic RHS
- [ ] It should already work (it compiles `node.value` generically)

### Phase 4: Test & Profile
- [ ] Run all existing tests
- [ ] Add regression tests for edge cases
- [ ] Profile sum_loop - expect ~800M ops (down from 1000M)
- [ ] Check other benchmarks

### Estimated LOC Changes
- anf.lx: +40 lines
- Tests: +50 lines
- Total: ~90 lines

### Time Estimate
- 2-3 hours to implement
- 1 hour to test thoroughly

---

## Success Criteria

After implementing Approach 1:

1. **sum_loop total ops: <850M** (currently 1000M)
   - Eliminate UNWIND/POP from both assignments in loop
   - Target: ~800M ops (20% reduction)

2. **Bytecode for `i = i + 1`:**
   ```
   GET_LOCAL i
   CONST_BYTE 1
   ADD
   SET_LOCAL i
   ```
   (4 ops instead of 8)

3. **All tests pass**
4. **No performance regression on other benchmarks**

---

## Fallback Plan

If Approach 1 reveals unexpected issues:
- Revert ANF changes
- Implement Approach 2 (peephole) for assignment pattern only
- Re-evaluate after seeing results
