# Lx Compiler Architecture

This document describes the structure, responsibilities, and design principles of the Lx compiler.

The architecture is organized around **representations (IR levels)** and **pass roles**, rather than a traditional “frontend/backend” split. This framing scales better as the compiler grows and aligns with Lx’s long-term goals: deterministic compilation, explicit dependencies, tooling support, and evolvable optimizations.

---

## High-Level Compilation Model

Lx compilation is organized around **artifacts** rather than a single linear pipeline.
Work is scheduled by dependencies between artifacts (ASTs, analysis tables, bytecode),
and tooling can stop at any artifact boundary.

At a high level:

```
source
  → ast.final
    ├─ analysis.resolve
    ├─ analysis.dce.local
    ├─ analysis.fastcheck
    ├─ analysis.typecheck (optional)
    └─ analysis.dce.whole_program
          ↓
     analysis.dce.final
          ↓
     bytecode.function
          ↓
     bytecode.verified
          ↓
     object.bytes
```

Crucially:

* **Transforms** change a representation.
* **Analyses** compute *facts* about a representation without mutating it.
* Code generation is *mechanical* and delegates all policy to analysis + selection layers.

Note: `analysis.resolve.lower` is derived from the **post-lower, pre-ANF AST** and
is used by typecheck to align with lowered syntax. It is not computed from
`ast.final`.

---

## Core Design Principles

1. **Representation-oriented structure**
   Passes are categorized by *what they operate on* (AST vs bytecode), not by when they run.

2. **Role separation**
   Transforms and analyses are distinct concepts with different invariants and lifecycles.

3. **Single canonical AST**
   Codegen and backend analyses agree on a single canonical AST (`ast.final`).
   Tooling analyses may use earlier ASTs, but must pair them with analyses
   produced for that same representation.

4. **Explicit facts, not implicit state**
   Semantic knowledge (bindings, deadness, type facts) lives in side tables keyed by `nodeId`, not inside the AST.

5. **Policy / mechanism separation**
   `select.lx` decides *what* to emit; `codegen.lx` decides *how* to emit it.

6. **Tooling-first**
   Resolve + typecheck + error infrastructure form a complete, reusable foundation for LSP and static tools.

---

## Directory Structure (Current)

Passes are organized by **role**, with infra at the root:

```
src/passes/
  parse/
    parser.lx
  transform/
    lower.lx
    anf.lx
    anf-inline.lx
    lower-intrinsics.lx
  analysis/
    resolve.lx
    dce.lx
    fastcheck.lx
    typecheck.lx
    typecheck-helper.lx
  emit/
    codegen.lx
  verify/
    verify-bytecode.lx
  opt/
    peephole-bytecode.lx
  pipeline.lx
  node-utils.lx
  scanner.lx
```

Supporting libraries (not passes), such as `select.lx`, live outside `passes/` (e.g. `src/select.lx`).

---

## Canonical Artifacts

Lx compilation is organized around a small set of **canonical artifacts**.
An artifact is a *named, well-defined product* of compilation that may be:

* a transformed representation (AST, bytecode), or
* a set of **analysis facts** associated with a representation.

Artifacts are the unit of dependency, caching, and scheduling.

The key rule is:

> **All downstream consumers must agree on which artifact they are using.**
> Analyses and transforms are only valid with respect to a specific artifact version.

This section defines the canonical artifacts that structure the compiler.

---

### Artifact Classes

Artifacts fall into three broad classes:

1. **Representation artifacts**
   Concrete program representations (AST, bytecode).

2. **Analysis artifacts**
   Side tables keyed by `nodeId` or instruction index that describe a representation.

3. **Program-level artifacts**
   Aggregations that depend on the full module graph.

---

## AST Artifacts

### **`ast.final`** — Canonical AST

The **canonical AST** is the single AST instance that all downstream passes agree on.

It is produced by applying the enabled AST transform passes in order:

```
parse
  → lower
  → anf
  → anf-inline        (if enabled)
  → lower-intrinsics  (if enabled)
```

Properties:

* Exactly one `ast.final` exists per module per compilation.
* Codegen operates on `ast.final`.
* `nodeId`s in `ast.final` are the authoritative keys for all AST-level side tables.
* Passes that mutate the AST in place **must preserve `nodeId`** to keep analyses valid.

