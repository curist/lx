# LX VM Quicken Spec (Bytecode Specialization with Deopt)

## Goals

1. **Reduce runtime type-check overhead** on hot bytecode sites (especially `OP_ADD`, `OP_GET_BY_INDEX`, `OP_SET_BY_INDEX`, numeric comparisons).
2. Maintain **semantic equivalence** with baseline interpreter:
   * No changes to bytecode length or jump offsets.
   * Any failed assumption **deopts** to baseline execution and continues correctly.
3. Keep changes localized:
   * Minimal modifications to `runUntil()` dispatch loop.
   * Per-function metadata (hot counters, quickened opcode overlay).
4. Support future extensions (inline caches, loop-triggered quickening, trace JIT), without forcing them now.

## Non-goals (for initial implementation)

* No native code generation.
* No trace recording / loop trace execution.
* No cross-instruction fusion (superinstructions) at runtime.

---

# High-level Design

Quicken is implemented as an **overlay opcode stream** per function:

* Baseline bytecode remains in `fn->chunk.code` (immutable or treated as canonical).
* A lazily-allocated `fn->qcode` (same length as `chunk.count`) stores the effective opcode for each byte offset.
* Dispatch uses baseline to advance `ip` as today, but selects the opcode from `qcode` when present.

When a site becomes hot, we rewrite `qcode[ipOff]` from a generic opcode (e.g. `OP_ADD`) to a specialized opcode (e.g. `OP_ADD_NUM`). Specialized opcodes include **fast guards**; on guard failure they **deopt** the site back to baseline (`qcode[ipOff] = OP_ADD`) and re-dispatch.

This yields:

* no bytecode length changes,
* no jump offset rewriting,
* deterministic correctness: any miss falls back to baseline logic.

---

# Data Structures

## ObjFunction fields

Add a small "quicken" struct to `ObjFunction`:

```c
typedef struct {
  uint16_t* hot;     // saturating counters per byte offset (chunk.count entries)
  uint8_t*  qcode;   // quickened opcode per byte offset (chunk.count entries), NULL until enabled
} Quicken;
```

And in `ObjFunction`:

```c
Quicken quicken;
```

### Allocation policy

* `hot` is allocated lazily the first time the function executes (or upon first threshold check).
* `qcode` is allocated lazily when the first site in the function is quickened.

Memory note: `uint16_t hot[chunk.count]` is usually enough; saturate at 65535.

---

# Dispatch Semantics

## Computing ipOff

**CRITICAL TIMING:** Compute `ipOff` BEFORE reading operands:

```c
uint8_t* ipAtOpcode = frame->ip;  // Save IP before READ_BYTE
uint8_t raw = READ_BYTE();         // This advances ip
size_t ipOff = (size_t)(ipAtOpcode - fn->chunk.code);

// Now read operands with READ_BYTE/READ_SHORT as normal
```

**Why?** Because some opcodes have operands, and you need `ipOff` to point at the **opcode byte**, not at an operand byte.

## Effective opcode selection

Pseudo:

```c
uint8_t raw = READ_BYTE();
size_t ipOff = (size_t)(frame->ip - fn->chunk.code - 1);

uint8_t op = raw;
if (fn->quicken.qcode != NULL) op = fn->quicken.qcode[ipOff];
```

Important: we do **not** read operands from `qcode`. Operands remain in baseline `chunk.code` and are read via `READ_BYTE/READ_SHORT` as today. This is why quickened opcodes must preserve operand layout (or use none).

---

# Hotness & Triggering

## Thresholds

Use a small set of constants:

```c
#define QUICKEN_THRESHOLD  4096   // Start with this value
```

**Rationale for 4096:**
* Small functions might execute <1000 times total
* You want to quicken **hot loops**, not cold startup code
* Higher threshold = fewer wasted quickenings = less deopt noise

## Updating counters

At each executed instruction:

* If `hot` is enabled for this function, increment `hot[ipOff]` (saturating).
* If counter crosses threshold and opcode is eligible, call `quickenAt(fn, ipOff, raw)`.

Activation policy:

* Either allocate `hot[]` immediately for all functions, or
* allocate `hot[]` the first time a function is called (recommended), or
* allocate only when `QUICKEN` is enabled (good for development).

---

# Quicken API

## `ensureQuickenHot(fn)`

Allocates `hot[]` if NULL.

## `ensureQuickenQCode(fn)`

Allocates `qcode[]` if NULL, and initializes:

```c
memcpy(fn->quicken.qcode, fn->chunk.code, fn->chunk.count);
```

This ensures that by default `qcode[i] == baseline opcode byte`.

## `quickenAt(fn, ipOff, rawOpcode)`

Decides whether/how to specialize the opcode at `ipOff`, based on the opcode and runtime operand tags currently on the value stack (via `peek()`).

**Quicken is conservative:**

* Only specialize when the observed types correspond to a meaningful fast path.
* Otherwise do nothing.

---

# Specialized Opcodes

Specialized opcodes must satisfy:

1. Same operand layout as baseline opcode at that site (often "no operands").
2. Fast guard using `peek` (no stack mutation before guard).
3. On guard failure: deopt `qcode[ipOff]` back to baseline opcode and re-dispatch from the same site.

## Initial opcode set (Phase 1)

### Arithmetic

* `OP_ADD_NUM` : assumes two numbers
* `OP_ADD_STR` : assumes two strings

### Indexing

* `OP_GET_ARRAY_NUM`    : container is array, key is integer-ish number
* `OP_GET_HASHMAP_STR`  : container is hashmap, key is string
* `OP_GET_HASHMAP_NUM`  : container is hashmap, key is number
* `OP_GET_STRING_NUM`   : container is string, key is number
* (optional) enum variants later

### Mutating indexing

* `OP_SET_ARRAY_NUM`
* `OP_SET_HASHMAP_STR`
* `OP_SET_HASHMAP_NUM`

You can add numeric compares (`OP_LESS_NUM`, etc.) immediately after these.

---

# Deopt Mechanics

### Requirements

* Deopt must not change VM-visible state (stack, locals, ip).
* After deopt, execution should proceed as if the baseline opcode executed at that site.

### Implementation pattern

**CRITICAL: Guards must happen BEFORE any stack mutation:**

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

This requires your dispatch to support a re-entry label:

```c
dispatch:
switch (op) { ... }
```

and your main loop to set `op` and jump to `dispatch` each iteration.

---

# Concrete Pseudocode (VM Loop Skeleton)

This shows only the relevant scaffolding.

```c
for (;;) {
  uint8_t* ipAtOpcode = frame->ip;
  uint8_t raw = READ_BYTE();
  size_t ipOff = (size_t)(ipAtOpcode - fn->chunk.code);

  // hotness
  if (fn->quicken.hot != NULL) {
    uint16_t c = fn->quicken.hot[ipOff];
    if (c != 0xFFFF) fn->quicken.hot[ipOff] = c + 1;
    if (c + 1 == QUICKEN_THRESHOLD) {
      quickenAt(fn, ipOff, raw);
    }
  }

  uint8_t op = raw;
  if (fn->quicken.qcode != NULL) op = fn->quicken.qcode[ipOff];

dispatch:
  switch (op) {
    case OP_ADD_NUM: {
      Value b = peek(0);
      Value a = peek(1);
      if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
        pop(); pop();
        push(NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b)));
        break;
      }
      fn->quicken.qcode[ipOff] = OP_ADD; // deopt
      op = OP_ADD;
      goto dispatch;
    }

    case OP_ADD: {
      // existing slow path unchanged
      ...
    }

    ...
  }
}
```

---

# `quickenAt()` Decision Rules

The quicken pass uses the **current operand tags** (observed at runtime) to choose a specialization.

