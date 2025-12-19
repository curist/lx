# Lx Typechecker Roadmap

## Overview

This document defines the **long-term roadmap** for the Lx typechecker.

The typechecker is intentionally **not** a full Hindley–Milner system.
Instead, it evolves in **phases**, each with a sharply defined contract.

Each phase:

* preserves the public `typecheck(ast, resolveResult, opts)` signature
* does not mutate the AST
* builds on the previous phase without invalidating its guarantees

The roadmap prioritizes **predictability, tooling value, and maintainability** over theoretical completeness.

---

## Design Principles (Non-Negotiable)

1. **Monotonic Complexity**

   * Later phases may add power, but must not rewrite earlier semantics.

2. **Containment over Soundness**

   * Errors degrade locally to `Any`; analysis must continue.

3. **Explicit Phase Boundaries**

   * If a feature belongs to Phase N, it must *not* partially exist in Phase N-1.

4. **Inference Is Usage-Driven**

   * Definitions alone never force concrete types.

5. **Side Tables Only**

   * All information is stored outside the AST (keyed by nodeId).

---

## Phase Summary

| Phase | Name                        | Status        | Core Value                 |
| ----: | --------------------------- | ------------- | -------------------------- |
|     1 | Skeleton                    | ✅ implemented | Structural correctness     |
|     2 | Constraint Mono Inference   | ⏳ next        | Useful intra-module types  |
|     3 | Nilability & Option         | 🔜 planned    | Honest control-flow typing |
|     4 | Collections (Record vs Map) | 🔜 planned    | Data structure clarity     |
|     5 | Function Semantics          | 🔜 planned    | Realistic call behavior    |
|     6 | Imports / Modules           | 🔜 planned    | Cross-file usefulness      |
|     7 | Diagnostics & Errors        | 🔜 planned    | Human-usable feedback      |
|     8 | Tagged Records (Optional)   | ⏭ deferred    | Discriminated unions       |
|     9 | Type Annotations            | 🔜 planned    | Escape hatches             |

---

## Phase 1 — Skeleton Typechecker (Baseline)

### Goal

Establish a **stable traversal and data model**.

### Capabilities

* Fresh `TypeVar` for each declaration
* Minimal expression traversal
* Eager unification for literals and operators
* `types[nodeId]` populated for key nodes
* No crashes, infinite loops, or missing entries

### Explicit Non-Goals

* No constraint accumulation
* No backward propagation
* No narrowing
* No Option / Map / Tagged
* No polymorphism

### Output Quality

Types are often `Unknown`.
This is **expected and acceptable**.

---

## Phase 2 — Constraint-Based Monomorphic Inference

### Goal

Collapse `Unknown` into meaningful types **within a module**, without HM.

### Core Additions

* Constraint store (`Eq`, `HasField`)
* Worklist solver with containment
* Predeclare top-level bindings (forward refs)
* Function refinement second pass

### New Guarantees

* Field access on parameters can infer record shape
* Call sites propagate argument structure into functions
* Function return types stabilize after usage

### Still Explicitly Excluded

* Polymorphism / schemes
* Nilability
* Flow-sensitive narrowing
* Imports

### Phase-Exit Criterion

The following must infer correctly:

```lx
fn getName(x) { x.name }
let a = .{ name: "A" }
getName(a)  // → String
```

---

## Phase 3 — Nilability & Option Discipline

### Problem

Lx semantics frequently produce `nil`. Ignoring this yields lies.

### Goal

Introduce **honest but minimal nil tracking**.

### Additions

* `Option(T)` type
* `joinTypes` at control-flow merges
* `if` without `else` → `Option(T)`
* Indexing and map lookup return `Option(T)`

### Limited Narrowing

Only for:

* `x == nil`
* `x != nil`

No general boolean logic inference.

### Why Separate

Nilability touches:

* `if`
* `return`
* indexing
* dot access
* env merge

Mixing this into Phase 2 risks destabilization.

---

## Phase 4 — Collections: Record vs Map

### Problem

`. {}` is overloaded in Lx.

### Goal

Make **collection intent explicit** in types.

### Additions

* `Map(K, V)` (likely `K ∈ {String, Number}`)
* Literal disambiguation rules:

  * `.{}`

    * default Map, or
    * first-use determines shape (policy decision)
* Strict separation:

  * `obj.k` → Record only
  * `obj["k"]` → Map only

### Record Policy

* Record literals are **closed**
* TypeVar-origin records may grow via constraints

---

## Phase 5 — Function & Call Semantics

### Problem

Lx allows Lua-style calls, but inference needs discipline.

### Decisions to Encode

* Exact arity (Phase 2+ default), or
* Extra args ignored, missing args → `Nil` / `Option`

### Additions

* Builtin signature table
* Call compatibility rules
* Clear error reporting for mismatches

---

## Phase 6 — Imports & Cross-Module Typing

### Goal

Make types usable across files.

### Additions

* Module type summaries
* Import signature loading
* Binding replay / substitution
* Caching to avoid re-analysis storms

### Policy Decisions

* Unknown import → error or `Any`?
* Partial import typing allowed?

---

## Phase 7 — Diagnostics & Error Quality

### Goal

Turn inference into **human-usable tooling**.

### Additions

* Constraint provenance tracking
* Error localization using node spans
* Structured messages:

  * “expected X, got Y”
  * “field missing”
  * “not callable”

### Invariant

Errors must **never cascade uncontrollably**.

---

## Phase 8 — Tagged Records (Optional / Deferred)

### Goal

Support discriminated unions without syntax changes.

### Additions

* Detect tag fields (`kind`, `type`, etc.)
* Build `Tagged(tagField, cases)`
* Safe field access under known tags
* Optional narrowing via tag equality

### Status

Optional. Valuable, but not required for MVP.

---

## Phase 9 — Type Annotations (Escape Hatch)

### Goal

Allow users to **override inference limits**.

### Additions

* Variable annotations
* Function parameter and return annotations
* Type aliases (non-distinct)

### Semantics

Annotations emit constraints; they do not bypass checking.

---

## Dependency Graph (High Level)

```
Phase 1
  ↓
Phase 2
  ↓
Phase 3 ─┐
  ↓      │
Phase 4  │
  ↓      │
Phase 5  │
  ↓      │
Phase 6  │
  ↓      │
Phase 7  │
         └── Phase 8 (optional)
                ↓
             Phase 9
```

---

## Completion Definition (MVP Line)

A **practically complete** Lx typechecker includes:

* Phase 1–7
* Phase 9 (annotations)

Phase 8 (Tagged) is optional.

---

## Closing Principle

> Each phase exists to **reduce surprise**, not to maximize inference power.

If a feature cannot be explained in one paragraph, it probably belongs in a later phase.

