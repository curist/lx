# Lx Pass Pipeline Spec (Unified Pass Contract)

This document specifies a unified pass interface and a generic pipeline runner so new compiler passes can be added/reordered with minimal driver glue, while keeping consolidated diagnostics and robust source-position resolution via origin chains.

It is written as an implementation handoff: another LLM (or human) should be able to implement the plan directly.

---

## Goals

- Make adding a new pass “append a descriptor” (no bespoke plumbing in `driver.lx`).
- Standardize pass I/O shape (`success`, `errors`, optional `ast`, `nextNodeId`, `origin`, pass-specific artifacts).
- Centralize error collection and formatting, including future passes that allocate nodes.
- Keep the existing `driver.compileModule()` external shape working during migration (tests + CLI + scripts).

## Non-goals

- Rewriting every existing pass to a brand-new internal representation immediately.
- Enforcing “no AST mutation ever”. Instead: declare and minimize mutation.
- Changing codegen/verifier/object format behavior as part of this work.

---

## Current Observations (Repo Reality)

- Many callers and tests read `result.parseResult/lowerResult/anfResult/resolveResult/typecheckResult` directly.
- `errors.resolveNodePosition()` currently follows only `anfResult.origin` then `lowerResult.origin`, then indexes `parseResult.ast`.
- Some passes already mutate/annotate AST nodes:
  - `resolve.lx` mutates import nodes (`node.importResult`, `node.importType`, `node.importBindings`).
  - `anf-inline.lx` mutates the AST in-place (explicitly).
  - `lower.lx` and `anf.lx` currently copy forward `importResult` fields.
  - `typecheck.lx` reads `node.importType` in `checkImport()` (see `lx/src/passes/frontend/typecheck.lx`).

This spec aims to make new passes cheap to integrate *without* requiring a full immutability purge on day 1, while providing a path to re-commit to “semantic info lives in pass artifacts, not AST annotations”.

---

## Terminology

- **Pass**: a transformation/analysis stage. May produce a new AST, side tables, diagnostics, etc.
- **Origin map**: `{ newNodeId -> oldNodeId }` mapping for provenance, used for error position resolution.
- **Pipeline state**: the threaded state between passes (current AST, nodeId counter, pass results so far).

---

## Unified Data Shapes

### Pass descriptor

```lx
// Required: name, run(state, ctx)
// Optional: enabled(ctx, state), mutatesAst, fatal, requires/provides
let pass = .{
  name: "lower",

  // Optional: default true if absent.
  enabled: fn(ctx, state) { true },

  // Optional: default false if absent. Used for debugging/expectations.
  mutatesAst: false,

  // Optional: default true if absent.
  // If false, a failure in this pass does not stop the pipeline; the pass output
  // is recorded and the pipeline continues. Intended for "best-effort" passes
  // used by tooling (e.g. current typecheck).
  fatal: true,

  // Optional dependency metadata for pass-manager validation.
  // If present, the pipeline builder should ensure these are satisfied by
  // earlier passes in the chosen pipeline.
  requires: ["resolve"],     // example
  provides: ["resolvedNames"], // example

  // Required.
  // Returns a PassResult-like map (see below).
  run: fn(ctx, state) {
    // ...
  },
}
```

### Pipeline state (threaded)

```lx
let state = .{
  path: "file.lx",      // module identity key, also for diagnostics
  source: "…",          // source text (driver-provided)

  ast: nil,             // current AST (typically a Block node after parse)
  nextNodeId: 1,        // allocator cursor for passes that allocate nodes

  // Pass outputs so far:
  // state.passes[name] == PassResult
  passes: .{},
}
```

### Context (shared environment)

```lx
let ctx = .{
  // driver-owned import/runtime concerns:
  importCache: driver.cache,           // for resolve/import
  compileModule: driver.compileModule, // recursive import callback

  // toggles:
  opts: .{
    withAnf: true,
    withAnfInline: true,
    withTypecheck: true,
  },

  // optional debugging knobs:
  debug: .{
    // stopAfter: "resolve",
  },
}
```