## For `OP_ADD`

* if `peek(0)` and `peek(1)` are both numbers → quicken to `OP_ADD_NUM`
* else if both strings → quicken to `OP_ADD_STR`
* else do nothing (keep baseline)

## For `OP_GET_BY_INDEX`

Let:

* container = `peek(1)`
* key = `peek(0)`

Rules:

* if array + number → `OP_GET_ARRAY_NUM`
* else if hashmap + string → `OP_GET_HASHMAP_STR`
* else if hashmap + number → `OP_GET_HASHMAP_NUM`
* else if string + number → `OP_GET_STRING_NUM`
* else do nothing

## For `OP_SET_BY_INDEX`

Let:

* container = `peek(2)`
* key = `peek(1)`
* value = `peek(0)`

Rules:

* if array + number → `OP_SET_ARRAY_NUM`
* else if hashmap + string → `OP_SET_HASHMAP_STR`
* else if hashmap + number → `OP_SET_HASHMAP_NUM`
* else do nothing

When specializing, ensure `qcode` exists:

* `ensureQuickenQCode(fn);`
* set `fn->quicken.qcode[ipOff] = specializedOpcode;`

---

# Semantics for "integer-ish" array index guards

Your baseline semantics for arrays require:

* key must be number
* key must be integer (checked via `(double)index == AS_NUMBER(key)`)

Specialized array ops must do the same. Guard should be:

1. `IS_NUMBER(key)`
2. convert to `int index = (int)num;`
3. check `(double)index == num`

If fails, deopt.

---

# Instrumentation & Flags

Add build flags:

* `-DQUICKEN` to compile in quicken support.
* `-DQUICKEN_STATS` to print:
  * number of sites quickened per opcode kind,
  * deopt counts per specialized opcode.

```c
#ifdef QUICKEN_STATS
typedef struct {
  uint64_t quickenAttempts;
  uint64_t quickenSuccess;
  uint64_t deopts[256];  // Per specialized opcode
} QuickenStats;
```

Add runtime env var or VM flag (optional):

* `LX_QUICKEN=0/1` to disable without rebuild.

---

# GC Considerations

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

---

# Function Pointer Refreshing

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

---

# Actionable Implementation Plan

## Phase 0 — Preconditions (0.5 day)

* Confirm you can extend `ObjFunction` safely (GC marking if needed).
* Decide allocation strategy: `hot` in `malloc`, `qcode` in `malloc`.
* Add build flags: `-DQUICKEN` and `-DQUICKEN_STATS`.

## Phase 0.5 — IP offset validation (0.5 day)

* Add `ipOff` computation and print it in `DEBUG_TRACE_EXECUTION`.
* Verify it matches disassembly offsets.
* This catches offset bugs EARLY before quickening.

## Phase 1 — Minimal Scaffolding + OP_ADD_NUM (1 day)

1. Add `Quicken` struct and fields to `ObjFunction`.
2. Initialize fields to NULL when creating/reading functions (`loadObj`, function init).
3. Add `ensureQuickenHot(fn)` and `ensureQuickenQCode(fn)`.
4. Add per-instruction `ipOff` computation and `hot` increment plumbing.
5. Add `dispatch:` label so specialized ops can deopt and re-dispatch.
6. Implement `OP_ADD_NUM` only (skip OP_ADD_STR for now).
7. Add `quickenAt(fn, ipOff, raw)` that only handles `OP_ADD` → `OP_ADD_NUM`.

Acceptance criteria:

* With quicken enabled, all tests still pass.
* With quicken disabled, no perf regressions.
* A numeric microbenchmark shows reduced slow `OP_ADD` executions.

## Phase 2 — OP_GET_ARRAY_NUM (1 day)

1. Add `OP_GET_ARRAY_NUM` opcode to `chunk.h`.
2. Implement opcode case in VM with guard + fast path + deopt.
3. Extend `quickenAt` for `OP_GET_BY_INDEX` (array + number case only).

