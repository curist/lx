# Phase 2B Handoff: Remaining Work (VM Side)

## Current Status

### ✅ Completed (Compiler Side)

**Opcodes:**
- `OP_FORPREP` and `OP_FORLOOP` added to `include/chunk.h` (lines 104-105)
- Added to `lx/src/types.lx` OP enum (lines 186-187)

**Pattern Matching (`lx/src/passes/backend/codegen.lx`):**
- Extended `matchNumericForLoop()` to accept `>` and `>=` comparisons (cmp_kind 2, 3)
- Extract step value from `i = i + N` or `i = i - N` (lines 1058-1085)
- Validate step range: -128 to 127 (int8_t)
- Validate step direction matches comparison:
  - Negative step requires `>` or `>=`
  - Positive step requires `<` or `<=`
- Return `use_fast_path` flag: true for step=1, false otherwise

**Bytecode Emission (`lx/src/passes/backend/codegen.lx`):**
- Choose opcode based on `use_fast_path` (lines 1187-1189)
- Emit step byte for non-fast-path (lines 1198-1203, 1221-1225)
- Instruction lengths: 6 bytes (_1 variants), 7 bytes (parametric variants)

**Bytecode Verifier (`lx/src/passes/backend/verify-bytecode.lx`):**
- STACK_MIN: both 0 (operate on locals only)
- STACK_EFFECTS: both 0 (stack-neutral)
- INSTRUCTION_LENGTHS: both 7

