# Why Lx Uses Monomorphic Static Analysis

## Overview

Lx intentionally adopts a **monomorphic, best-effort static analysis** model.

This means:

> **Every variable, function, record, and array has exactly one stable type
> within its scope of analysis.**

Types are initially `Unknown`, refined by constraints, and then *frozen*.
If two constraints disagree, the analyzer reports an error.

This document explains **why this choice is deliberate**, how it aligns with
Lx’s goals, and what it enables (and avoids).

---

## What “Monomorphic” Means in Lx

Monomorphic in Lx does **not** mean:

* No inference
* No flexibility
* No higher-level abstractions

It means:

* One binding → one type
* One function → one meaning
* One record → one shape
* One array → one element type

There is **no type generalization**, no per-call specialization, and no
environment cloning.

This makes the analysis:

* Local
* Predictable
* Linear-time
* Easy to reason about

---

## Design Alignment with Lx

### 1. Lx Is a Restrained Dynamic Language

Lx code is typically written with strong *informal consistency*:

* Records are used as structured data
* Arrays contain uniform elements
* Functions are called consistently
* Variants are encoded via tagged records

Monomorphic analysis captures these expectations directly.

It catches:

* Accidental shape drift
* Incorrect field access
* Mismatched function calls
* Half-completed refactors

Without requiring:

* Type annotations
* Generic parameters
* Trait systems
* Type-level programming

This yields **high signal with low complexity**.

---

### 2. Stable Meaning Beats Expressive Meaning

In Lx, a function is expected to *mean one thing*:

```lx
fn add(a, b) { a + b }
add(1, 2)
add(3, 4)
```

This is valid.

But:

```lx
add(1, 2)
add("x", "y")
```

Is rejected by design.

The goal is not to express all valid programs, but to **reject inconsistent ones early**.

Monomorphic typing enforces the invariant:

> If two call sites disagree, the program is unclear.

This dramatically improves error quality and predictability.

---

## Why Not Polymorphism?

Supporting polymorphism would require:

* Type schemes
* Generalization points
* Instantiation per call
* Environment cloning
* More complex closure handling

This directly conflicts with Lx’s architecture:

* Shared `TypeVar`s across closures
* Flow-insensitive analysis
* Frozen record and array shapes
* Side-table-driven compiler phases

Polymorphism would **merge type checking with name resolution**, complicate
the pipeline, and significantly increase implementation and maintenance cost.

---

### Polymorphism Breaks Local Reasoning

With monomorphism, you can understand types by reading one scope.

With polymorphism, this is no longer true:

```lx
fn make() { [] }

let x = make()
push(x, 1)

let y = make()
push(y, "hi")
```

Now `make` must mean different things in different contexts.

This undermines:

* Frozen array rules
* Simple unification
* Predictable diagnostics

Lx intentionally avoids this complexity.

---

## Tooling and LSP Benefits

Monomorphic analysis provides **stable answers** for tooling:

* “What is the type of this variable?”
* “What fields exist on this record?”
* “What does this function return?”

These answers are:

* Deterministic
* Cacheable
* Easy to present
* Easy to trust

This makes Lx an excellent foundation for:

* LSPs
* Refactoring tools
* Navigation
* Inline diagnostics

Polymorphic answers are inherently conditional and harder to surface clearly.

---

## Alignment with Runtime Semantics

Lx’s runtime is:

* Monomorphic
* Slot-based
* Closure-capturing
* Non-specializing

There is no JIT or specialization phase that would benefit from static
polymorphism.

Adding polymorphism would increase frontend complexity **without improving
runtime performance**.

Monomorphic analysis matches the VM’s execution model directly.

---

## Tagged Records Cover Most Union Needs

Instead of polymorphic ADTs, Lx uses tagged records:

```lx
.{ kind: "Int", value: 1 }
.{ kind: "Str", value: "x" }
```

With:

* Frozen per-tag shapes
* Optional tag-aware refinement
* Clear runtime representation

This provides most of the practical benefits of algebraic data types without
introducing polymorphic type inference.

---

## Escape Hatches: `Unknown` and `Any`

The analysis remains best-effort:

* `Unknown` represents insufficient information
* `Any` represents intentional loss of precision

When errors occur, the analyzer:

* Reports the issue
* Continues analysis
* Widens types to avoid cascades

This keeps the system robust and usable even for partially dynamic code.

---

## Future Extensibility

Monomorphism is **not a dead end**.

Possible future extensions include:

* Explicit generics (opt-in, annotated)
* Signature files for imports
* More precise tagged unions
* Stricter modes for CI

Crucially, all of these can be added **without changing the default model**.

---

## Summary

Lx uses monomorphic static analysis because it:

* Matches how Lx code is written
* Aligns with the VM and compiler architecture
* Maximizes error clarity
* Enables strong tooling with minimal complexity
* Preserves future extensibility

Most programs do not need polymorphism.

They need **consistency**.

Monomorphic analysis enforces that consistency clearly, locally, and early.