**Rationale:**
This eliminates ambiguity about “which AST codegen uses” and prevents stale analyses.

---

## AST Analysis Artifacts

AST analyses do **not** mutate the AST.
They compute facts about a specific AST representation and are keyed by its `nodeId`s.

All AST analysis artifacts are keyed by `nodeId`.

---

### **`analysis.resolve`**

Produced by `resolve.lx`.

Contains:

* `resolvedNames`
* `scopeInfo`
* `nodes` (partial node index)

Properties:

* Required by all binding-aware passes and analyses.
* Establishes core semantic invariants of the program.
* Does not depend on other analyses.
* Tied to the AST representation it was computed on.
* For tooling on the lowered AST, use `analysis.resolve.lower`.

---

### **`analysis.resolve.lower`**

Produced by the `resolve-lower` pass (same implementation as `resolve.lx`).

Contains the same tables as `analysis.resolve`, but keyed to the post-lower,
pre-ANF AST.

Properties:

* Used by typecheck to align with the lowered AST shape.
* Does not affect codegen (which uses `analysis.resolve` on `ast.final`).

---

### **`analysis.dce.local`**

Produced by `dce.lx`.

Contains:

* `deadNodes[nodeId]`
* `usedImportProperties`

Properties:

* Computed per module.
* Pure analysis; does not rewrite the AST.
* Used by codegen to skip emission of dead *pure* expressions.

---

### **`analysis.fastcheck`**

Produced by `fastcheck.lx`.

Contains:

* `facts[nodeId]` (fixnum / numeric / string / etc.)

Properties:

* Conservative, flow-insensitive.
* Used exclusively by `select.lx`.
* Replaceable by a stronger future analysis (`analysis.rep`) without changing codegen.

---

### **`analysis.typecheck`**

Produced by `typecheck.lx`.

Contains:

* `types[nodeId]`
* Type diagnostics

Properties:

* Tooling-oriented.
* No impact on runtime or codegen.
* May be computed lazily or omitted in non-tooling pipelines.
* Preferably computed on the lowered AST with `analysis.resolve.lower`.
* Falls back to `ast.final` + `analysis.resolve` when lower is disabled.

---

## Program-Level Analysis Artifacts

Some analyses require knowledge of **all modules in the program**.

These artifacts are computed after the module graph is known.

---

### **`analysis.dce.whole_program`**

Aggregates results from all `analysis.dce.local` artifacts.

Contains:

* `usedExportsPerModule`
* Derived unused-export information

Produces a *derived view*:

### **`analysis.dce.final`** (per module)

* Union of:

  * local dead nodes
  * whole-program unused export nodes

Properties:

* Must be computed **before codegen** if enabled.
* Determines which export values are elided from emitted bytecode.
* Does not mutate AST; affects emission only.

---

## Bytecode Artifacts

### **`bytecode.function`**

Produced by `codegen.lx`.

Input requirements:

* `ast.final`
* `analysis.resolve`
* `analysis.dce.final`
* `analysis.fastcheck` (or future `analysis.rep`)

Contains:

* Bytecode chunk
* Constant pool
* Instruction → `nodeId` mapping

Properties:

* Mechanical emission only.
* Deterministic given its inputs.

---

### **`bytecode.verified`**

Produced by `verify.lx`.

Contains:

* Validation result
* Stack-discipline diagnostics

Properties:

* Mandatory for compilation success.
* Consumed by object building and runtime loading.

---

## Object and Runtime Artifacts

### **`object.bytes`**

Produced by `objbuilder`.

Contains:

* Serialized bytecode
* Constant pool
* Metadata

This artifact is the boundary between the compiler and the runtime.

---

## Artifact Dependency Summary

At a high level:

```
ast.final
  ├─ analysis.resolve
  ├─ analysis.dce.local
  ├─ analysis.fastcheck
  ├─ analysis.typecheck (optional)
  └─ analysis.dce.whole_program
        ↓
   analysis.dce.final
        ↓
   bytecode.function
        ↓
   bytecode.verified
        ↓
   object.bytes
```

`analysis.resolve.lower` is produced from the lowered AST (post-lower, pre-ANF)
and is consumed by typecheck; it is intentionally kept separate from the
`ast.final` analysis set used by codegen.

---

