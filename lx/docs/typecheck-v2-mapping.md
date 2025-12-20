# Implementation Mapping Spec for Typecheck v2

**Bidirectional typing + Reactive solver + LSP vs Batch scheduling + Units/Deps**

## 1) New/Changed Data Structures

### 1.1 Checker fields (additions)

Extend `makeChecker(resolveResult)` to include:

```lx
.{
  // Existing
  types: .{},
  typeVarBindings: .{},
  errors: [],
  hadError: false,
  ...

  // v2: solver orchestration
  nextConstraintId: 1,
  constraintsById: .{},        // ConstraintId -> constraint object
  constraintStatus: .{},       // ConstraintId -> "pending"|"deferred"|"solved"|"errored"
  pending: [],                 // queue (array + head index) of ConstraintId
  pendingHead: 0,              // head index to pop from pending
  queued: .{},                 // ConstraintId -> true (dedupe)
  watchers: .{},               // TypeVarId -> [ConstraintId]
  // Track re-enqueue triggers precisely
  typeVarVersion: .{},         // TypeVarId -> integer
  changedTypeVars: .{},        // TypeVarId -> true (optional; mostly for debugging)

  // v2: modes/budget
  mode: "batch" or "lsp",
  solverBudgetSteps: nil,      // only in lsp
  solverStepsUsed: 0,
  didReachBudget: false,

  // v2: unit/dependency tracking
  currentUnitId: nil,          // UnitId while traversing unit body
  units: .{},                  // UnitId -> unit record
  declToDependents: .{},       // DeclId -> Set(UnitId) (optional; can be derived later)
  dirtyUnits: nil,             // passed from opts
  checkedUnits: [],            // meta output
}
```

### 1.2 Queue helpers (add)

Implement three helpers:

```lx
fn enqueueConstraint(checker, cid) {
  if checker.queued[cid] { return }
  checker.queued[cid] = true
  push(checker.pending, cid)
}

fn dequeueConstraint(checker) {
  if checker.pendingHead >= len(checker.pending) { return nil }
  let cid = checker.pending[checker.pendingHead]
  checker.pendingHead = checker.pendingHead + 1
  checker.queued[cid] = nil
  cid
}

fn watchConstraint(checker, typeVarId, cid) {
  let ws = checker.watchers[typeVarId]
  if !ws { ws = []; checker.watchers[typeVarId] = ws }
  push(ws, cid)
}
```

### 1.3 Constraint IDs (change emission)

Replace `push(checker.constraints, constraintObj)` with:

```lx
fn addConstraint(checker, c) {
  let id = checker.nextConstraintId
  checker.nextConstraintId = id + 1
  c.id = id
  checker.constraintsById[id] = c
  checker.constraintStatus[id] = "pending"
  enqueueConstraint(checker, id)

  // If currently collecting inside a Unit, record ownership
  if checker.currentUnitId {
    let u = checker.units[checker.currentUnitId]
    if u { push(u.constraints, id) }
  }

  id
}
```

Then update `emitEq/emitCall/emitHasField/emitIndex/emitIndexSet/emitKeyLike` to call `addConstraint`.

---

## 2) Bidirectional Typing Mapping

### 2.1 Introduce `checkExpr`

Add:

```lx
fn checkExpr(checker, node, expected, blameNodeId, msg) {
  let actual = synthExpr(checker, node)

  // Use unify eagerly for bidi hot paths (lower latency, fewer constraints).
  if !unify(checker, actual, expected) {
    addError(checker, blameNodeId or node.id, msg or "Type mismatch")
    // Containment: in LSP mode, be conservative (avoid forcing Any eagerly)
    // In batch mode, you may choose to contain (see section 5).
    containEqFailure(checker, deref(checker, actual), deref(checker, expected))
  }

  checker.types[node.id] = expected
  expected
}
```

**Guideline:** prefer eager `unify` inside `checkExpr` to reduce constraint volume. Keep `emitEq` for non-bidi contexts or when you want deferred behavior.

### 2.2 Map call typing to bidi first, constraint fallback second

Update `checkCall`:

Current:

* synth callee, synth args, fresh out, emitCall(callee,args,out)

New:

