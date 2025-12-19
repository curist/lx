# Query Service (query.lx)

## Responsibility

Provide **position-based IDE queries** over the compiler’s existing artifacts:

* Hover: derived type at `(filename, line, col)`
* Goto definition: jump from an identifier usage to its declaration

**Query must be read-only**:

* No inference, no unification, no name resolution.
* Only table lookups + span selection.

**Input**: `ast` + `resolveResult` + `typecheckResult`
**Output**: query responses for editor tooling

---

## Inputs

### AST node spans (parser / lowered AST)

Every AST node includes:

* `id: int`
* `type: string`
* `filename: string`
* `line: int, col: int`
* `endLine: int, endCol: int`

### Resolver tables (`resolveResult`)

* `resolvedNames: { [nodeId]: ResolvedName }` for **Identifier nodes only**
* `nodes: { [nodeId]: Node }` dictionary of **all visited AST nodes**, keyed by `id`

`ResolvedName` is one of:

* local: `{ kind: "local", getOp, setOp, slot, declaredAt, depth }`
* upvalue: `{ kind: "upvalue", getOp, setOp, upvalueIndex, declaredAt, depth }`
* builtin: `{ kind: "builtin", getOp, setOp, name, depth }` (no `declaredAt`)

**declaredAt contract (as implemented):**

* vars/params/locals: `declaredAt` is the declaring **Identifier node id** (`node.name.id`)
* hoisted/named functions: `declaredAt` is the **Function node id** (`expr.id`) for references to that function
* builtins: no `declaredAt`

You can always obtain spans via:
`resolveResult.nodes[declaredAt]`

### Typechecker tables (`typecheckResult`)

* `types: { [nodeId]: Type }`
* `typeVarBindings: { [typeVarId]: Type }` (deref’d) for resolving type variables in hover payloads

---

## API

```lx
fn queryInit(ast, resolveResult, typecheckResult, opts) -> {
  success: bool,
  ctx: QueryCtx,
}

fn queryHover(ctx, filename, line, col) -> HoverResult
fn queryGotoDefinition(ctx, filename, line, col) -> GotoResult

// opts
.{
  buildIndex: bool, // default true
}
```

---

## Query Context

```lx
type QueryCtx = .{
  ast,
  nodes,         // resolveResult.nodes
  resolvedNames, // resolveResult.resolvedNames
  types,         // typecheckResult.types
  typeVarBindings, // typecheckResult.typeVarBindings

  // optional acceleration:
  fileNodeIds,   // { [filename]: [nodeId...] }
}
```

### Initialization

```lx
fn queryInit(ast, resolveResult, typecheckResult, opts) {
  let ctx = .{
    ast: ast,
    nodes: resolveResult.nodes,
    resolvedNames: resolveResult.resolvedNames,
    types: typecheckResult.types,
    fileNodeIds: .{},
  }

  let buildIndex = !(opts and opts.buildIndex == false)
  if buildIndex { buildFileIndex(ctx) }

  return .{ success: true, ctx: ctx }
}
```

---

## Node Selection by Position

### Containment

```lx
fn contains(n, line, col) {
  if line < n.line or line > n.endLine { return false }
  if line == n.line and col < n.col { return false }
  if line == n.endLine and col > n.endCol { return false }
  return true
}
```

### Smallest-span wins

```lx
fn spanScore(n) {
  (n.endLine - n.line) * 100000 + (n.endCol - n.col)
}

fn nodeAtPos(ctx, filename, line, col) {
  let ids = ctx.fileNodeIds[filename] or keys(ctx.nodes)

  let best = nil
  let bestScore = nil

  each(ids, fn(id) {
    let n = ctx.nodes[id]
    if !n or n.filename != filename { return }
    if !contains(n, line, col) { return }

    let s = spanScore(n)
    if !best or s < bestScore {
      best = n
      bestScore = s
    }
  })

  return best
}
```

---

## Hover

### Behavior

* Select node at position.
* Build hover payload with `type: formatTypeWithBindings(ctx, types[node.id])` (or `"Unknown"`) and, for identifiers, attach symbol info: resolution kind, name, and declaration span/type when available.
  * `formatTypeWithBindings` follows `typeVarBindings` to display the bound type where possible; unbound vars show as `Unbound T<n>` even when nested inside functions/records.
  * When hovering a record literal key (String node), the hover type is taken from the corresponding field value instead of the String key literal.
