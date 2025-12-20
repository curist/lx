# Lx Typecheck v2 Specification

**Bidirectional Inference + Reactive Constraint Solving + Incremental/Dependency-Driven Scheduling**

## 0. Scope and Goals

### Primary goals

* Provide **whole-program analysis** suitable for batch compilation (complete diagnostics; stable output types).
* Provide **LSP-facing typing and diagnostics** with low latency and incremental updates (partial results allowed; diagnostics should not flicker).

### Non-goals (v2)

* Polymorphism / schemes / generalization
* Full soundness
* Rich flow-sensitive narrowing beyond existing Option heuristics
* Perfect module system typing (imports may remain coarse in v2)

---

## 1. Concepts and Terminology

### 1.1 Decl identity

A **Decl** is any binding target that can be referenced:

* top-level `let`
* top-level `fn name(...) { ... }` (the function decl itself)
* params and locals (optional for cross-run caching; required for within-run correctness)

Each Decl has an identifier `DeclId`. In current implementation this is `declaredAt` (node id), but v2 treats it abstractly.

**Requirement:** for incremental across edits, DeclId should be stable across edits. If not available initially, v2 must still support within-run incremental solving and unit scheduling; cross-run caching is optional.

### 1.2 Unit

A **Unit** is the smallest independently re-checkable component.
In v2, Units are:

* each top-level function declaration body
* each top-level let initializer expression (if present)
* optionally, nested functions as separate units (recommended, but can be phase 2)

Unit identifier: `UnitId` (usually equals DeclId for top-level declarations).

### 1.3 Constraints

Constraints encode delayed typing relationships:

* Eq, HasField(read/write), Call/OverloadCall, Index/IndexSet, KeyLike, etc.

Constraints are identified by stable `ConstraintId` within a run.

### 1.4 Solver model

The solver is **reactive**:

* constraints are attempted when relevant information changes
* a constraint may be **Solved**, **Deferred(waiting on typevars)**, or **Errored/Contained**

---

## 2. Public API

```lx
fn typecheck(ast, resolveResult, opts) -> {
  success: Bool,
  types: Map<NodeId, Type>,
  typeVarBindings: Map<TypeVarId, Type>,
  diagnostics: [Diagnostic],
  meta: {
    mode: "batch" | "lsp",
    didReachBudget: Bool,
    pendingConstraints: Number,
    checkedUnits: [UnitId],
  }
}
```

### 2.1 Options

```lx
opts = .{
  mode: "batch" | "lsp",

  // LSP-only: step or time budget for solver work
  solverBudgetSteps: Number?,

  // LSP-only: list of dirty units to re-check (if omitted, re-check everything in file)
  dirtyUnits: [UnitId]?,

  // LSP-only: whether to emit provisional diagnostics
  diagnosticsPolicy: "definitive-only" | "provisional",
}
```

---

## 3. Modes

### 3.1 Batch mode

* Typecheck all units.
* Run solver to fixpoint (or a very high cap).
* Run finalization passes:

  * commit Indexable to Array/Map as per evidence
  * finalize remaining constraints (report and contain as needed)
* Output: complete types + complete diagnostics.

### 3.2 LSP mode

* Typecheck only dirty units and dependency-impacted units (if available).
* Run solver with a finite budget (steps or time).
* **Do not** emit “unresolved call/index” errors solely due to unknowns.
* **Do not** aggressively contain unknowns to `Any`.
* Output: partial types, monotone refinement; diagnostics should be stable.

---

## 4. Bidirectional Typing

### 4.1 Core API

Two mutually-recursive functions:

* `synthExpr(node) -> Type`
* `checkExpr(node, expectedType, blameNodeId, msg) -> Type`

`checkExpr`:

* calls `synthExpr` to obtain `actual`
* enforces `actual ~ expected` via unification or an Eq constraint
* returns `expected` (preferred) or `actual` (allowed)

### 4.2 Where check-mode is used (required v2)

**Calls**

* If callee synthesizes to `Function(params, ret)` (or derefs to it), then:

  * for each required parameter `pi`, call `checkExpr(arg[i], pi, argNodeId, "Argument mismatch")`
  * return `ret`
* Otherwise, fall back to emitting a `Call` constraint.

**Return**

* if `currentReturn` exists:

  * `checkExpr(value, currentReturn, node.id, "Return mismatch")`

**Unary and arithmetic**

* enforce operand expectations via `checkExpr`:

  * `-x` expects Number
  * `!x` expects Bool (or Any if you choose; but keep consistent)
  * numeric ops expect Number
  * string concat uses expected String when either side is known String

### 4.3 Optional check-mode sites (recommended)

* `if` with expected type:

  * check both branches against expected (and apply Option rule if no else)
* array/record literals:

  * if expected is `Array[T]`, check elements against `T`
  * if expected is `Record{...}`, check field types

---

## 5. Reactive Constraint Solver

### 5.1 Data structures

#### TypeVar bindings

```lx
checker.typeVarBindings: Map<TypeVarId, Type>
checker.typeVarVersion: Map<TypeVarId, Number> // increments on real changes
```

#### Pending work queue

```lx
checker.pending: Queue<ConstraintId>
```

#### Watchers index

Maps “typevar changed” -> “constraints to retry”

```lx
checker.watchers: Map<TypeVarId, [ConstraintId]>
```

#### Constraint store

```lx
checker.constraintsById: Map<ConstraintId, Constraint>
checker.constraintStatus: Map<ConstraintId, "pending"|"deferred"|"solved"|"errored">
```

### 5.2 Solver loop

Pseudo:

1. Pop constraint from `pending`.
2. Attempt solve:

   * if solved: mark solved
   * if deferred(blockingVars): register watchers for each blocking var, mark deferred
   * if error: emit diagnostic (subject to policy), contain types as needed, mark errored

The loop stops when:

* pending queue is empty, OR
* solverBudgetSteps is exhausted (LSP mode)

### 5.3 Blocking variables

Each constraint type implements:

```lx
fn blockingTypeVars(checker, c) -> [TypeVarId]
```

Rules:

* Return root unbound typevar IDs that caused deferral.
* If there is no clear blocker, the solver must conservatively treat it as solvable-or-error (do not defer forever without watchers).

Examples:

* `HasField`: base is unbound TypeVar -> block on base root
* `Call`: callee unbound TypeVar -> block on callee root (unless inference-from-call binds it)
* `Index`: base unbound -> block on base root; `Indexable` ambiguous -> block on base root and possibly key/elem roots
* `KeyLike`: key unbound -> block on key root

### 5.4 Triggering rechecks on binding changes

`bindTypeVar(id, newType)`:

* if binding actually changes:

  * increment `typeVarVersion[id]`
  * enqueue all constraints in `watchers[id]`
  * clear watchers list for `id` (or keep, but avoid duplicates)

**Dedup requirement:** pending queue must not balloon with duplicate entries. Use a `queuedConstraints` set.

---

## 6. Units and Incremental Rechecking

### 6.1 Per-unit artifacts

Each Unit stores:

```lx
unit = .{
  unitId: UnitId,
  declId: DeclId,
  constraints: [ConstraintId],
  dependsOnDecls: Set<DeclId>,
  publicTypeFingerprint: String|Number,
  lastCheckedRevision: Number,
}
```

### 6.2 Dependency tracking during traversal

During `checkIdentifier`:

* when identifier resolves to `declaredAt = declId`:

  * if `checker.currentUnitId` is set:

    * add edge: `Unit(currentUnitId) dependsOnDecl(declId)`

### 6.3 Dirtying and re-check schedule (LSP mode)

On edit, the front-end passes `dirtyUnits`.
Typechecker:

1. Invalidate those units’ constraints (remove them from pending/constraints store).
2. Re-run type collection only for dirty units to re-emit constraints.
3. If dependency graph exists:

   * recheck dependents only if the dirty unit’s **public type** changed (see 6.4)

Batch mode ignores dirtyUnits and checks all units.

### 6.4 Public type change rule

A Unit’s public type is:

* for function decl: derefAll(fnTypeVar or fn signature type)
* for let decl: derefAll(binding type)

Compute fingerprint:

* structural hash (kind + children fingerprints) or `typeEquals` comparisons with caching

Only propagate invalidation to dependents when fingerprint changes.

---

## 7. Diagnostics Policy

### 7.1 Definitive vs provisional

Diagnostics have:

* `severity: "error" | "warning" | "hint"`
* `confidence: "definitive" | "provisional"`

**Batch mode:** may emit definitive errors and finalize unresolved constraints with containment.

**LSP mode:**

* In `definitive-only`, emit only errors that are independent of unknowns, or that compare concrete incompatible types.
* In `provisional`, you may emit “likely” errors, but must avoid flicker by:

  * suppressing unresolved-call/index diagnostics
  * suppressing “missing field” if base is still unbound TypeVar
  * prefer deferring over containing to Any

### 7.2 Finalization rules by mode

* Batch: run `finalizeIndexables` and `finalizeConstraints`.
* LSP:

  * `finalizeConstraints`: **disabled**
  * `finalizeIndexables`: either disabled or only for display (no error emission)

---

## 8. Implementation Constraints / Invariants

1. **Unification must be monotone**: once a typevar is bound to a concrete type, later bindings must refine or error, not arbitrarily replace with unrelated types unless containment is invoked by diagnostics policy.

2. **No global rescans** in the solver:

   * the solver must not iterate all constraints repeatedly; it must be queue-driven.

3. **Constraints are stable objects** referenced by ID:

   * mutation is allowed (e.g., Call -> OverloadCall), but solver must re-enqueue if mutation changes solvability.

4. **Environment lookups remain lexical**:

   * closure capture works by referencing outer decl bindings.

---

## 9. Migration Plan (phased delivery)

### Phase 1: Bidi hotspots

* Introduce `checkExpr`.
* Apply to calls (when callee known), returns, arithmetic/unary.

### Phase 2: Reactive solver

* Add `ConstraintId`, `pending`, `watchers`, blocking typevar extraction.
* Keep same constraint semantics.

### Phase 3: Two-mode scheduling

* Implement `opts.mode`.
* Disable finalize in LSP; add budgeted solver.

### Phase 4: Units + dependency graph

* Track `currentUnitId` during traversal.
* Record `dependsOnDecls`.
* Recheck only dirty units; propagate if public type changes.

### Phase 5: Stable IDs across edits (optional)

* Replace node-id-based DeclId with stable symbol IDs for persistent caching.

---

## 10. Test Requirements

### 10.1 Solver tests

* “Deferred then solved” constraints are retried when a specific typevar binds.
* No full rescans: ensure queue size and attempts scale with impacted constraints.

### 10.2 LSP stability tests

* Editing inside one function does not produce unrelated diagnostics elsewhere.
* Budget exhaustion returns partial types with `meta.didReachBudget = true` and no finalize-style errors.

### 10.3 Batch completeness tests

* Batch mode produces same or more diagnostics than LSP.
* Finalization converts Indexable -> Array/Map deterministically given evidence.