1. `calleeT = synthExpr(...)`, `calleeD = deref(checker, calleeT)`
2. If `calleeD.kind == "Function"`:

   * enforce too-few args immediately if required known
   * check args against params using `checkExpr` where possible
   * unify output with return
   * set node type to return
3. Else fallback to `emitCall` as today

Pseudo:

```lx
fn checkCall(checker, node) {
  let calleeT = synthExpr(checker, node.callee)

  // synth args once so types table is populated; but checkExpr can be used too
  let argsNodes = node.args or []
  let argsTs = []
  for i ... { push(argsTs, synthExpr(checker, argsNodes[i])) }

  let calleeD = deref(checker, calleeT)
  if calleeD.kind == "Function" {
    let params = calleeD.params
    let required = (calleeD.minArity != nil) and calleeD.minArity or len(params)
    if len(argsNodes) < required {
      addError(checker, node.id, "Too few arguments: expected at least " + str(required) + ", got " + str(len(argsNodes)))
      let t = typeAny()
      checker.types[node.id] = t
      return t
    }

    let limit = len(params) < len(argsNodes) and len(params) or len(argsNodes)
    for let i = 0; i < limit; i = i + 1 {
      // For non-function params: checkExpr(argNode, paramType,...)
      // For function params: keep your arity-subtyping rule (see 2.3)
      let paramT = params[i]
      if deref(checker, paramT).kind == "Function" {
        // preserve existing special-case logic: shorter-arity fn is ok
        // easiest mapping: leave as unify logic (same as current solver path)
        let argT = argsTs[i]
        // ... same logic as in Call solver for function arguments ...
      } else {
        checkExpr(checker, argsNodes[i], paramT, argsNodes[i].id, "Argument mismatch")
      }
    }

    // out type is return type
    checker.types[node.id] = calleeD.return
    return calleeD.return
  }

  // fallback: emit Call constraint
  let out = freshTypeVar(checker)
  let argNodeIds = []
  for i ... { push(argNodeIds, argsNodes[i].id) }
  emitCall(checker, calleeT, argsTs, argNodeIds, out, node.id)
  checker.types[node.id] = out
  out
}
```

### 2.3 Preserve your “callback arity rule” locally

You currently implement:

* passing shorter-arity function where longer callback expected is OK
* passing longer-arity function where fewer args expected is unsafe

Keep that logic either:

* in the bidi call path (for immediate checks), AND
* in the `Call` constraint solver (for fallback cases)

Do not remove it from the solver until bidi call path covers most calls.

### 2.4 Map returns to bidi checks

Update `checkReturn`:

Current:

* emitEq(currentReturn, rt)

New:

* if currentReturn exists and node.value exists:

  * `checkExpr(checker, node.value, checker.currentReturn, node.id, "Return mismatch")`
* else unify Nil/expected as appropriate

### 2.5 Map arithmetic/unary to bidi checks

Replace `emitEq` in these nodes with `checkExpr`:

* `-x` expects Number
* `!x` expects Bool (or Any if that’s your semantics; but spec should pick one)

This immediately reduces deferred `Eq` constraints.

---

## 3) Reactive Solver Mapping

### 3.1 Replace `solveWorklist(...)` with queue loop

Delete or bypass `helper.solveWorklist` usage for v2. Replace `solveConstraints(checker)` with:

```lx
fn solveConstraints(checker) {
  let budget = checker.mode == "lsp" and checker.solverBudgetSteps or nil

  for true {
    if budget != nil and checker.solverStepsUsed >= budget {
      checker.didReachBudget = true
      return
    }

    let cid = dequeueConstraint(checker)
    if !cid { return }

    checker.solverStepsUsed = checker.solverStepsUsed + 1

    let c = checker.constraintsById[cid]
    if !c { continue } // might have been removed by unit invalidation

    // already solved/errored?
    let st = checker.constraintStatus[cid]
    if st == "solved" or st == "errored" { continue }

    let res = trySolveConstraintV2(checker, c)

    if res.status == "solved" {
      checker.constraintStatus[cid] = "solved"
    } else if res.status == "defer" {
      checker.constraintStatus[cid] = "deferred"
      // register watchers
      let bs = res.blocking or []
      for let i = 0; i < len(bs); i = i + 1 { watchConstraint(checker, bs[i], cid) }
    } else if res.status == "error" {
      checker.constraintStatus[cid] = "errored"
    } else {
      // defensive: treat unknown as deferred with no watchers -> not allowed
      // convert to error to avoid stuck constraints
      addError(checker, c.nodeId, "Internal: solver returned unknown status")
      checker.constraintStatus[cid] = "errored"
    }
  }
}
```

