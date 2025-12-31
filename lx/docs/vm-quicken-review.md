# VM Quicken Implementation Review

## Design Validation

✅ **The quicken spec is implementation-ready.** The overlay approach is exactly right for avoiding jump offset rewrites.

## Critical Implementation Details

### 1. IP Offset Timing (MOST CRITICAL)

The spec shows:
```c
uint8_t raw = READ_BYTE();
size_t ipOff = (size_t)(frame->ip - fn->chunk.code - 1);
```

This is correct AFTER `READ_BYTE()` advances `ip`, but be careful with operand reading:

**Recommended approach:**
```c
// At the TOP of the loop, before anything else:
uint8_t* ipAtOpcode = frame->ip;  // Save IP before READ_BYTE
uint8_t raw = READ_BYTE();         // This advances ip
size_t ipOff = (size_t)(ipAtOpcode - fn->chunk.code);

// Now read operands with READ_BYTE/READ_SHORT as normal
```

**Why?** Because some opcodes have operands, and you need `ipOff` to point at the **opcode byte**, not at an operand byte.

### 2. Operand Reading Consistency

The spec says:
> "we do not read operands from qcode. Operands remain in baseline chunk.code"

This is **automatically true** because `READ_BYTE()` uses `frame->ip++`, which always points into `chunk.code`. The only thing read from `qcode` is the opcode itself. ✅

### 3. Guard Order Matters (CRITICAL FOR CORRECTNESS)

**Deopt pattern must happen BEFORE any stack mutation:**

```c
case OP_ADD_NUM: {
  // ✅ GOOD: peek doesn't mutate
  Value a = peek(1);
  Value b = peek(0);
  if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
    // NOW safe to mutate
    pop(); pop();
    push(NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b)));
    break;
  }
  // Deopt: stack unchanged, safe to re-dispatch
  fn->quicken.qcode[ipOff] = OP_ADD;
  op = OP_ADD;
  goto dispatch;
}
```

**Never do this:**
```c
// ❌ BAD: mutated before guard
double b = AS_NUMBER(pop());  // Stack changed!
double a = AS_NUMBER(pop());  // Stack changed!
if (!IS_NUMBER(...)) {  // Too late - stack corrupted
  // Can't safely deopt now
}
```

### 4. Function Pointer Caching

In your loop, you cache `frame->closure->function`. After quickening, you'll need:

```c
ObjFunction* fn = frame->closure->function;  // Cache this at loop start

// Access quicken data:
if (fn->quicken.qcode != NULL) op = fn->quicken.qcode[ipOff];
```

### 5. Function Pointer Refresh After OP_CALL

After `OP_CALL`, you update `frame`, `closure`, `slots`. You'll also need:

```c
case OP_CALL: {
  // ... existing call logic
  frame = &vm.frames[vm.frameCount - 1];
  closure = frame->closure;
  slots = frame->slots;
  fn = closure->function;  // ← ADD THIS
  break;
}
```

### 6. GC Considerations

Your `Quicken` struct has raw pointers:
```c
uint16_t* hot;
uint8_t*  qcode;
```