### Pass result (standard keys + pass-specific payload)

Minimum contract:

```lx
// Required keys
.{
  success: true or false,
  errors: [] or ["..."] or [{...}],

  // Optional standard keys that the runner threads:
  ast: <ast> or nil,
  nextNodeId: <number> or nil,
  origin: .{ [newId]: oldId } or nil,
}
```

Pass-specific keys are allowed (and expected). Example: `resolve` can keep its current `resolvedNames`, `scopeInfo`, etc. These are stored under `result.passes.resolve.*`.

### Module compilation result (pipeline output)

```lx
.{
  status: "done" or "failed",
  success: true/false,
  path: path,
  failedPass: "resolve" or nil,

  passOrder: ["parse","lower","anf","resolve","anf-inline","typecheck"],
  passes: .{ [passName]: passResult },
}
```

**Note:** After Phase 4, legacy projection fields (parseResult, lowerResult, anfResult, etc.) have been removed. All consumers now use `result.passes.*` directly.

---

## Runner Semantics

### Threading rules

After each pass:

- If `passResult.ast` is present → `state.ast = passResult.ast`
- If `passResult.nextNodeId` is present → `state.nextNodeId = passResult.nextNodeId`
- Store: `state.passes[pass.name] = passResult`

### Error tagging rule (boundary normalization)

To enable consolidated, pass-attributed diagnostics without rewriting every pass immediately:

- If an error is a map/object and missing `pass`, add `err.pass = passName`.
- Allow string errors (parser currently emits formatted strings). Strings remain strings.

### Stop condition

Default behavior:

- Pipeline stops at the first pass where `!passResult.success` and the pass is
  `fatal` (default).
- Pipeline continues past failed passes marked `fatal: false`.

In all cases, the pipeline result includes pass outputs up to (and including)
the last executed pass.

Rationale:

- Lx should avoid exposing arbitrary per-pass toggles as a public API; instead,
  it should offer a small set of curated pipelines (profiles).
- `fatal: false` exists to support explicitly best-effort passes used for
  tooling/IDE experiences.

---

## Proposed Code Skeletons

### `lx/src/passes/pipeline.lx` (new)

Implement:

- `runPasses(passes, ctx, initialState) -> pipelineResult`
- `normalizeErrors(passName, errors) -> normalizedErrors`

Skeleton:

```lx
fn normalizeErrors(passName, errors) {
  if !errors { return [] }

  // Ensure array
  let out = type(errors) == "array" and errors or [errors]

  // Tag objects with pass name
  for let i = 0; i < len(out); i = i + 1 {
    let e = out[i]
    if type(e) == "map" and !e.pass {
      e.pass = passName
    }
  }

  out
}

fn runPasses(passList, ctx, state) {
  let passesOut = .{}
  let passOrder = []
  let failedPass = nil

  // Ensure state.passes exists
  if !state.passes { state.passes = .{} }

  for let i = 0; i < len(passList); i = i + 1 {
    let p = passList[i]
    if !p or !p.name or !p.run { continue }

    let enabled = p.enabled and p.enabled(ctx, state) or true
    if !enabled { continue }

    push(passOrder, p.name)

    let raw = p.run(ctx, state) or .{ success: false, errors: ["Pass returned nil: " + p.name] }
    raw.errors = normalizeErrors(p.name, raw.errors)

    // Record output
    passesOut[p.name] = raw
    state.passes[p.name] = raw

    // Thread state
    if raw.ast { state.ast = raw.ast }
    if raw.nextNodeId { state.nextNodeId = raw.nextNodeId }

    let fatal = p.fatal == nil and true or p.fatal
    if !raw.success and fatal {
      failedPass = p.name
      break
    }

    if ctx and ctx.debug and ctx.debug.stopAfter and ctx.debug.stopAfter == p.name {
      break
    }
  }

  .{
    status: failedPass and "failed" or "done",
    success: failedPass == nil,
    path: state.path,
    failedPass: failedPass,
    passOrder: passOrder,
    passes: passesOut,
    ast: state.ast,
    nextNodeId: state.nextNodeId,
  }
}

.{
  runPasses: runPasses,
  normalizeErrors: normalizeErrors,
}
```