**Build Status:**
- Compiler builds successfully with `make`
- Will generate OP_FORPREP/OP_FORLOOP bytecode for non-1 steps
- **Runtime will crash** if loops with non-1 steps are executed (VM doesn't handle them yet)

---

## ❌ Remaining Work

### 1. VM Handlers (src/vm.c)

**Location:** After `OP_FORLOOP_1` case in the VM dispatch loop

**Implementation:**

```c
case OP_FORPREP: {
  uint8_t i_slot = READ_BYTE();
  uint8_t limit_slot = READ_BYTE();
  uint8_t cmp_kind = READ_BYTE();
  int8_t step = (int8_t)READ_BYTE();  // Signed step (-128 to 127)
  uint16_t offset = READ_SHORT();

  Value i = slots[i_slot];
  Value limit = slots[limit_slot];

  // Validate types
  if (!IS_FIXNUM(i)) {
    RUNTIME_ERROR("For loop variable must be a fixnum.");
    goto error;
  }
  if (!IS_NUMBER(limit)) {
    RUNTIME_ERROR("For loop limit must be numeric.");
    goto error;
  }

  // Check initial entry condition
  int64_t i_int = AS_FIXNUM(i);
  double limit_dbl = AS_NUMBER(limit);
  bool shouldEnter = false;

  switch (cmp_kind) {
    case 0: shouldEnter = ((double)i_int < limit_dbl); break;   // <
    case 1: shouldEnter = ((double)i_int <= limit_dbl); break;  // <=
    case 2: shouldEnter = ((double)i_int > limit_dbl); break;   // >
    case 3: shouldEnter = ((double)i_int >= limit_dbl); break;  // >=
    default:
      RUNTIME_ERROR("Invalid comparison kind in FORPREP.");
      goto error;
  }

  if (!shouldEnter) {
    // Skip loop body by jumping forward
    next_ip = ip + offset;
  }

  DISPATCH();
}

case OP_FORLOOP: {
  uint8_t i_slot = READ_BYTE();
  uint8_t limit_slot = READ_BYTE();
  uint8_t cmp_kind = READ_BYTE();
  int8_t step = (int8_t)READ_BYTE();  // Signed step
  uint16_t offset = READ_SHORT();

  Value i = slots[i_slot];
  Value limit = slots[limit_slot];

  // Extract i as int64
  int64_t i_int = AS_FIXNUM(i);

  // Increment with signed step
  i_int += step;

  // Check for fixnum overflow
  if (!fixnumFitsInt64(i_int)) {
    RUNTIME_ERROR("For loop counter overflow.");
    goto error;
  }

  // Store incremented value
  slots[i_slot] = FIXNUM_VAL(i_int);

  // Check loop continuation condition
  double limit_dbl = AS_NUMBER(limit);
  bool shouldContinue = false;

  switch (cmp_kind) {
    case 0: shouldContinue = ((double)i_int < limit_dbl); break;   // <
    case 1: shouldContinue = ((double)i_int <= limit_dbl); break;  // <=
    case 2: shouldContinue = ((double)i_int > limit_dbl); break;   // >
    case 3: shouldContinue = ((double)i_int >= limit_dbl); break;  // >=
    default:
      RUNTIME_ERROR("Invalid comparison kind in FORLOOP.");
      goto error;
  }

  if (shouldContinue) {
    // Jump backward to loop body
    next_ip = ip - offset;
  }

  DISPATCH();
}
```

**Key Points:**
- `int8_t` cast for step byte (handles negative values via two's complement)
- Signed arithmetic: `i_int += step` works for both positive and negative steps
- Same cmp_kind switch for both FORPREP and FORLOOP
- Overflow check after increment (fixnum range validation)

**Reference:** Existing `OP_FORPREP_1` and `OP_FORLOOP_1` handlers (similar structure)

---

### 2. Disassembler (src/debug.c)

**Location:** Update `forLoopInstruction()` function to handle step parameter

**Current Implementation:**
```c
static int forLoopInstruction(const char* name, int sign, Chunk* chunk, int offset) {
  uint8_t i_slot = chunk->code[offset + 1];
  uint8_t limit_slot = chunk->code[offset + 2];
  uint8_t cmp_kind = chunk->code[offset + 3];
  uint16_t jump = (uint16_t)(chunk->code[offset + 4] << 8);
  jump |= chunk->code[offset + 5];

  const char* cmp_str;
  switch (cmp_kind) {
    case 0: cmp_str = "<"; break;
    case 1: cmp_str = "<="; break;
    default: cmp_str = "?"; break;
  }

  printf("%-19s i=%d limit=%d %s -> %d\n", name, i_slot, limit_slot,
         cmp_str, offset + 6 + sign * jump);
  return offset + 6;
}
```

**Updated Implementation:**
```c
static int forLoopInstruction(const char* name, int sign, Chunk* chunk, int offset) {
  uint8_t i_slot = chunk->code[offset + 1];
  uint8_t limit_slot = chunk->code[offset + 2];
  uint8_t cmp_kind = chunk->code[offset + 3];

  // Check if this is a stepped variant (OP_FORPREP/OP_FORLOOP)
  OpCode op = chunk->code[offset];
  bool has_step = (op == OP_FORPREP || op == OP_FORLOOP);

  int8_t step = 1;
  int offset_idx = 4;
  if (has_step) {
    step = (int8_t)chunk->code[offset + 4];  // Signed step
    offset_idx = 5;
  }

  uint16_t jump = (uint16_t)(chunk->code[offset + offset_idx] << 8);
  jump |= chunk->code[offset + offset_idx + 1];

  const char* cmp_str;
  switch (cmp_kind) {
    case 0: cmp_str = "<"; break;
    case 1: cmp_str = "<="; break;
    case 2: cmp_str = ">"; break;   // NEW
    case 3: cmp_str = ">="; break;  // NEW
    default: cmp_str = "?"; break;
  }

  int instruction_length = has_step ? 7 : 6;
  printf("%-19s i=%d limit=%d %s step=%d -> %d\n", name, i_slot, limit_slot,
         cmp_str, step, offset + instruction_length + sign * jump);
  return offset + instruction_length;
}
```

**Switch Cases:**
```c
case OP_FORPREP_1:
  return forLoopInstruction("OP_FORPREP_1", 1, chunk, offset);
case OP_FORLOOP_1:
  return forLoopInstruction("OP_FORLOOP_1", -1, chunk, offset);
case OP_FORPREP:
  return forLoopInstruction("OP_FORPREP", 1, chunk, offset);
case OP_FORLOOP:
  return forLoopInstruction("OP_FORLOOP", -1, chunk, offset);
```

**Key Changes:**
- Detect stepped variant by checking opcode
- Read step byte as `int8_t` (handles negative)
- Adjust offset_idx for jump operand
- Display step value in output
- Add `>` and `>=` to cmp_str switch
- Return correct instruction length (6 or 7)

---

### 3. Tests

**Test File:** `lx/test/phase2b-signed-steps.test.lx`

```lx
let suite = (import "test/makeTestSuite.lx")()
fn test(name, cb) { suite.defineTest("phase2b-signed-steps - " + name, cb) }

// Phase 2B: Signed constant steps

test("countdown loop with i = i - 1", fn(assert) {
  let sum = 0
  for let i = 10; i > 0; i = i - 1 {
    sum = sum + i
  }
  assert.equal(sum, 55)  // 10+9+8+...+1
})

test("countdown loop with >= comparison", fn(assert) {
  let sum = 0
  for let i = 5; i >= 0; i = i - 1 {
    sum = sum + i
  }
  assert.equal(sum, 15)  // 5+4+3+2+1+0
})

test("step by 2 ascending", fn(assert) {
  let result = []
  for let i = 0; i < 10; i = i + 2 {
    push(result, i)
  }
  assert.equal(len(result), 5)
  assert.equal(result[0], 0)
  assert.equal(result[1], 2)
  assert.equal(result[4], 8)
})

test("step by 3 ascending", fn(assert) {
  let sum = 0
  for let i = 0; i < 20; i = i + 3 {
    sum = sum + 1
  }
  assert.equal(sum, 7)  // 0, 3, 6, 9, 12, 15, 18
})

test("step by -2 descending", fn(assert) {
  let result = []
  for let i = 10; i > 0; i = i - 2 {
    push(result, i)
  }
  assert.equal(len(result), 5)
  assert.equal(result[0], 10)
  assert.equal(result[4], 2)
})

test("step by -5 descending", fn(assert) {
  let count = 0
  for let i = 100; i >= 0; i = i - 5 {
    count = count + 1
  }
  assert.equal(count, 21)  // 100, 95, ..., 5, 0
})

test("large positive step", fn(assert) {
  let count = 0
  for let i = 0; i < 1000; i = i + 100 {
    count = count + 1
  }
  assert.equal(count, 10)  // 0, 100, 200, ..., 900
})

test("maximum positive step (127)", fn(assert) {
  let count = 0
  for let i = 0; i < 1000; i = i + 127 {
    count = count + 1
  }
  assert.equal(count, 8)  // 0, 127, 254, ..., 889
})

test("maximum negative step (-128)", fn(assert) {
  let count = 0
  for let i = 0; i > -1000; i = i - 128 {
    count = count + 1
  }
  assert.equal(count, 8)  // 0, -128, -256, ..., -896
})

test("empty loop with negative step", fn(assert) {
  let count = 0
  for let i = 0; i > 10; i = i - 1 {
    count = count + 1
  }
  assert.equal(count, 0)  // Never enters
})

test("nested loops with different steps", fn(assert) {
  let sum = 0
  for let i = 0; i < 10; i = i + 2 {
    for let j = 10; j > 0; j = j - 2 {
      sum = sum + 1
    }
  }
  assert.equal(sum, 25)  // 5 outer * 5 inner
})

test("countdown with break", fn(assert) {
  let sum = 0
  for let i = 10; i > 0; i = i - 1 {
    if i == 5 {
      break
    }
    sum = sum + i
  }
  assert.equal(sum, 40)  // 10+9+8+7+6
})

test("step-by-N with continue", fn(assert) {
  let sum = 0
  for let i = 0; i < 20; i = i + 3 {
    if i == 9 {
      continue
    }
    sum = sum + i
  }
  assert.equal(sum, 51)  // 0+3+6+12+15+18 (skip 9)
})

suite.run()
```

**Verification Tests:**

Create `lx/test-phase2b-disasm.lx` to verify correct opcodes:
```lx
// Verify disassembly shows correct opcodes and step values

let driver = import "src/driver.lx"
let ModuleResolution = import "src/module_resolution.lx"

fn testDisasm(name, source, expectedOpcodes) {
  let testPath = "/tmp/phase2b-disasm.lx"
  exec("cat > " + testPath, .{ input: source })

  // Compile and get disassembly
  exec("./out/lx disasm " + testPath, .{ capture: true })
  // Would need to parse output and verify opcodes
  println("✓ " + name)
}

testDisasm("Step +2 uses OP_FORPREP/OP_FORLOOP", "
fn test() {
  for let i = 0; i < 10; i = i + 2 {
    println(i)
  }
}
", ["OP_FORPREP", "OP_FORLOOP"])

testDisasm("Step -1 uses OP_FORPREP/OP_FORLOOP", "
fn test() {
  for let i = 10; i > 0; i = i - 1 {
    println(i)
  }
}
", ["OP_FORPREP", "OP_FORLOOP"])

testDisasm("Step +1 uses OP_FORPREP_1/OP_FORLOOP_1", "
fn test() {
  for let i = 0; i < 10; i = i + 1 {
    println(i)
  }
}
", ["OP_FORPREP_1", "OP_FORLOOP_1"])
```

---

### 4. Bootstrap Process

**Why Bootstrap Needed:**
- New opcodes added to `chunk.h`
- Compiler source (`codegen.lx`) modified to emit new opcodes
- Embedded bytecode headers must be regenerated

**Steps:**

```bash
# 1. Build with system compiler (lx in PATH)
make clean
make

# 2. Test that Phase 1 still works (step=1 loops)
./out/lx run lx/test/fused-for-loop.test.lx

# 3. Rebuild with newly built compiler
LX=./out/lx make -B prepare build

# 4. Test Phase 2B loops
./out/lx run lx/test/phase2b-signed-steps.test.lx

# 5. Verify stability (build should be reproducible)
LX=./out/lx make -B prepare build

# 6. Run full test suite
make test
```

**Expected Results:**
- fused-for-loop.test.lx: 20/20 tests pass (unchanged)
- phase2b-signed-steps.test.lx: 13/13 tests pass (new)
- All existing tests continue to pass

---

## Testing Strategy

### Manual Verification

**Test countdown loop:**
```bash
cat > /tmp/test-countdown.lx << 'EOF'
let sum = 0
for let i = 10; i > 0; i = i - 1 {
  println(i)
  sum = sum + i
}
println("Sum: " + str(sum))
EOF

./out/lx run /tmp/test-countdown.lx
# Expected output: 10, 9, 8, ..., 1, Sum: 55
```

**Test disassembly:**
```bash
./out/lx disasm /tmp/test-countdown.lx | grep FOR
# Expected: OP_FORPREP with step=-1, OP_FORLOOP with step=-1
```

**Test step-by-N:**
```bash
cat > /tmp/test-step2.lx << 'EOF'
for let i = 0; i < 10; i = i + 2 {
  println(i)
}
EOF

./out/lx run /tmp/test-step2.lx
# Expected output: 0, 2, 4, 6, 8
```

---

## Known Issues & Gotchas

### 1. Step Range Limitation
- Step must fit in int8_t: -128 to 127
- Loops with steps outside this range fall back to generic loop
- Not an error, just not optimized

### 2. Comparison Direction Validation
- Pattern matching enforces: negative step requires `>` or `>=`
- Positive step requires `<` or `<=`
- Mismatched combinations rejected (infinite loop prevention)

### 3. Fixnum Overflow
- VM checks `fixnumFitsInt64()` after increment
- Runtime error if overflow occurs
- Affects very long loops with large initial values

### 4. Zero Step
- Pattern matching rejects `i = i + 0` (would infinite loop)

### 5. Fast Path Optimization
- Step=1 uses `OP_FORPREP_1`/`OP_FORLOOP_1` (6-byte instructions)
- Other steps use `OP_FORPREP`/`OP_FORLOOP` (7-byte instructions)
- Codegen automatically chooses based on `use_fast_path` flag

---

## Code Locations Reference

**C Files (VM/Debug):**
- `src/vm.c`: VM execution loop, add cases after line ~800 (after OP_FORLOOP_1)
- `src/debug.c`: Disassembler, update forLoopInstruction() around line ~250

**Lx Files (Already Complete):**
- `lx/src/passes/backend/codegen.lx`: Pattern matching and emission
- `lx/src/passes/backend/verify-bytecode.lx`: Bytecode verifier tables
- `lx/src/types.lx`: OP enum

**Headers (Already Complete):**
- `include/chunk.h`: OpCode enum

**Tests (To Create):**
- `lx/test/phase2b-signed-steps.test.lx`: Main test suite
- `lx/test-phase2b-disasm.lx`: Disassembly verification (optional)

---

## Success Criteria

- [ ] VM handlers implemented without crashes
- [ ] Disassembler shows correct step values
- [ ] All 13 Phase 2B tests pass
- [ ] All existing tests still pass (no regressions)
- [ ] Bootstrap succeeds (compiler can rebuild itself)
- [ ] Build is stable (3 consecutive rebuilds succeed)

---

## Next Steps After Phase 2B

With Phase 2B complete, the fused loop optimization will support:
- ✅ Integer init
- ✅ Variable limit (identifier only)
- ✅ Signed constant steps (-128 to 127)
- ✅ Comparisons: `<`, `<=`, `>`, `>=`

**Phase 2A (deferred):** General limit expressions (function calls, arithmetic)
- Requires solving ANF Block scope issues
- More complex, lower priority
- Can be revisited after Phase 2B is stable

**Phase 3 (optional):** Further optimizations
- Variable steps
- Multiple increment forms (e.g., `i += 2`)
- Float/double support