### 3.2 Modify `bindTypeVar` to trigger watchers

Update `bindTypeVar(checker, id, type)`:

* Keep your “no-op rebind suppression” via `typeEquals`.
* On real change:

  * increment `typeVarVersion[id]`
  * enqueue all watchers for `id`
  * clear watchers[id] list (recommended to avoid duplicates)
  * optionally record `changedTypeVars[id] = true`

```lx
fn bindTypeVar(checker, id, type) {
  let old = checker.typeVarBindings[id]
  if old and typeEquals(checker, old, type) { return }

  checker.typeVarBindings[id] = type

  // versioning
  let v = checker.typeVarVersion[id] or 0
  checker.typeVarVersion[id] = v + 1

  checker.changedTypeVars[id] = true

  // wake watchers
  let ws = checker.watchers[id]
  if ws {
    for let i = 0; i < len(ws); i = i + 1 { enqueueConstraint(checker, ws[i]) }
    checker.watchers[id] = nil
  }
}
```

### 3.3 Change `trySolveConstraint` to return {status, blocking?}

Create `trySolveConstraintV2(checker, c)` returning:

* `{ status: "solved" }`
* `{ status: "defer", blocking: [TypeVarId, ...] }`
* `{ status: "error" }`

Mapping rule:

* Everywhere you currently `return false` to defer, replace with `"defer"` and report blocking vars.

#### Blocking var extraction helpers

Add:

```lx
fn rootUnboundTypeVarId(checker, t) {
  let info = derefWithRootVar(checker, t)
  let root = info.root
  let dt = deref(checker, info.type)
  if root and root.kind == "TypeVar" and dt.kind == "TypeVar" and !getTypeVarBinding(checker, root.id) {
    return root.id
  }
  nil
}
```

For constraints, use:

* `rootUnboundTypeVarId(checker, c.base)` etc.

#### Example mappings

**HasField**
Current defers if base is TypeVar unresolved.
New:

* if base unresolved -> `{defer, blocking:[baseRootId]}`
* Optional: if Option unwrap inner unresolved -> block on inner root id too

**KeyLike**
Current: if typevar unbound -> `return false`.
New: `{defer, blocking:[rootId]}`

**Index/IndexSet**
Current: several `return false` cases:

* base unresolved -> block on base root
* base Indexable ambiguous -> block on base root (and optionally key root)
  New: return defer with that blocking list.

**Call**
Your current solver often *solves* an unbound callee by binding it to a function inferred from call. That should stay `solved`.
If you do want to defer in some cases, block on callee root.

---

## 4) Units + Dependency Graph Mapping

### 4.1 Add unit records

Define:

```lx
fn makeUnit(unitId, declId) {
  .{
    unitId: unitId,
    declId: declId,
    constraints: [],
    dependsOnDecls: .{}, // set map declId->true
    publicFingerprint: nil,
    dirty: true,
  }
}
```

Store in `checker.units[unitId]`.

### 4.2 Decide Unit boundaries (v2 initial)

Start with top-level:

* each top-level `Let` initializer is a Unit
* each top-level `Function` body is a Unit

You already have `predeclareTopLevel`. Extend it to also create units:

```lx
fn declareTopLevelUnits(checker, ast) {
  for each top-level node:
    if Let with init: unitId = node.name.id (or binding.declaredAt); declId = node.name.id
    if Function: unitId = node.id; declId = node.id
    checker.units[unitId] = makeUnit(unitId, declId)
}
```

### 4.3 Track dependencies during identifier checking

In `checkIdentifier` after computing `declId = binding.declaredAt`:

* if `checker.currentUnitId` exists:

  * `checker.units[currentUnitId].dependsOnDecls[declId] = true`

This gives you `unit -> decl` edges cheaply.

### 4.4 Invalidation and selective recheck (LSP)

Introduce `opts.dirtyUnits` input.
In `typecheck(ast, resolveResult, opts)`:

* Set `checker.mode`, `checker.solverBudgetSteps`, `checker.dirtyUnits`
* If LSP mode:

  * create units upfront (`declareTopLevelUnits`)
  * mark only passed dirty units as dirty; mark others as clean
  * then only traverse dirty units’ bodies/initializers with `checker.currentUnitId` set

Traversal mapping:

* Instead of `synthExpr(checker, ast)` over whole root block, do:

```lx
fn checkDirtyUnits(checker, ast) {
  // ast is Block
  for each top-level node:
    let unitId = ...
    if checker.mode == "batch" or checker.units[unitId].dirty {
      checker.currentUnitId = unitId
      synthExpr(checker, node) // or synth initializer/body appropriately
      checker.currentUnitId = nil
      push(checker.checkedUnits, unitId)
    }
}
```

Batch mode: mark all units dirty and process all.

### 4.5 Removing old constraints for a unit (required for real incremental)

When a unit is dirty and you are rechecking it, you must remove its prior constraints:

```lx
fn removeUnitConstraints(checker, unitId) {
  let u = checker.units[unitId]
  if !u { return }
  for each cid in u.constraints:
    checker.constraintsById[cid] = nil
    checker.constraintStatus[cid] = nil
    checker.queued[cid] = nil
  u.constraints = []
}
```

Also, watchers may hold stale cids. That is fine if `solveConstraints` checks `constraintsById[cid]` for nil and skips.

---

## 5) Two-Mode Diagnostics and Finalization Mapping

### 5.1 Split finalize paths by mode

In your current pipeline you run:

* solve
* refineFunctions
* solve
* finalizeIndexables
* solve
* finalizeConstraints

In v2:

**Batch**

* `check all units`
* `solveConstraints` (no budget)
* `refineFunctions` (or unitized refinement; see 6)
* `solveConstraints`
* `finalizeIndexables`
* `solveConstraints`
* `finalizeConstraints`

**LSP**

* `check dirty units`
* `solveConstraints` with budget
* `NO finalizeConstraints`
* `finalizeIndexables`: either skipped OR “display-only”

### 5.2 “Display-only indexable” option for LSP

Today `finalizeIndexables` emits an error on ambiguity and defaults to Map.
For LSP, avoid hard errors and avoid committing too early.

Implement:

* `finalizeIndexables(checker, opts)` with `opts.emitErrors` boolean

  * if `emitErrors=false`, do not `addError` for ambiguous; choose a presentation fallback:

    * keep Indexable bound as Indexable (best), OR
    * bind to `Map[Any, Any]` but without diagnostics
* For batch: `emitErrors=true` and keep current behavior.

### 5.3 Containment differences

In LSP mode:

* Avoid doing `bindTypeVar(..., Any)` merely because something is unknown.
* Only contain to Any when you emitted a **definitive** error (true contradiction).

Concretely:

* `containEqFailure` and `containHasFieldFailure` should be called:

  * always in batch when emitting error
  * in LSP only when the mismatch is concrete (not “base unresolved”)

This is mostly a policy choice: implement as `if checker.mode == "batch" { contain } else { contain only if both sides concrete }`.

---

## 6) Function Refinement: Mapping Options

Your current design:

* during `checkFunction`, you synth once (with fresh vars), record `functionInfos`, then later `refineFunctions` re-walks bodies.

This is compatible with v2, but for LSP you must avoid “refine all functions” each time.

### 6.1 Minimal mapping (keep current, but gate by mode/dirty)

* In batch mode: keep `refineFunctions(checker)` as you have.
* In LSP mode: refine only functions that are in dirty units.

Implementation:

* When pushing `functionInfos`, include `unitId`:

```lx
push(checker.functionInfos, .{
  unitId: checker.currentUnitId,
  node: node,
  defEnv: defEnv,
  ...
})
```

Then:

```lx
fn refineFunctions(checker) {
  for each info in checker.functionInfos:
    if checker.mode == "lsp" and !checker.units[info.unitId].dirty { continue }
    ... existing logic ...
}
```

### 6.2 Better mapping (optional later): make each function body its own unit

If nested functions become frequent, you can:

* treat nested function nodes as units as well
* schedule refinement by unit dependency graph
  This can be deferred until after reactive solver lands.

---

## 7) Dependency-Driven Recheck Propagation (Phase 4)