### Driver integration (`lx/src/driver.lx`)

Implement passes as adapters around existing pass functions (no pass refactors required initially):

- `parse`: call `parse(state.source, state.path)`; thread `ast`, `nextNodeId`
- `lower`: call `lower(state.ast, .{ startNodeId: state.nextNodeId })`
- `anf` (gated): call `anf(state.ast, .{ startNodeId: state.nextNodeId })`
- `resolve`: call `resolve(state.ast, .{ importCache: ctx.importCache, compileModule: ctx.compileModule })`
- `anf-inline` (gated): call `anfInline(state.ast, state.passes.resolve)`
- `typecheck` (gated): call `typecheck(state.ast, state.passes.resolve, .{})` (or current signature)

Then:

- Return the pipeline output plus legacy projection fields.
- Keep `driver.cache[path] = {status:"compiling"}` sentinel for circular imports before running passes.

### Pass-aware origin folding in `lx/src/errors.lx`

Replace hard-coded origin walking with generic fold across `result.passOrder`:

```lx
fn foldOrigins(nodeId, result) {
  if !nodeId or !result { return nil }

  let currentId = nodeId
  let maxDepth = 100

  let order = result.passOrder or []
  // Walk from last executed pass backwards
  for let oi = len(order) - 1; oi >= 0; oi = oi - 1 {
    let name = order[oi]
    let pr = (result.passes and result.passes[name]) or nil
    let origin = pr and pr.origin
    if !origin { continue }

    let depth = 0
    for depth < maxDepth and origin[currentId] {
      currentId = origin[currentId]
      depth = depth + 1
    }
  }

  currentId
}
```

Then `resolveNodePosition(nodeId, result)` becomes:

- `let parserAst = result.passes and result.passes.parse and result.passes.parse.ast` (fallback to `result.parseResult.ast`)
- `let leafId = foldOrigins(nodeId, result)` (fallback to existing `anfResult.origin`+`lowerResult.origin` logic when `passOrder` absent)
- build nodes index on parse AST and look up `leafId`.

This is the core future-proofing: any new node-allocating pass that returns `origin` automatically participates in error position resolution.

---

## AST Mutation Standard

Default policy:

- Passes should treat input AST as immutable and return a new AST when transforming.
- If a pass must mutate for performance, it must declare `mutatesAst: true` in its descriptor and document invariants.
- Semantic annotations should live in pass outputs (“artifacts/side tables”), not on AST nodes.

### Known current “leak” to fix during migration

- `resolve.lx` attaches `importType/importBindings/importResult` to AST nodes and downstream copies them in `lower.lx`/`anf.lx`.
- `typecheck.lx` reads `node.importType` (see `checkImport()`).

Migration strategy:

1. Add `resolve` artifact: `resolveResult.importInfoByNodeId[nodeId] = .{ importType, importBindings, importResult }`.
2. Update `typecheck.lx` to consult `resolveResult.importInfoByNodeId[node.id]` first.
3. Stop mutating the AST in `resolve.lx` for imports.
4. Remove `importResult` copy-forward in `lower.lx`/`anf.lx` after all consumers are updated.

---

## Migration Plan (Step-by-step)

### Phase 1: Infrastructure (no behavior changes)

1. Add `lx/src/passes/pipeline.lx` with `runPasses()` + `normalizeErrors()`.
2. Update `lx/src/driver.lx` to use the runner internally:
   - Build pass list with gates from `withAnf`, `withAnfInline`, `withTypecheck`.
   - Keep legacy projection fields in the returned result (to avoid breaking tests and tools).
3. Update `lx/src/errors.lx`:
   - Teach `collectErrors(result)` to prefer `result.passOrder/result.passes` when present.
   - Teach `resolveNodePosition()` to fold origins across pass order.
4. Run the existing test suite (should remain green).

### Phase 2: Remove import AST annotations (re-commit to side tables)