## Why Canonical Artifacts Matter

This model addresses several structural issues directly:

* **No stale analyses**
  Analyses are explicitly tied to `ast.final`.

* **No “rerun before codegen” hacks**
  Codegen declares its dependencies; the scheduler ensures they are built.

* **Whole-program phases are well-defined**
  Program-level artifacts run *before* codegen when they must affect output.

* **Tooling and compilation share the same foundation**
  Tooling pipelines simply stop earlier in the artifact graph.

* **Scheduling becomes declarative**
  The compiler can answer:
  *“What must be built to produce artifact X?”*

---

## Future Direction

A dependency-driven scheduler can be layered on top of this artifact model:

* Each artifact declares:

  * required artifacts
  * scope (module vs program)
* The driver builds only what is needed for a requested target
* Artifacts are cached and invalidated by representation changes

This turns compilation into a small, deterministic build graph rather than a fixed linear pipeline.

---

## Adding a Pass

When you add a new pass, you must update the **artifact spec** and **pass
definitions** so the planner and scheduler stay in sync.

Checklist:

1. **Define the pass implementation** (AST transform or analysis).
2. **Register the pass in `PASS_DEFS`** with:
   * `name`
   * `requires` / `provides`
   * `run` function
3. **Declare or extend the artifact it produces** in `ARTIFACT_SPEC`:
   * add a new artifact if needed
   * list its `requires`
   * list the pass in `produces`
4. **Verify the plan** with:
   * `lx compile --plan --artifact <target>`

Notes:

* `PASS_DEFS` lives in `lx/src/driver/passes.lx`.
* `ARTIFACT_SPEC` is the single source of truth for artifact dependencies.
* The planner validates these at startup; invalid references fail fast.

---

## AST-Level Passes

### **parse.lx** — AST transform (syntax → AST)

**Role:** AST transform
**Input:** source text
**Output:** AST with node IDs and spans

* Recognizes Lx grammar
* Assigns monotonically increasing `nodeId`s
* Attaches source positions (filename, line, column)
* Reports **syntax errors only**
* Preserves surface syntax
* Enforces “everything is an expression” structurally (root is an implicit `Block`)
* Does **not** enforce contextual legality (e.g. return placement)

Lexing/scanning is an implementation detail of parsing and is not itself a pipeline pass.

---

### **lower.lx** — AST transform (desugaring)

**Role:** AST transform
**Input:** AST
**Output:** canonical AST (new node IDs)

* Pure syntactic rewrites:

  * Arrow: `x->f(a)` → `f(x, a)`
  * Normalize missing `else` branches
* Produces a new AST with fresh node IDs
* Records **origin chains** for error reporting
* No semantic reasoning

---

### **anf.lx** — AST transform (evaluation order normalization)

**Role:** AST transform
**Input:** AST
**Output:** ANF AST

* Lowers expressions into A-normal form
* Introduces temporary `let` bindings to preserve left-to-right evaluation
* Makes block values explicit
* No name resolution or semantic checks
* Enabled by default (can be disabled for experimentation)

---

### **anf-inline.lx** — AST transform (post-resolve optimization)

**Role:** AST transform
**Input:** resolved ANF AST
**Output:** optimized AST (mutated in place)

* Runs **after resolve**
* Eliminates single-use ANF temporaries
* Uses binding identity (`declaredAt`) rather than names
* Conservative: only inlines pure expressions
* Binder-aware and scope-safe
* Preserves `nodeId`s

This pass exists specifically because it can rely on resolver metadata.

---

### **lower-intrinsics.lx** — AST transform (post-resolve optimization)

**Role:** AST transform
**Input:** resolved AST
**Output:** optimized AST (mutated in place)

* Rewrites generic patterns into `IntrinsicCall` nodes
* Examples:

  * `x % 8` → `mod_const(x, 8)`
  * `x == 3` → `eq_const(x, 3)`
* Preserves `nodeId`s so all side tables remain valid
* Keeps ANF focused purely on evaluation order

---

## AST-Level Analyses

### **resolve.lx** — AST analysis (binding + semantic validation)

**Role:** AST analysis
**Input:** AST
**Output:** side tables

* Name resolution (locals, upvalues, globals)
* Function hoisting
* Contextual legality checks:

  * `return` placement
  * `break` / `continue` placement
  * assignment targets
