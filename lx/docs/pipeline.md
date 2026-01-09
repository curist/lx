# LX Compiler Pipeline

## From Pass Lists to Artifact-Driven Scheduling

This document explains why the current LX compilation pipeline is difficult to reason about, and how it evolves into an **artifact-driven, dependency-scheduled pipeline**.

The goal is not to redesign individual passes, but to **clarify ownership, ordering, and validity** of compiler work so that:

* analyses are never stale,
* whole-program phases run at the correct time,
* backend stages are no longer “special wiring,” and
* compilation targets (compiler vs tooling) are explicit and reliable.

---

## Background

LX currently has three overlapping orchestration layers:

1. **Pass execution**
   `pipeline.lx` runs an ordered list of passes with:

   * `requires` / `provides` metadata
   * option-based enablement

2. **Module driver**
   `driver.lx` coordinates:

   * per-module compilation
   * import caching
   * some whole-program phases (typecheck, whole-program DCE)

3. **Backend wiring**
   `commands/common.lx` performs work *outside* the pass system:

   * selects an AST variant manually
   * runs `fastcheck`
   * runs `codegen`
   * verifies bytecode

This split means **no single component owns the full compilation plan**.

---

## Core Problem Statement

### The pipeline executes *passes*, but the compiler produces *artifacts*

The compiler’s real outputs are things like:

* a resolved AST
* dead-node facts
* representation/type facts
* verified bytecode

But today, LX schedules **passes**, not **artifacts**.
As a result:

* pass results are stored opportunistically,
* consumers pick “the latest available AST,”
* analyses may not match the representation they’re used with,
* whole-program phases run “after the fact.”

---

## Symptoms in the Current Design

### 1. There is no single authoritative schedule

Two schedulers exist:

* `pipeline.runPasses()` decides AST transforms and analyses
* `compileWithDriver()` decides backend work

Neither knows the full dependency graph.

Consequences:

* duplicated logic (“which AST do I use?”)
* implicit assumptions about pass order
* backend analyses must be manually injected

---

### 2. Analyses can be stale relative to codegen

Some AST transforms mutate in place (`anf-inline`, `lower-intrinsics`).

The AST used by codegen is chosen via a fallback chain:

```lx
let ast =
  passes["anf-inline"]?.ast or
  passes.anf?.ast or
  passes.lower?.ast
```

But analyses such as:

* `analysis.dce.local`
* `analysis.fastcheck`

are often computed **earlier**, over a different AST shape.

**Observed behavior:**

* analyses “need to be rerun before codegen”
* correctness depends on undocumented ordering conventions

---

### 3. Whole-program phases run too late to affect output

`runWholeProgramDce()` executes after all modules compile.

However:

* `bytecode.function` has already been produced
* modifying `deadNodes` at this point does not reliably affect emission

This forces one of two bad outcomes:

* whole-program DCE silently does nothing
* or codegen must be rerun ad-hoc

---

### 4. `requires` / `provides` exists but does not drive execution

`pipeline.lx` can *validate* a chosen pass list, but it cannot:

* build a plan for a requested output
* compute analyses on demand
* invalidate cached results when upstream artifacts change

Dependency metadata is descriptive, not operational.

---

### 5. There are no explicit build targets

A robust compiler can answer questions like:

* “Give me the resolved AST”
* “Give me type information for tooling”
* “Give me verified bytecode”

LX instead has a single entrypoint that produces *many* intermediate results, and callers must decide what to consume.

---

## Design Goals (Restated)

1. **Single source of truth for scheduling**
   One system decides *what* runs and *when*.

2. **Artifact-based compilation**
   Passes exist to *produce artifacts*; artifacts are the unit of dependency.

3. **Dependency-driven planning**
   The compiler builds only what is required for a requested target.

4. **Representation consistency**
   Analyses are always computed over the representation they are consumed with.

5. **Whole-program phases are first-class**
   They run before downstream consumers and invalidate dependents.

6. **Explicit caching and invalidation**
   No accidental reuse, no implicit reruns.

---

## The Artifact-Driven Pipeline Model

### Key shift in mindset

Instead of:

> “Run this list of passes”

the compiler asks:

> **“Build artifact `T` for module `M`.”**

Examples:

* `buildModule(path, "analysis.resolve")`
* `buildModule(path, "bytecode.verified")`
* `buildProgram(entry, "analysis.dce.whole_program")`

---

## Canonical Build Targets