5. Update `resolve.lx` to store import metadata in a side table keyed by import-node id:
   - e.g. `resolver.importInfoByNodeId[node.id] = .{ ... }`
   - return it on `resolveResult`.
6. Update `typecheck.lx` to read import type info from `resolveResult` (not `node.importType`).
7. Delete `node.importType/importBindings/importResult` writes in `resolve.lx`.
8. Delete `importResult` copy-forward code in `lower.lx`/`anf.lx` once no longer needed.
9. Run tests.

### Phase 3: Migrate call sites and tests to `result.passes`

10. Update high-level consumers (`lx/main.lx`, `lx/scripts/build-*.lx`, `lx/services/query.lx`) to read from `result.passes.*`.
11. Update tests gradually (many refer to legacy `lowerResult/anfResult/...`).
12. Optionally: keep legacy aliases indefinitely or mark as deprecated in docs.

### Phase 4: Remove legacy surface (completed)

13. Migrate remaining uses of `parseErrors`/`lowerErrors` to `passes.parse.errors`/`passes.lower.errors`.
14. Remove legacy projection layer from `driver.lx`:
    - Delete all backward-compat projection fields (parseResult, lowerResult, anfResult, etc.)
    - Delete legacy error fields (parseErrors, lowerErrors)
    - Remove anfResult.ast overwrite hack
15. Remove backward-compat fallbacks from `errors.lx`:
    - Remove legacy origin walking from `foldOrigins()`
    - Remove parseResult.ast fallback from `resolveNodePosition()`
    - Remove legacy field collection from `collectErrors()`
16. Fix source load failure to return proper failed parse pass with error message.
17. Run tests.

### Phase 5: Pass manager hardening and curated profiles (completed)

18. Add `requires`/`provides` metadata to all passes in `driver.lx`:
    - Parse provides: `["ast", "parseTree"]`
    - Lower requires: `["ast"]`, provides: `["loweredAst"]`
    - ANF requires: `["loweredAst"]`, provides: `["anfAst", "loweredAst"]` (ANF is a refinement of lowered)
    - Resolve requires: `["loweredAst"]`, provides: `["resolution", "scopeInfo"]`
    - ANF-inline requires: `["anfAst", "resolution"]`, provides: `["optimizedAst"]`
    - Typecheck requires: `["resolution"]`, provides: `["typeInfo"]`

19. Define curated profiles in `driver.lx`:
    - `default`: parse → lower → anf → resolve → anf-inline → typecheck (current semantics)
    - `tooling`: Same as default (best AST/tables for IDE/LSP)
    - `O0`: parse → lower → anf → resolve → typecheck (skip anf-inline optimization)

20. Update `driver.make()` to support profile parameter:
    - Profile sets defaults: `make(.{ profile: "O0" })`
    - Explicit flags override profile: `make(.{ profile: "O0", withTypecheck: false })`
    - Default profile is `"default"` if not specified

21. Add dependency validation to `pipeline.lx`:
    - `validatePassDependencies()` checks requires/provides consistency
    - Always runs before pipeline execution (fail-fast on invalid configurations)
    - Reports clear error messages for missing requirements
    - Returns validation errors as synthetic "validate" pass for proper error collection

22. Add optional mutation checking behind `ctx.debug.validatePasses`:
    - Lightweight pointer identity check for `mutatesAst: false` passes
    - Does not catch all mutations (no deep equality), but catches obvious violations
    - Only runs when debug flag is enabled

23. Run tests and verify all profiles work correctly.

---

## Definition of Done

- Driver uses `runPasses()` with a pass list descriptor; adding a pass does not require bespoke wiring.
- `errors.resolveNodePosition()` automatically handles any future pass that returns `origin`.
- `errors.collectErrors()` collects from pass results in order.
- Import/type metadata no longer lives on AST nodes; it lives in resolve artifacts and flows through results.
- Pass dependencies are validated at pipeline build time via `requires`/`provides` metadata.
- Curated profiles provide standard compilation modes (default, tooling, O0).
- Profile flags can be overridden individually for flexibility.
- Optional debug validation catches mutation violations in non-mutating passes.
- Existing test suite passes.