* Semantic errors:

  * undefined variables
  * duplicate declarations
  * read-before-init
* Produces side tables:

  * `resolvedNames`
  * `scopeInfo`
  * `nodes`
* Does **not** mutate the AST
* Scheduled as two passes:
  * `resolve` on `ast.final` for codegen and post-ANF analyses
  * `resolve-lower` on the lowered AST for typecheck

---

### **dce.lx** — AST analysis (dead code elimination facts)

**Role:** AST analysis
**Input:** resolved AST
**Output:** side tables

* Identifies dead expressions and unused bindings
* Produces:

  * `deadNodes[nodeId]`
  * `usedImportProperties`
* Pure analysis: does not rewrite the AST
* Used by codegen to skip emission of dead pure expressions

Whole-program DCE extends this analysis but follows the same model: compute facts, do not emit code.

---

### **fastcheck.lx** — AST analysis (representation/type facts)

**Role:** AST analysis
**Input:** AST + resolve tables
**Output:** side tables

* Computes lightweight representation facts:

  * fixnum vs numeric vs string, etc.
* Flow-insensitive, conservative, fast
* Produces `facts[nodeId]`
* Consumed exclusively by `select.lx`

This pass is intentionally replaceable by a stronger future analysis (`rep`).

---

### **typecheck.lx** — AST analysis (tooling-oriented)

**Role:** AST analysis
**Input:** lowered AST + `analysis.resolve.lower` (preferred), or `ast.final` + `analysis.resolve` (fallback)
**Output:** side tables

* Monomorphic, best-effort inference
* Infers stable types for:

  * variables
  * functions
  * records
  * arrays
* Detects incompatible usage
* No impact on runtime or codegen
* Intended for tooling (LSP, diagnostics, refactors)

---

## Opcode Selection and Bytecode

### **select.lx** — Opcode selection policy

**Role:** policy module (not a pass)

* Centralizes all *decision-making* for bytecode emission
* Pure, deterministic functions
* Reads analysis facts (`fastcheck` today, `rep` later)
* Returns opcode choices or emission plans
* No byte emission
* No mutation of codegen state

Responsibilities:

* Choose specialized opcodes (`ADD_INT`, `ADD_STR`, etc.)
* Recognize superinstruction patterns
* Abstract over analysis sources

This layer decouples analysis evolution from codegen mechanics.

---

### **codegen.lx** — Bytecode transform (mechanical emission)

**Role:** bytecode transform
**Input:** AST + analysis tables
**Output:** bytecode function / chunk

* Walks the AST in source order
* Evaluates operands
* Delegates all policy decisions to `select.lx`
* Emits bytecode and manages:

  * stack discipline
  * local slots
  * constant pool
  * control-flow patching
* Records `nodeId` per instruction for diagnostics
* No semantic reasoning

Architecture:

```
compile operands
  → select (policy)
  → emit bytecode (mechanical)
```

---

### **verify.lx** — Bytecode analysis (stack discipline)

**Role:** bytecode analysis
**Input:** bytecode
**Output:** validation result

* Dataflow analysis of stack effects
* Ensures:

  * no underflow
  * no overflow
  * balanced stack at joins
* Mandatory pass
* Errors reported via `nodeId`

---

## Error Handling

Centralized in `errors.lx`.

Key features:

* Unified error structure across all passes
* Origin-chain tracking (ANF → lower → parse)
* Lazy construction of complete `nodeId → node` index
* Precise source positions for all diagnostics

---

## Driver and Pipelines

The driver:

* Orchestrates pass execution
* Owns the import cache
* Manages module lifecycle
* Supports curated **profiles** (compiler, typecheck, query)

Profiles select *targets*, not arbitrary pass subsets. This avoids invalid combinations and simplifies reasoning.

Future direction: a dependency-driven scheduler that builds requested artifacts (AST, analyses, bytecode) from declared dependencies.

---

## Design Goals Recap

1. Deterministic compilation
2. Precise diagnostics
3. Tooling-first architecture
4. Policy/mechanism separation
5. Evolvable analysis and backend
6. Maintainability through explicit structure

---

## Non-Goals

* Full polymorphic type inference
* Aggressive optimization
* Perfect soundness
* SSA or moving GC assumptions