Acceptance criteria:

* Array access loops show reduced overhead.
* Test that changes array to hashmap triggers deopt and returns correct output.

## Phase 3 — Rest of indexing + OP_ADD_STR (1–2 days)

1. Add remaining GET/SET variants:
   * `OP_GET_HASHMAP_STR`, `OP_GET_HASHMAP_NUM`, `OP_GET_STRING_NUM`
   * `OP_SET_ARRAY_NUM`, `OP_SET_HASHMAP_STR`, `OP_SET_HASHMAP_NUM`
2. Add `OP_ADD_STR` for string concatenation.
3. Implement specialized op cases with guards and same semantics.
4. Extend `quickenAt` for all GET/SET variants.

Acceptance criteria:

* `map_hit_miss` style loops show reduced overhead.
* Polymorphic tests trigger deopts and return correct results.

## Phase 4 — Stats & Tuning (0.5 day)

1. Add counters for:
   * quicken attempts
   * successful quickens
   * deopts per specialized opcode
2. Tune threshold based on self-hosted compiler workload.
3. Print stats at VM shutdown if `QUICKEN_STATS` enabled.

Acceptance criteria:

* You can report "sites quickened" and "deopts" after running the compiler.
* Quicken success rate is >90%.

**Estimated total time: 4-6 days for phases 0-4**

---

# Testing Plan

## Correctness tests (must-have)

1. **Polymorphic add**
   * Loop where same `OP_ADD` site sees numbers then strings; ensure it deopts and continues correct.

2. **Array index integer check**
   * Attempt `arr[1.2]` should error (baseline semantics). Ensure specialized op deopts then errors, not silently truncates.

3. **Hashmap key type flip**
   * Same site used with string then number keys; ensure deopt, correct results.

4. **pcall boundary**
   * Errors thrown through specialized op still propagate correctly and do not corrupt stacks.

5. **Quickening stability test**
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
   Expected: 2 quickenings, 1 deopt, correct results.

## Perf microbenchmarks

* `sum_loop` - numeric add in tight loop
* `array_fill` - array indexing
* `map_hit_miss` - hashmap access
* Self-hosted compiler codegen pass

Metrics:

* Runtime (should improve 10-30% on numeric loops)
* Total executed ops (optional)
* Deopt count (should be low in hot loops)

---

# Tricky Parts to Watch

1. **`ipOff` must be computed before operand reads**
   * Save `frame->ip` BEFORE `READ_BYTE()`

2. **Function pointer must be refreshed after OP_CALL**
   * `fn = closure->function;` after call setup

3. **Guards must happen before any stack mutation**
   * Use `peek()` for guards, only `pop()` after guard passes

4. **`goto dispatch` needs a `dispatch:` label before `switch (op)`**
   * Place label immediately before switch statement

5. **Deopt must restore exact baseline opcode**
   * `fn->quicken.qcode[ipOff] = raw;` not a hardcoded value

---

# Notes on Future Extensions (not now, but compatible)

* **Loop-triggered quickening**: Increment backedge counters on `OP_LOOP`, and when hot, lower thresholds inside the loop region.
* **Inline caches**: Store per-site cache (object identity, shape id) in a side array indexed by `ipOff`. Requires GC marking discipline.
* **Superinstruction padding**: If you later want runtime fusion without rewriting jumps, prefer a dedicated `OP_SKIP n` rather than raw NOP islands.
* **Trace compilation**: Record hot traces and compile to specialized bytecode sequences.

---

## Implementation Status

- [ ] Phase 0: Preconditions
- [ ] Phase 0.5: IP offset validation
- [ ] Phase 1: Scaffolding + OP_ADD_NUM
- [ ] Phase 2: OP_GET_ARRAY_NUM
- [ ] Phase 3: Full indexing + OP_ADD_STR
- [ ] Phase 4: Stats & tuning
