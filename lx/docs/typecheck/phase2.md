# Typecheck Phase 2 Specification

**Status:** Design / Next step after Phase 1 skeleton
**Scope:** Improve results from `Unknown`-heavy Phase 1 by adding *intra-module* propagation and deferred solving, while preserving the same public API signature.

## 1. Goals

Phase 2 upgrades Phase 1’s “single-pass, eager” checker into a **two-stage constraint system** that:

1. **Collects constraints** during traversal instead of immediately failing or binding irreversibly.
2. **Solves constraints** after collection (and optionally in a second wave) to:

   * Bind function parameter/return types more often
   * Improve record/field typing on generic parameters
   * Reduce “Unknown” at definition sites when usage exists
3. Keeps the checker **monomorphic** (still no generalization / schemes).

Phase 2 must remain:

* Local (single module, no whole-program inference)
* Fast (tooling-friendly)
* Best-effort (errors do not abort; `Any` used to contain cascades)

## 2. Non-Goals (Explicit)

Phase 2 does **not** add:

* Polymorphism (no `Scheme`, no `generalize`, no instantiation)
* Full flow sensitivity / narrowing (still Phase 3)
* Tagged unions (Phase 3)
* Full import signature propagation (Phase 3+; Phase 2 may treat imports as `Any` or “opaque module type”)
* General subtyping (no variance system; function arity widening stays as a targeted rule only if you keep it)

## 3. External Contract (Must Not Change)

Public function signature remains:

```lx
typecheck(ast, resolveResult, opts) -> {
  success: bool,
  types: { nodeId → Type },
  errors: [{ nodeId, message, severity }],
  typeVarBindings?: { typeVarId → Type } // optional but recommended for debugging/tooling
}
```

Also preserved:

* **No AST mutation**
* All information stored in side tables keyed by nodeId

## 4. Key Problem Phase 2 Solves

Phase 1 yields `Unknown` for:

```lx
fn purifyFunction(func) {
  .{ name: func.name }
}

fn compile() {
  let f = endCompiler() // concrete record type
  purifyFunction(f)
}
```

In Phase 2, the call site must constrain `func` enough that `func.name` becomes concrete, and thus `purifyFunction`’s return record becomes concrete as well *within the module*.

## 5. Core Idea: Constraints First, Solve Later

### 5.1 Type State

Phase 2 still uses `TypeVar` + bindings, but **unification is not the only propagation mechanism**. We add a constraint store.

**Types remain Phase-1 core:**

* `Any, Nil, Number, Bool, String`
* `TypeVar(id)`
* `Function(params, return)`
* `Array(elem)`
* `Record(fields)` (closed shape)

### 5.2 Constraint Forms (Minimum Set)

Phase 2 introduces these constraints:

1. **Equal**

   * `t1 ≈ t2` with origin nodeId + message
2. **HasField**

   * `base has field "a" : tf`
   * Supports accumulating record shape over time
3. **HasIndex** (optional, if you support `arr[i]` and map-like)

   * For Phase 2 minimalism: you may keep Phase 1’s Array-only index and defer maps.

Recommended minimal set for immediate value:

* `Equal`
* `HasField`

### 5.3 Where Constraints Are Generated

During traversal (same dispatcher as Phase 1), instead of forcing eager resolution:

* For `Binary +`:

  * emit `Equal(left, Number)` and `Equal(right, Number)` and return `Number` (or a `TypeVar` constrained to `Number`)
* For function call:

  * emit `Equal(callee, Function(paramTVs, retTV))`
  * emit `Equal(arg_i, param_i)` for provided args
* For dot access `x.a`:

  * create fresh `tf`
  * emit `HasField(T(x), "a", tf)`
  * return `tf`

Critically: **dot access does not invent a fresh record immediately**; it emits `HasField` which later may:

* refine `TypeVar` into a `Record` with that field
* unify if already a `Record`
* error if not record-like

## 6. Two-Pass Structure

Phase 2 uses two logical passes:

### Pass A: Predeclare & Collect

* Predeclare all top-level `let` and `fn` names into the environment with fresh `TypeVar`s (enables forward refs).
* Traverse AST to:

  * synthesize “result types” (often `TypeVar`s)
  * **emit constraints** instead of fully solving
* During this pass:

  * Do not emit cascading errors when constraints are merely unsolved.
  * Only emit errors for hard structural impossibilities (e.g., `Call` on literal number).

### Solve 1: Primary Solve

* Run constraint solver over collected constraints.
* Produce typeVar bindings and error list.

### Pass B: Recheck Bodies (Optional but recommended)

* Re-walk function bodies after Solve 1 using updated bindings to:

  * generate additional constraints that were previously blocked by unknown callee/param types
  * improve return propagation
* Then run Solve 2.

This is similar in spirit to your earlier “two-pass” doc, but now with a **constraint store** rather than “defer failed unify”.

## 7. Constraint Solver Specification

### 7.1 Solver Inputs / Outputs

Input:

* `constraints: [Constraint]`
* existing `typeVarBindings`
  Output:
* updated `typeVarBindings`
* `errors`

### 7.2 Solver Order and Strategy

A practical approach that stays small:

1. Iterate constraints in a worklist until no progress:

   * Try to **discharge** each constraint given current bindings.
   * If solvable, apply bindings/unifications and continue.
   * If blocked on unknowns, keep it for later iterations.
2. After N iterations (or no progress):

   * Remaining constraints that are still contradictory become errors (best-effort).

### 7.3 Handling `Equal(t1, t2)`

* Use unification (Phase 1 unify) but do **not** immediately “panic” on mismatch:

  * Record error with origin
  * Bind the participating TypeVar(s) to `Any` as a containment strategy
  * Continue

### 7.4 Handling `HasField(base, name, tf)`

Let `baseD = deref(base)`:

* If `baseD` is `TypeVar(id)`:

  * If the TypeVar is unbound:

    * bind it to `Record({ name: tf })`
  * If bound to `Record(fields)`:

    * if field exists: emit `Equal(fields[name], tf)`
    * else: extend the record with `{ name: tf }`

      * **This is the Phase 2 shift:** record shapes become “growable during inference”
      * But the *final* record type remains closed: after solving, it’s fixed.
* If `baseD` is `Record(fields)`:

  * If name exists: `Equal(fields[name], tf)`
  * Else: **error** (closed record mismatch) OR permissive mode:

    * recommended for Phase 2: allow extension *only if record originated from a TypeVar*; literal record remains closed.
    * See 7.6 “Origin-aware record growth”.
* Else:

  * If `baseD` is `Any`: constraint is satisfied (result `Any`)
  * Otherwise: error (dot on non-record)

### 7.5 Occurs Check

Occurs check remains mandatory when binding TypeVar to a structure that contains itself.

### 7.6 Origin-aware Record Growth (Recommended)

To avoid making *every* record implicitly open:

* Tag `Record` types with an `origin`:

  * `origin: "literal"` for `.{ ... }`
  * `origin: "var"` for records created by `HasField` on a `TypeVar`
* Rule:

  * only `"var"` records can grow
  * `"literal"` records are closed immediately (missing field is an error)

This preserves “records are closed” in user intuition, while still enabling parameter-shape inference.

## 8. Function Types in Phase 2

### 8.1 Calls Create Param/Return Constraints

For `f(a1..an)`:

* create fresh `P1..Pk` and `R`
* add `Equal(T(f), Function([P1..Pk], R))` where `k = arity we observe`

For dynamic languages, you have two options:

* **Option 1 (simple):** use `k = n` at each call, and unify; mismatched arities become errors.
* **Option 2 (Lua-ish):** keep your arity widening rule:

  * extra args ignored
  * missing args error/warn
  * For Phase 2, keep this rule *only* in call checking; do not implement variance/subtyping.

### 8.2 Propagating Return Types

* Every `return expr` adds `Equal(T(expr), currentReturnTV)`
* Implicit return:

  * If function body is an expression, `Equal(T(bodyLast), returnTV)`
  * If body is a block, use last expression, unless it ends with explicit `return`

This is still monomorphic.

## 9. What “Better” Means (Acceptance Criteria)

Phase 2 is successful if these become true inside a single module:

1. **Call-site constrains parameter field access**

   ```lx
   fn getName(x) { x.name }
   let a = .{ name: "A" }
   getName(a)
   ```

   Expected: `getName : ({ name: String }) -> String`

2. **PurifyFunction stops returning Unknown**

   * At least for fields accessed, return record types become concrete if call sites provide concrete arguments.

3. **Definition-only remains Unknown**

   ```lx
   fn f(x) { x.name }
   f
   ```

   Expected: still `Unknown`/`Any` unless used.

## 10. Error Strategy (Phase 2)

* Errors are recorded with origin nodeId
* On contradiction, bind conflict to `Any` to prevent cascades
* `success` is false iff any `severity: "error"` occurred

## 11. Implementation Checklist

### Phase 2A — Infrastructure

* Add `ConstraintStore`
* Add constraint constructors: `Equal`, `HasField`
* Ensure every constraint has:

  * `originNodeId`
  * `message` (or message key)

### Phase 2B — Collector Pass

* Replace eager `constrain` calls with:

  * `emitEqual(...)`
  * `emitHasField(...)`

### Phase 2C — Solver

* Worklist solver loop with progress tracking
* Unify + record-growth rules
* Occurs check enforcement

### Phase 2D — Optional Second Pass

* Re-walk deferred function bodies after Solve 1
* Emit new constraints, run Solve 2

## 12. Phase 2 Test Plan (High-Level)

Minimum regression tests:

* Field access propagation through call
* Multiple field accesses accumulate into a single record shape
* Conflicting field types across call sites produce one error and `Any` containment
* Literal record remains closed (missing field error)
* TypeVar-derived record can grow as fields are discovered

---

## 13. Notes on Your “Unknown for purifyFunction” Output

With Phase 1-only skeleton behavior, the output you saw:

* `Function : Unknown`
* `purifyFunction : Unknown`
* `compile : Unknown`

is **expected** if Phase 1 does not:

* solve constraints globally after collecting usage, and/or
* revisit function bodies after call-site constraints exist.

Phase 2 explicitly addresses this by:

* emitting `HasField` constraints from `func.name`
* binding `func` to a record type once a call provides an argument type
* solving after collection