The pipeline is organized around the **Canonical Artifacts** defined in `architecture.md`.

### Per-module artifacts

* `ast.final`
* `analysis.resolve`
* `analysis.dce.local`
* `analysis.fastcheck`
* `analysis.typecheck` (optional)
* `bytecode.function`
* `bytecode.verified`

### Program-level artifacts

* `program.module_graph`
* `analysis.dce.whole_program`
* derived per-module: `analysis.dce.final`

---

## Scheduling Rules

Each artifact declares:

* **requires**: other artifacts it depends on
* **scope**: module-local or program-level
* **representation** it is valid for (e.g. `ast.final`)
* **invalidates** (implicitly, via stamps)

### Example dependencies

* `analysis.resolve`

  * requires: `ast.final`

* `analysis.fastcheck`

  * requires: `ast.final`, `analysis.resolve`

* `analysis.dce.local`

  * requires: `ast.final`, `analysis.resolve`

* `analysis.dce.whole_program`

  * requires:

    * all modules have `analysis.dce.local`
    * complete module graph

* `bytecode.function`

  * requires:

    * `ast.final`
    * `analysis.resolve`
    * `analysis.fastcheck`
    * `analysis.dce.final`

* `bytecode.verified`

  * requires: `bytecode.function`

---

## Validity and Invalidation

Artifacts are cached only if their **input stamps** match.

Minimal stamping model:

* **`astStamp`**
  Derived from:

  * enabled AST transforms
  * transform configuration
  * node ID space

* **`programStamp`**
  Derived from:

  * module graph
  * enabled whole-program phases

An artifact is reusable only if:

* its `astStamp` matches the current `ast.final`
* its `programStamp` matches the current program state (if applicable)

---

## Execution Strategy

To build `bytecode.verified` for a module:

1. Build `ast.final` using the configured AST transforms.
2. Build required AST analyses:

   * `analysis.resolve`
   * `analysis.fastcheck`
   * `analysis.dce.local`
3. If enabled, build `analysis.dce.whole_program`.
4. Derive `analysis.dce.final` for the module.
5. Build `bytecode.function`.
6. Build `bytecode.verified`.

No analysis runs twice unless its inputs changed.

---

## Whole-Program DCE (Reframed)

### Old behavior

* Local DCE runs during module compilation.
* Whole-program DCE mutates results *after* codegen.
* Effects are unreliable.

### New behavior

Whole-program DCE is an artifact:

1. Each module produces `analysis.dce.local`.
2. `analysis.dce.whole_program` aggregates usage.
3. Each module derives `analysis.dce.final`.
4. `bytecode.function` **depends on** `analysis.dce.final`.

Result:
Unused exports are eliminated *during* emission, not retroactively.

---

## AST Consistency Rule

> **Any analysis consumed by codegen must be computed over `ast.final`.**

This implies:

* AST-mutating passes run *before* DCE / fastcheck
* analyses that must run earlier either:

  * do not affect codegen, or
  * require explicit remapping

In practice, LX adopts the simple, robust rule:

* `resolve` may run earlier
* `fastcheck` and `dce` run after the last AST mutation

---

## How This Resolves the Original Pain Points

| Pain                              | Resolution                                    |
| --------------------------------- | --------------------------------------------- |
| Rerunning analyses before codegen | Codegen depends on artifacts, not pass timing |
| Whole-program DCE placement       | Modeled as a prerequisite artifact            |
| Manual AST selection              | Single canonical `ast.final`                  |
| Backend special-casing            | Backend stages are normal artifact producers  |
| Fragile ordering                  | Dependencies, not lists, define order         |

---

## Migration Strategy (Incremental)

1. Treat `fastcheck` and `codegen` as artifact producers.
2. Make `ast.final` explicit and authoritative.
3. Move whole-program DCE *before* codegen via artifacts.
4. Introduce stamps and caching.
5. Eventually replace “run passes” with “build artifact”.

Each step is independently useful.

---

## Summary

The LX compiler already has most of the *mechanics* required for a robust pipeline.
What is missing is a shift in perspective:

> **Passes exist to produce artifacts.
> Artifacts define scheduling, validity, and correctness.**

By centering the pipeline around canonical artifacts and dependency-driven planning, LX gains:

* correctness without reruns
* predictable whole-program behavior
* cleaner backend integration
* a natural foundation for tooling and incremental compilation

This model scales without introducing new conceptual debt—and removes several classes of bugs outright.