These are **not** GC objects, so:
- ✅ No GC marking needed (they're just arrays of primitives)
- ⚠️ Must free in `freeFunction()`:

```c
void freeFunction(ObjFunction* function) {
  freeChunk(&function->chunk);
  FREE_ARRAY(uint16_t, function->quicken.hot, function->chunk.count);
  FREE_ARRAY(uint8_t, function->quicken.qcode, function->chunk.count);
  FREE(ObjFunction, function);
}
```

## Threshold Tuning Recommendation

```c
#define QUICKEN_THRESHOLD  4096   // Start with this instead of 1024
```

**Rationale:**
- Small functions might execute <1000 times total
- You want to quicken **hot loops**, not cold startup code
- Higher threshold = fewer wasted quickenings = less deopt noise
- Can tune down later if needed

**Later enhancement:** Add a separate **loop backedge** counter for even better targeting.

## Testing Strategy Addition

Add one more critical test to the spec's test plan:

**Quickening Stability Test**
```lx
// Site that quickens, deopts, then re-quickens to same type
let arr = [1, 2, 3]
for let i = 0; i < 10000; i = i + 1 {
  if i == 5000 {
    arr = .{ "0": 99 }  // Force deopt (array→hashmap)
  }
  if i == 6000 {
    arr = [4, 5, 6]  // Causes re-quickening
  }
  let x = arr[0]  // This site should quicken, deopt, re-quicken
}
```

**Expected:** 2 quickenings, 1 deopt, correct results throughout.

## Stats to Add

```c
#ifdef QUICKEN_STATS
typedef struct {
  uint64_t quickenAttempts;
  uint64_t quickenSuccess;
  uint64_t deopts[256];  // Per specialized opcode
} QuickenStats;
```

This tells you:
- **Quicken success rate** (should be >90%)
- **Which specialized ops deopt most** (helps prioritize guards)
- **Total quickening activity** (helps tune thresholds)

## Refined Implementation Order

Slightly reordered from spec for faster validation:

**Phase 0.5: IP offset validation** (0.5 day)
- Add `ipOff` computation and print it in `DEBUG_TRACE_EXECUTION`
- Verify it matches disassembly offsets
- **This catches offset bugs EARLY before quickening**

**Phase 1: OP_ADD_NUM only** (1 day)
- Just numbers (skip OP_ADD_STR for now)
- Simpler guards, faster validation
- Run numeric microbenchmark
- Verify deopt works

**Phase 2: OP_GET_ARRAY_NUM** (1 day)
- Single indexing specialization
- Tests array access patterns (most common)
- Verify type-changing deopt

**Phase 3: Rest of indexing + OP_ADD_STR** (1-2 days)
- Complete the indexing specializations
- Add string concatenation
- Full test suite

**Phase 4: Stats + tuning** (0.5 day)
- Implement stats collection
- Tune threshold based on real workload
- Measure self-hosted compiler improvement

## Tricky Parts Checklist

Before implementing each specialized opcode, verify:

1. ✅ **`ipOff` computed before operand reads**
2. ✅ **Guards use `peek()`, not `pop()`**
3. ✅ **Stack mutations only after guard passes**
4. ✅ **Deopt restores exact baseline opcode**
5. ✅ **`goto dispatch` label exists**
6. ✅ **Function pointer refreshed after calls**

## Expected Performance Gains

Based on the type-check overhead analysis:

- **Numeric loops:** 20-40% faster
  - `OP_ADD`, `OP_MULTIPLY`, etc. skip 3+ type checks per op

- **Array access loops:** 30-50% faster
  - `OP_GET_BY_INDEX` skips ~8 type checks
  - `OP_SET_BY_INDEX` skips ~10 type checks

- **Hashmap string access:** 15-25% faster
  - Skips container type + key type checks

- **Self-hosted compiler:** 10-20% overall
  - Hottest paths (codegen, ANF) will see bigger gains
  - Cold paths (parse once) won't benefit much

## Validation Checklist

After implementing each phase:

- [ ] All existing tests pass
- [ ] No perf regression with quicken disabled
- [ ] Polymorphic test triggers deopt correctly
- [ ] Stack corruption test (random pop/push) passes
- [ ] pcall still catches errors correctly
- [ ] GC runs without crashes
- [ ] Stats show reasonable quicken/deopt ratio

## Future Optimization Opportunities

Once quickening is stable:

1. **Loop-aware quickening**
   - Track `OP_LOOP` backedges
   - Lower threshold for code inside loops
   - Expected: 2-3x faster hot loops

2. **Inline caching**
   - Cache hashmap lookup results
   - Reuse if object identity matches
   - Expected: 40-60% faster hashmap-heavy code

3. **Type-specialized locals**
   - `OP_GET_LOCAL_NUM`, `OP_SET_LOCAL_NUM`
   - Assumes local slot always holds number
   - Expected: 10-15% faster numeric code

4. **Trace compilation**
   - Record linear execution traces
   - Emit specialized bytecode for entire trace
   - Expected: 50-100% faster hot paths

## Common Pitfalls to Avoid

1. ❌ **Don't hardcode opcode values in deopt**
   ```c
   // BAD:
   fn->quicken.qcode[ipOff] = OP_ADD;  // What if raw was different?

   // GOOD:
   fn->quicken.qcode[ipOff] = raw;  // Use the saved baseline opcode
   ```

2. ❌ **Don't quicken before first execution**
   ```c
   // BAD: quicken at bytecode load time
   // GOOD: quicken after threshold executions
   ```

3. ❌ **Don't forget to update `fn` after frame changes**
   ```c
   // After OP_CALL, OP_RETURN:
   fn = frame->closure->function;  // Must refresh!
   ```

4. ❌ **Don't assume `qcode` is always present**
   ```c
   // GOOD:
   if (fn->quicken.qcode != NULL) op = fn->quicken.qcode[ipOff];

   // BAD:
   op = fn->quicken.qcode[ipOff];  // May be NULL!
   ```

## Summary

The quicken spec is solid and ready for implementation. Key success factors:

1. **Start small** - OP_ADD_NUM first, validate thoroughly
2. **Test deopt** - Polymorphic tests are critical
3. **Measure early** - Profile before/after each phase
4. **Guard carefully** - Stack must be unchanged before deopt
5. **Refresh pointers** - `fn` must be current after calls

**Estimated timeline: 4-6 days for full implementation**

The design is sound and the phasing is sensible. The overlay approach elegantly solves the jump offset problem while maintaining correctness through deoptimization.
