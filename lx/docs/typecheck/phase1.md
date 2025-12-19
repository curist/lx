# Lx Typechecker Roadmap & Phase-1 Specification

## 1. Context & Motivation

The Lx typechecker is intentionally **incremental**.
Rather than implementing a full Hindley–Milner or flow-sensitive system upfront, we build a **small, predictable core** and layer features on top.

This document records:

* What **Phase 1** does today (and why)
* What was explicitly removed
* How future phases will extend behavior *without destabilizing the core*

This is a **design constraint document**, not just an implementation note.

---

## 2. Phase 1 — Minimal Monomorphic Core (CURRENT)

### 2.1 Goals

Phase 1 provides a **stable foundation**:

* Eager, monomorphic type inference
* Deterministic behavior
* No flow sensitivity
* No delayed solving
* No polymorphism

The key question Phase 1 answers:

> “Given the expressions I see and the constraints they force, can I prove a concrete type?”

If the answer is no, the result is **Any / Unknown**, by design.

---

### 2.2 Supported Types (Phase 1)

Phase 1 supports a **small closed set** of types:

```
Type =
  Any
  Nil
  Number
  Bool
  String
  TypeVar(id)
  Function([Type], Type)
  Array(Type)
  Record({ field: Type })
```

Notably **absent**:

* Option / nullable types
* Tagged unions
* Map / dictionary
* Polymorphic schemes
* Type aliases
* Subtyping

---

### 2.3 Core Mechanism

Phase 1 uses **eager unification with substitution**, not a constraint graph.

#### Data Structures

* `typeVarBindings : TypeVarId → Type`
* `types            : NodeId → Type`
* `env              : lexical scope → DeclNodeId → TypeVar`

There is **no constraint store** and **no solver pass**.

#### Unification

* Happens immediately
* Mutates `typeVarBindings`
* Uses an occurs check to prevent infinite types
* Structural for `Function`, `Array`, and `Record`
* `Any` unifies with everything

This is effectively a **union-find–like system** implemented with a dictionary and recursion.

---

### 2.4 Environment & Scope Rules

* Lexical scoping only
* No environment cloning
* No branch-local refinement
* No re-merging of environments

Bindings are created once per declaration and reused.

---

### 2.5 Control Flow Semantics (Phase 1)

Control flow is intentionally **dumb**.

#### `if` expression

* Condition is typechecked but ignored for narrowing
* Both branches are checked in the *same* environment
* If both branches exist:

  * Their result types must unify
  * Otherwise → error
* If there is no `else`:

  * Result type is `Nil`

There is **no Option wrapping** and no nil-aware behavior.

---

### 2.6 Function Semantics

* Functions are **monomorphic**
* A function’s type is inferred only from:

  * its parameters
  * constraints inside its body
  * **actual call sites**

Unused or weakly constrained functions often infer to:

```
(TypeVar...) -> TypeVar
```

Which appears externally as `Unknown`.

This is **expected**.

---

### 2.7 What Phase 1 Explicitly Does *Not* Do

Phase 1 intentionally removes all of the following:

* Option / nil-join logic
* Branch narrowing
* Guard extraction
* Tagged unions
* Map vs Record discrimination
* Two-pass inference
* Deferred constraints
* Polymorphism (`Scheme`, `generalize`, `instantiate`)
* Import-time type replay
* Builtin overloading tricks

If any of these appear to be needed, that is a signal for a **later phase**, not a Phase-1 patch.

---

### 2.8 Expected Phase-1 Behavior (Sanity Examples)

| Code                         | Phase-1 Result |
| ---------------------------- | -------------- |
| `1 + 2`                      | `Number`       |
| `fn(x){x}`                   | `Unknown`      |
| `let x = 1; x`               | `Number`       |
| `fn add(a,b){a+b}; add(1,2)` | `Number`       |
| Unused function              | `Unknown`      |
| `if cond { 1 } else { "x" }` | error          |
| `if cond { 1 }`              | `Nil`          |

“Unknown” is **not a bug**; it is a signal that no constraint forced a concrete type.

---

## 3. Why Phase 1 Looks “Underpowered”

This is intentional.

Phase 1 optimizes for:

* Predictability
* Debuggability
* Small surface area for bugs

It deliberately avoids:

* “Best effort guessing”
* Implicit unions
* Flow-based reasoning

Later phases will make results look “smarter” **without touching Phase-1 invariants**.

---

## 4. Phase 2 — Flow Sensitivity & Option Types (NEXT)

### 4.1 New Concepts Introduced

Phase 2 introduces **local flow sensitivity**, but still no polymorphism.

Additions:

* `Option(T)`
* Environment cloning
* Environment merging
* Nil-checks as guards
* Branch joins

### 4.2 Key Rule

> Phase 2 must *wrap* Phase 1, not modify it.

Phase 1 remains responsible for:

* unification
* structural consistency

Phase 2 adds:

* branch-local environments
* controlled re-merging using `joinTypes`

---

### 4.3 New Semantics

* `if` without `else` → `Option(T)`
* `x == nil` / `x != nil` refine `x` inside branches
* After branches, types are **joined**, not overwritten

This is where “real world” code starts to feel usable.

---

## 5. Phase 3 — Shape Reasoning & Sum Types

Phase 3 introduces **data modeling power**:

* Tagged unions
* Discriminated records
* Map vs Record separation
* Dot / index narrowing
* Tag-based field access

This phase assumes:

* Phase 1 unification is correct
* Phase 2 env merging is stable

---

## 6. Phase 4 — Polymorphism (Optional)

Only after Phases 1–3 are solid:

* `Scheme`
* `generalize`
* `instantiate`
* Value restriction rules

This phase is **optional** depending on language goals.

---

## 7. Development Discipline Going Forward

### Hard Rules

1. **Never add Phase-2 logic to Phase-1 code**
2. Phase-1 files must remain small and boring
3. Each phase must have:

   * a written contract
   * golden tests
4. “Looks dumb but correct” > “looks smart but unstable”

---

## 8. Current Status Summary

* ✅ Phase 1 implemented
* ✅ Large legacy code removed
* ✅ Deterministic behavior restored
* ⏳ Phase 2 design pending
* ❌ No Option / narrowing yet (by design)

The system is **not unfinished** — it is **correctly staged**.

---

## 9. Next Concrete Steps

Recommended order:

1. Add **Phase-1 golden tests**
2. Freeze Phase-1 behavior
3. Write **Phase-2 env merge spec**
4. Implement Phase-2 as a thin layer
5. Re-introduce expressiveness gradually

