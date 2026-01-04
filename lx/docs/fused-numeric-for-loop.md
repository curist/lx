# Fused Numeric `for` Loops (Lua-style) — Design Spec

## Goal

Reduce interpreter overhead for numeric `for` loops by replacing the current “condition + jump + update + loop” sequence with fused loop opcodes that:

- Eliminate most per-iteration dispatch for loop control
- Preserve existing language semantics (except where explicitly documented)
- Remain compatible with the existing stack-based VM and local-slot model
- Work well with fixnum + double numeric representation (mixed-mode comparisons)

Primary motivation: close a large portion of the performance gap vs Lua on loop-heavy code by cutting loop-control instruction count.

---

## Non-Goals

- Migrating the VM to register-based bytecode
- Requiring SSA/typed IR (may be future work)
- Adding new surface syntax
- Performing aggressive speculative JIT-style specialization (future phase)

---

## Background: Current Loop Overhead

Current lowering of:

```lx
for let i = start; i <= limit; i = i + 1 { body }
````

Typically emits per iteration:

* load i/limit, compare, NOT, conditional jump
* unconditional jump to body/increment glue
* increment + pop + backedge loop

This is often ~8–12 opcode dispatches per iteration in the interpreter.

---

## Design Overview

Introduce fused loop opcodes analogous to Lua’s numeric `for`:

* **FORPREP**: loop entry check and forward exit jump
* **FORLOOP**: induction update + termination test + backward jump to body

Key properties:

* **Stack-neutral**: opcodes operate on local slots; stack height unchanged
* **Fixnum induction variable**: `i` must be fixnum on the fast path
* **Limit may be fixnum or double**: supports common patterns where `limit` comes from `tonumber()` (double)
* **Direction derived from step sign** (Phase 2+): step > 0 uses `<`/`<=`, step < 0 uses `>`/`>=`
* **Conservative compilation**: if slot liveness or pattern requirements are not met, fall back to generic loop lowering

---

## VM Model Assumptions

### Locals / Slots

* Compiler uses 0-based slots; bytecode operands are 1-based:

  * `localSlot(slot) = slot + 1`
* VM indexes locals via `slots[bytecode_slot]` (consistent with other local ops).

### Numeric Representation

* Numbers are either:

  * **fixnum** (46-bit signed payload; ±35,184,372,088,832)
  * **double**
* Mixed-mode comparisons (fixnum vs double) are allowed.

### Falsy Semantics

* Only `false` and `nil` are falsy (not directly relevant to numeric loops, but important for consistency).

---

## Bytecode Specification (Phase 1)

### OP_FORPREP_1

**Purpose:** loop entry guard (step = +1)

**Encoding:**

```
[opcode]
[i_slot:u8]
[limit_slot:u8]
[cmp_kind:u8]   // 0 = LT (<), 1 = LE (<=)
[offset:u16]    // forward jump distance to exit
```

**Runtime behavior:**

* `i` must be fixnum, else runtime error or bail out (see Overflow/Deopt policy).
* `limit` must be numeric (fixnum or double), else error/bail out.
* If condition fails, `ip += offset` (skip loop body). Else fall through.

### OP_FORLOOP_1

**Purpose:** increment + test + backedge (step = +1)

**Encoding:**

```
[opcode]
[i_slot:u8]
[limit_slot:u8]
[cmp_kind:u8]   // 0 = LT (<), 1 = LE (<=)
[offset:u16]    // backward distance to body start
```

**Runtime behavior:**

* `i` must remain fixnum; increment with overflow check.
* Store updated `i` back to slot.
* Compare updated `i` to `limit`:

  * If limit fixnum: int compare
  * If limit double: compare (double)i to limit
* If condition holds: `ip -= offset` (jump back to body). Else fall through.

---

## Edge-Case Semantics

* **NaN limit (double):** comparisons are false => loop terminates immediately.
* **±Inf limit:** loop behaves per IEEE comparisons; +Inf continues until fixnum overflow (unless deopt is added).
* **Fractional limit:** behaves naturally (e.g., `i <= 10.5` runs i=1..10).
* **Fixnum overflow:** Phase 1 policy is runtime error. Later phases may deopt to generic numeric loops.

---

## Compiler Responsibilities

### Pattern Detection (Phase 1)

Fuse only when loop matches:

* init: `for let i = <expr>; ...`
* condition: `i < limit` or `i <= limit`
* update: `i = i + 1`
* (Optional Phase 1 restriction) no `break/continue` unless explicitly supported

### Slot Liveness Invariant (Critical)

For fused loops, operands refer to slots that must remain live across iterations.

Define:

* `loopContinueSlot = gen.nextLocalSlot` immediately after compiling `node.init`

Require:

* `i_slot < loopContinueSlot`
* `limit_slot < loopContinueSlot`
* `localSlot(i_slot) != localSlot(limit_slot)` (no alias in bytecode slot space)

If any check fails: fall back to generic loop lowering.

### Continue/Break Integration

Current `continue` patching targets the loop tail. For fused loops, the tail is `FORLOOP`, so:

* Compile body
* `patchContinues(gen)` immediately before emitting `FORLOOP`
* `break` targets loop exit as usual (after scope unwind)

---

## Implementation Status (as of Phase 1)

* VM opcodes `OP_FORPREP_1`, `OP_FORLOOP_1`: implemented
* Disassembler: implemented
* Compiler pattern matcher: implemented
* Compiler emission: implemented conservatively (limit must be resolvable, slots must satisfy liveness checks)
* Known integration risk: resolver bindings for declaration sites may be absent; Phase 1 can derive `i_slot` via a validated allocation delta:

  * record `slotBeforeInit`
  * compile init
  * require `nextLocalSlot == slotBeforeInit + 1`
  * then `i_slot = slotBeforeInit`
  * otherwise fallback

---

## Phased Roadmap

### Phase 1 — MVP Fusion (step=+1)

**Goal:** speed up the most common numeric loops with minimal risk.

Scope:

* `i` fixnum
* step fixed at +1
* compare `<` or `<=`
* limit in a stable slot (local/param) and passes liveness invariant
* overflow => runtime error
* mixed-mode limit (double) supported

Deliverables:

* opcodes + VM impl + disassembler
* codegen: conservative emission + fallback
* tests: basic correctness + disassembly golden tests
* benchmark: `sum_loop`, `array_fill` sanity

### Phase 2 — Generalize Loop Semantics

**Goal:** increase applicability while keeping correctness.

Add:

1. **General limit expressions** via compiler rewrite:

   * rewrite `limitExpr` into an init-local `__limit`
   * ensures slot stability without “hidden temps”
2. **Signed constant step**:

   * support `i = i + K` and `i = i - K` where K is constant
   * direction derived from step sign (Lua-style)
3. **break/continue support** (if Phase 1 excluded them):

   * continue jumps to loop tail (before `FORLOOP`)
   * break jumps to exit label

Opcodes:

* introduce `OP_FORPREP` / `OP_FORLOOP` with `step_imm` (s8/s16) as an operand, or add `*_IMM8/IMM16` variants.

### Phase 3 — Fixnum-First Numeric Producers

**Goal:** increase fast-path hit rate across the language.

Update natives to return fixnum when integral and in range:

* `tonumber()`
* `Math.floor()`
* `len()`
* (optional) other integer-valued APIs

This improves:

* loop limit types (more fixnum limits)
* integer arithmetic specialization
* reduces mixed-mode overhead

### Phase 4 — Deopt / Bailout (Correctness-Preserving Fast Paths)

**Goal:** remove “runtime error on overflow” and handle rare type transitions.

Add a minimal bailout mechanism so fused loops can fall back if:

* induction var stops being fixnum
* overflow occurs
* step/limit assumptions break

Approaches:

* side-exit to a slow-path bytecode sequence
* re-enter interpreter at an alternate IP with generic lowering

### Phase 5 — Optional Specialization Variants (FI/FD)

**Goal:** remove residual per-iteration type dispatch if profiling warrants.

Add variants:

* fixnum limit compare (FI)
* double limit compare (FD)

Select via:

* compile-time type info (if available), or
* a cached per-loop flag set once (avoid self-modifying bytecode).

### Phase 6 — IR/JIT Readiness (Optional)

**Goal:** enable higher-level optimizations and/or JIT without replacing the baseline VM.

* Lower stack bytecode to a register/SSA IR for hot paths (ANF can help)
* Use fused loops as canonical induction-variable patterns in IR
* JIT (or AOT) compilation becomes a separate pipeline

---

## Testing Requirements

* Unit tests:

  * `<` and `<=` entry/exit correctness
  * mixed-mode limit (double) vs fixnum induction
  * NaN/Inf behavior
  * overflow behavior (per current phase policy)
* Golden disassembly tests for emitted fused loops
* Bytecode verifier updates (if verifier exists):

  * stack effects for new opcodes are 0
  * control-flow edges for forward/back jumps are handled

---

## Performance Expectations

* Before: ~8–12 loop-control opcode dispatches per iteration
* After Phase 1: ~1 dispatch per iteration (`FORLOOP_1`) + one-time `FORPREP_1`
* Expected speedup on loop-heavy code: ~1.5×–2.5× (benchmark-dependent)

---

## Notes / Open Questions

* Resolver binding availability for declaration sites:

  * Phase 1 can avoid dependency by validating init alloc delta (+1 slot)
  * Phase 2 rewrite approach further reduces resolver reliance
* Overflow policy:

  * Phase 1 error is acceptable for MVP
  * Phase 4 should introduce bailout to preserve semantics without fatal errors