* If node is Identifier, optionally include `resolvedNames[node.id]` in `details` for debugging.

```lx
fn queryHover(ctx, filename, line, col) {
  let n = nodeAtPos(ctx, filename, line, col)
  if !n { return .{ success: false, kind: "hover", message: "No node at position" } }

  let ty = ctx.types[n.id]
  let contents = buildHoverContents(ctx, n, ty)

  let details = .{ nodeType: n.type, nodeId: n.id }

  if n.type == "Identifier" {
    let r = ctx.resolvedNames[n.id]
    if r { details.resolved = r }
  }

  return .{
    success: true,
    kind: "hover",
    range: rangeOf(n),
    contents: contents,
    details: details,
  }
}
```

`buildHoverContents` returns `.{
  type: string,
  symbol?: .{ name, kind, declaration?, declarationType? },
}` so editor UIs can surface more than just the raw type string.

---

## Goto Definition

### High-level behavior

* Only triggers on Identifier nodes.
* Uses `resolvedNames[id]`.
* If `kind == "builtin"`: return “no definition” (builtins are external/untracked).
* Otherwise, use `declaredAt` and look up the node in `ctx.nodes`.
* **Normalize** the target location for function declarations so goto lands on the function name token where possible.

### Definition target normalization

Because named/hoisted functions use `declaredAt = FunctionNodeId`, the raw span may cover the entire function expression. For editor UX, normalize as follows:

```lx
fn normalizeDeclTarget(ctx, declNodeId) {
  let decl = ctx.nodes[declNodeId]
  if !decl { return nil }

  // If resolver stored the Function node id for named functions,
  // prefer jumping to the name identifier span when available.
  if decl.type == "Function" and decl.name and decl.name.id {
    let nameNode = ctx.nodes[decl.name.id]
    if nameNode { return nameNode }
  }

  // Otherwise use the node itself (Identifier bindings already land perfectly).
  return decl
}
```

### Goto implementation

```lx
fn queryGotoDefinition(ctx, filename, line, col) {
  let n = nodeAtPos(ctx, filename, line, col)
  if !n { return .{ success: false, kind: "goto", message: "No node at position" } }

  if n.type != "Identifier" {
    return .{ success: false, kind: "goto", message: "Not an identifier" }
  }

  let r = ctx.resolvedNames[n.id]
  if !r {
    return .{ success: false, kind: "goto", message: "Unresolved identifier" }
  }

  if r.kind == "builtin" {
    return .{ success: false, kind: "goto", message: "Builtin has no tracked definition" }
  }

  let declNodeId = r.declaredAt
  if !declNodeId {
    return .{ success: false, kind: "goto", message: "Missing declaredAt (compiler bug)" }
  }

  let targetNode = normalizeDeclTarget(ctx, declNodeId)
  if !targetNode {
    return .{ success: false, kind: "goto", message: "Declaration node missing (compiler bug)" }
  }

  return .{
    success: true,
    kind: "goto",
    target: rangeOf(targetNode),
  }
}
```

---

## Helpers

```lx
fn rangeOf(n) {
  .{
    filename: n.filename,
    line: n.line, col: n.col,
    endLine: n.endLine, endCol: n.endCol,
  }
}
```

`formatType(ty)` is a pure pretty-printer for your type representation.

---

## Testing

Create `lx/test/query.test.lx`.

Minimum tests:

1. **Goto local**

   * `let x = 1; x + 1`
   * goto on usage `x` lands on `let x` identifier span

2. **Goto param**

   * `fn f(a) { a }`
   * goto on `a` in body lands on param identifier span

3. **Goto named function (normalized)**

   * `fn foo() { 1 } foo()`
   * resolver for `foo` references uses `declaredAt = FunctionNodeId`
   * query normalizes to `decl.name.id` span (identifier token), not whole function span

4. **Global**

   * `println(x)` where `x` is unresolved/builtin (or `print` itself if you treat builtins as builtins)
   * returns “Global has no tracked definition”

5. **Hover uses types**

   * hover on `x` returns `Number` (or your type kind string), verifying `types[nodeId]` mapping is wired correctly

---

## Notes on Scope and Multi-file

This design is “single compilation unit” friendly. If you later compile multiple files together, the query context simply needs a combined `nodes` dictionary and a `fileNodeIds` index per filename; the algorithm stays identical.