Once you have `unit.dependsOnDecls`, add reverse map `declToDependents`.

### 7.1 Build reverse edges

After you typecheck a unit (or at end of collection):

* for each `declId` in `unit.dependsOnDecls`:

  * add `unitId` to `declToDependents[declId]`

### 7.2 Public fingerprint computation

Define:

* for top-level function decl: fingerprint of its `fnType` (or its decl binding)
* for top-level let decl: fingerprint of its decl typevar derefAll

Compute after solver pass:

```lx
fn computePublicFingerprint(checker, unitId) -> String {
  let u = checker.units[unitId]
  let declId = u.declId
  let tv = lookupDecl(checker, declId) or nil
  let t = tv and derefAll(checker, tv) or typeAny()
  // fingerprint: a stable string hash; simplest: stringify recursively
  return typeFingerprint(checker, t)
}
```

If fingerprint changes, mark dependents dirty and enqueue them for recheck (LSP), or schedule them (batch).

**Note:** For initial v2, you can skip automatic propagation and rely on caller-provided dirtyUnits. Add propagation later.

---

## 8) File/Module-Level Scheduling (Optional Later)

If you add module imports, the same unit model applies:

* each file is a set of units
* dependencies include imported decls (or module “exports”)

Not required for v2 unless you need cross-file LSP.

---

## 9) Concrete PR/Phase Plan (Recommended)

### PR 1 — Bidi scaffolding + calls/returns/ops

* Add `checkExpr`
* Update `checkCall` to short-circuit when callee is known Function
* Update `checkReturn`, unary, arithmetic to use `checkExpr`
* No solver changes yet
* Ensure tests still pass

### PR 2 — Constraint IDs + queue + watchers + bind triggers

* Add new checker fields
* Implement `addConstraint`, `enqueue/dequeue/watch`
* Convert all `emit*` to use `addConstraint` instead of `push(checker.constraints, ...)`
* Implement `solveConstraints` queue loop (still allow batch completion)
* Convert `trySolveConstraint` -> `trySolveConstraintV2` returning status + blocking
* Keep old `finalizeConstraints` but convert it to iterate remaining constraints by scanning `constraintsById` where status != solved (acceptable transitional step)

### PR 3 — Mode split: LSP budget + finalize policy

* Add `opts.mode`, `opts.solverBudgetSteps`
* In LSP mode:

  * run solver with budget
  * skip `finalizeConstraints`
  * make `finalizeIndexables` non-erroring or skipped
* Adjust containment policy for LSP

### PR 4 — Units (top-level) + constraint ownership + dirty recheck

* Create units from top-level nodes
* Track `currentUnitId`
* Record `unit.constraints` and implement `removeUnitConstraints`
* Add `opts.dirtyUnits` to recheck only those
* Gate `refineFunctions` by dirty unit in LSP

### PR 5 — Dependency edges + optional propagation

* Record `unit.dependsOnDecls` during `checkIdentifier`
* Build reverse map and implement “public fingerprint changed -> mark dependents dirty”

---

## 10) Test Mapping (What to Add)

### 10.1 Reactive solver correctness

Create synthetic tests that:

* produce a deferred constraint (e.g., `HasField` on unknown base)
* later bind the base typevar
* assert the constraint is retried and resolved without scanning all constraints

### 10.2 LSP budget behavior

* Run in LSP mode with low `solverBudgetSteps`
* Assert:

  * `meta.didReachBudget == true`
  * no finalize-style “Unresolved call/index” errors appear
  * partial types are present (TypeVars ok)

### 10.3 Unit invalidation

* Typecheck once (batch or lsp full), then re-run LSP with `dirtyUnits=[oneFunction]`
* Assert:

  * only that unit is re-checked (meta.checkedUnits)
  * constraints from other units are not duplicated
  * diagnostics from unrelated units do not change (stability)

---

## 11) Notes on “Minimal Disruption” Choices

* You can implement the reactive solver **without** immediately rewriting all constraint logic. The key is returning `{defer, blocking:[...]}` instead of `false`.
* You can keep `refineFunctions` intact at first; just gate it by mode/dirty unit so LSP does not re-walk everything.
* You can keep `typeEquals` as-is initially; if performance remains an issue, later replace it with fingerprints/versioning.

