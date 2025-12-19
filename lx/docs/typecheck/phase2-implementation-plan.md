# Typecheck Phase 2 Implementation Plan

**Goal:** Turn Phase 1 skeleton into a useful intra-module monomorphic inferencer by adding (1) a constraint store, (2) a solver, and (3) a light second pass for functions.

## 0. Guiding Constraints

* Keep **public signature** unchanged:

  * `typecheck(ast, resolveResult, opts) -> { success, types, typeVarBindings, errors }`
* Do not mutate AST.
* Maintain side tables:

  * `checker.types[nodeId] = Type`
  * `checker.typeVarBindings[typeVarId] = Type`
* Phase 2 remains **monomorphic**: no `Scheme`, no `generalize`, no `instantiate`.
* Target: eliminate “Unknown everywhere” when definitions are used within module.

---

## 1. Step 1 — Establish a Minimal Type Core (Lock It)

Before adding constraints, freeze the “Phase 2 minimal type universe”:

### Keep

* `Any, Nil, Number, Bool, String`
* `TypeVar(id)`
* `Function(params, return)`
* `Array(elem)`
* `Record(fields)` (closed *final* shape)

### Drop / Disable in Phase 2

* `Option`, `Map`, `Tagged`, `Scheme`, narrowing, knownTags, etc.

**Implementation note:** You can keep constructors around if used elsewhere, but Phase 2 pipeline should not depend on them.

---

## 2. Step 2 — Refactor Checker State to Add Constraints

Extend `makeChecker`:

### Add fields

```lx
constraints: [],          // worklist store
nextConstraintId: 1,      // optional, for debugging
debugConstraints: false,  // opts.debugConstraints
```

### Constraint representation (minimal)

Use plain objects:

```lx
// Equal constraint: t1 must unify with t2
.{ kind: "Eq", t1, t2, nodeId, msg }

// HasField constraint: base must be record-like and contain fieldName with fieldType
.{ kind: "HasField", base, fieldName, fieldType, nodeId, msg }
```

### Add helpers

* `emitEq(checker, t1, t2, nodeId, msg)`
* `emitHasField(checker, base, name, outType, nodeId, msg)`

No solving yet—just store.

---

## 3. Step 3 — Change Expression Checking to Emit Constraints (Collector Pass)

This is the most important mechanical change: **replace eager `constrain/unify` calls** with constraint emission wherever possible.

### 3.1 Identifiers / bindings

Keep Phase 1 behavior: each declaration gets a `TypeVar`, identifier references resolve to that `TypeVar` (or `Any` for unknown globals/builtins if you kept that).

### 3.2 Function declarations (top-level)

**Phase 2 needs forward references to work**, so implement “predeclare” (Step 4), but inside function checking you should:

* Create `paramTVs` and `retTV`.
* Set function’s type to `Function(paramTVs, retTV)` and `emitEq(bindingTV, fnType, node.id, ...)`
* When checking body:

  * Assign `param.id -> paramTV` in env
  * Set `checker.currentReturnType = retTV`
  * Traverse body and emit constraints:

    * On explicit `return expr`: `emitEq(type(expr), retTV, ...)`
    * On implicit return from last expression: same, if you choose that rule
* Store `checker.types[node.id] = fnType` (even before solving)

### 3.3 Calls

For `Call(callee, args...)`:

* `calleeT = synth(callee)`
* `argTs = map synth(args)`
* Create fresh paramTVs length = observed args length (`n`)
* Create fresh retTV
* `emitEq(calleeT, Function(paramTVs, retTV), node.callee.id, "call requires function")`
* For each i: `emitEq(argTs[i], paramTVs[i], node.args[i].id, "arg mismatch")`
* Result: `retTV`

This alone is enough to push constraints “backward” into callee types later.

### 3.4 Dot access (the key Phase 2 win)

For `Dot(obj, propName)`:

* `objT = synth(obj)`
* `fieldT = freshTypeVar(checker)`
* `emitHasField(checker, objT, propName, fieldT, node.id, "dot requires record")`
* Result: `fieldT`

Do **not** immediately refine `objT` into a record during collection. That happens in the solver.

### 3.5 Record literals

For `Hashmap` literals that are clearly “record literals” in your current AST:

* For each field `k: v`:

  * `vT = synth(v)`
  * store in fields map
* Result: `Record(fields)` (closed)
* Also store `checker.types[node.id] = recordType`

### 3.6 Arithmetic, comparisons, etc.

In collector pass:

* `Binary + - * / %`: `emitEq(left, Number)`, `emitEq(right, Number)`
* Comparison ops: same + result `Bool`
* Equality ops: result `Bool`, no constraints
* Unary `-`: operand Number
* Unary `!`: result Bool (or Any->Bool, no constraints)

Keep it minimal. If you’re worried about cascading errors, the solver will contain.

---

## 4. Step 4 — Add “Predeclare Top-Level” Pass (Enables Forward References)

Before collecting constraints from top-level expressions, walk `ast.body` and **pre-bind** all top-level declarations:

* For `Let name = ...`: bind `name.id` to fresh TypeVar in env immediately.
* For named `Function` declarations (if functions are expressions assigned to a name, treat similarly; if `fn name(...) {}` is an AST node with `node.name`, bind that too).

This makes `compile()` calling `purifyFunction()` constrain `purifyFunction`’s type even if it is defined later.

Implementation pattern:

```lx
fn predeclareTopLevel(checker, ast) {
  for each expr in ast.body:
    if expr.type == "Let": bindDeclMono(checker, expr.name.id)
    if expr.type == "Function" and expr.name: bindDeclMono(checker, expr.id) // or name.id depending on resolve contract
}
```

Match your existing resolve contract (you previously used function node id for named functions). Keep consistent with `resolvedNames.declaredAt`.

---

## 5. Step 5 — Implement the Solver (Worklist)

Add:

```lx
fn solveConstraints(checker) {
  let work = checker.constraints
  let progress = true
  let rounds = 0

  while progress and rounds < (checker.opts.maxSolveRounds or 20) {
    progress = false
    rounds = rounds + 1

    let remaining = []
    for each c in work:
      if trySolveConstraint(checker, c) {
        progress = true
      } else {
        push(remaining, c)
      }
    work = remaining
  }

  // Anything remaining: attempt one last time; if still fails, error or degrade to Any
  for each c in work:
    if !trySolveConstraint(checker, c) {
      addError(checker, c.nodeId, c.msg)
      // containment: force involved TVars to Any where possible
      containConstraintFailure(checker, c)
    }
}
```

### 5.1 `trySolveConstraint` rules

#### For `Eq`

* Use your Phase 1 `unify(checker, t1, t2)` but modify behavior:

  * Return `true` if unification succeeds
  * Return `false` only if blocked? (Eq is rarely “blocked”; it either unifies or fails)
  * On failure, don’t immediately add error here; let the outer loop decide (or do immediate error if you prefer)

In practice: `Eq` can be processed immediately; you don’t need to defer it unless you want to treat some cases as “blocked” (you likely don’t in monomorphic unification).

#### For `HasField(base, name, fieldT)`

This is where “blocked” states happen.

Let `b = deref(checker, base)`.

Cases:

1. `b.kind == "Any"`

   * Satisfy constraint: `emitEq(fieldT, Any)` effectively by binding `fieldT` to Any if it’s a TypeVar.
   * Return `true`.

2. `b.kind == "Record"`

   * If `b.fields[name]` exists:

     * unify `b.fields[name]` with `fieldT` => success/fail
     * return `true` if unify succeeded, otherwise still `true` (it’s decided), but record error later
   * Else:

     * This is a missing field on a closed record literal:

       * return `true` and mark contradiction (recommended: immediate error + contain)
       * Or return `true` and let outer addError (store msg somewhere)
     * Do **not** add fields to literal records.

3. `b.kind == "TypeVar"` (unbound or bound)

   * If TypeVar is unbound:

     * bind it to `Record({ [name]: fieldT })`
     * return `true`
   * If bound (after deref) it becomes Record: handled by case 2
   * If bound to non-record: contradiction (error + contain), return `true`

4. Otherwise (Number/String/Bool/Nil/Function/Array)

   * contradiction: dot on non-record
   * contain: set fieldT to Any
   * return `true`

### 5.2 Containment strategy (critical for tooling)

When a constraint fails:

* Prefer to bind the “output” type to `Any`:

  * For `HasField`, bind `fieldT` (if TypeVar) to `Any`
  * For `Eq`, bind both sides if they’re TypeVars, otherwise do nothing
    This keeps the rest of the module typecheck proceeding.

---

## 6. Step 6 — Add Phase 2 Second Pass for Functions (Recheck After Solve)

This is how you get from “some constraints” to “fully propagated results” without implementing HM.

### Why it matters

During the first collection pass, some function bodies may generate `HasField` constraints on parameters, but those parameters might not yet have had their shapes constrained by call sites until after solving.

So:

1. Collect constraints (Pass 1)
2. Solve
3. Re-walk function bodies and collect additional constraints with improved parameter types now available
4. Solve again

### Implementation approach

Store a list of function nodes during pass 1:

```lx
checker.functionNodes = []  // list of function AST nodes or ids
```

Then:

```lx
fn phase2RefineFunctions(checker) {
  for node in checker.functionNodes:
    // enter scope
    // bind params to the same param TVars used in the function's Function type
    // re-walk body emitting constraints again
}
```

Important: **reuse the same param/return TypeVars** (don’t create new ones on pass 2).

* If you stored `fnType = checker.types[node.id]` you can extract `fnType.params` and `fnType.return`.

Then run `solveConstraints` again (new constraints only, or all constraints; easiest is append and solve with worklist again).

---

## 7. Step 7 — Finalization / Output Tables

After solving (and refine+solve again), create:

* `derefedTypes[nodeId] = derefAll(checker, checker.types[nodeId])`
* `derefBindings[typeVarId] = derefAll(checker, checker.typeVarBindings[typeVarId])`

Return:

```lx
.{
  success: !checker.hadError,
  types: derefedTypes,
  typeVarBindings: derefBindings,
  errors: checker.errors,
}
```

This should make your `debug.lx` stop printing only `Unknown` for functions that are constrained by usage.

---

## 8. Incremental Milestones (So You Don’t Get Lost)

### Milestone A — Constraints exist, but solver is stub

* Implement constraint store and `emit*` helpers.
* Convert `Dot` to `HasField` emission.
* Keep everything else eager like Phase 1.
* Expect no improvement yet, but confirm constraint logging works.

### Milestone B — Solve `HasField` only

* Implement `HasField` solving rules.
* Your example `purifyFunction(func) { func.name }` called with `. { name: "x" }` should infer `name: String`.

### Milestone C — Convert Calls to constraints

* Implement call constraints `Eq(callee, Function(params, ret))`
* This enables propagating argument record shapes into function parameters.

### Milestone D — Second pass refine

* Add refine pass + second solve.
* This is where “Unknown everywhere” usually collapses into concrete types.

---

## 9. Engineering Notes (Pragmatic Decisions)

### Record growth policy

For Phase 2 minimal:

* **Allow TypeVar-origin records to grow** (from HasField).
* **Do not allow literal records to grow** (missing field = error).

You can implement this without extra tags by:

* Only growing when the base was an unbound `TypeVar` that you bind to a Record in the solver.
* Once it’s a record literal, it never grows.

### Arity semantics

If you want to keep “extra args ignored”:

* Collector: still emit constraints only for the first `paramCount` (if known).
* But in Phase 2, paramCount is often “observed args length”, so simplest is to **require exact** in Phase 2 and reintroduce Lua-ish widening later.

Given your earlier stance, I recommend:

* Phase 2: exact arity (reduces complexity)
* Phase 3+: widen if desired

---

## 10. Smoke Test Targets (What You Should See Change)

### Test 1

```lx
fn getName(x) { x.name }
let a = .{ name: "A" }
getName(a)
```

Expected after Phase 2:

* `getName` type becomes `Function([Record({name:String})], String)` (modulo formatting)

### Test 2 (your scenario)

`purifyFunction` called by `compile` should no longer be `Unknown` if `compile` passes something with `.name`.

---

## 11. Suggested File/Function Layout in `lx/src/typecheck.lx`

To keep the codebase maintainable:

1. Type constructors + deref + occurs + unify
2. Env + predeclare
3. Constraint store helpers
4. Collector (`synthExpr` + `check*` functions) — emits constraints
5. Solver (`solveConstraints`, `trySolveConstraint`, containment)
6. Refine pass for functions
7. `typecheck` main API

