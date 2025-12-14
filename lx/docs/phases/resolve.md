# Resolve Phase (resolve.lx)

## Responsibility

Name resolution, semantic validation, and side table construction.

**Input**: Lowered AST (canonical, no Arrow nodes)
**Output**: Side tables with binding info and semantic errors

## API

```lx
fn resolve(ast, opts) → {
  success: bool,
  ast: ProgramNode,  // Unchanged (no mutation)
  nodes: .{ nodeId → node },  // O(1) node lookup map
  resolvedNames: .{ nodeId → BindingInfo },
  scopeInfo: .{ nodeId → ScopeInfo },
  errors: [{ nodeId, message, severity }],
}

// opts:
.{
  importCache: .{},              // Driver-owned cache
  compileModule: fn(path) → result,  // Callback to compile imports
}
```

## Side Tables

### resolvedNames[nodeId]

For Identifier nodes:

```lx
.{
  kind: "local" | "upvalue" | "global",

  // Opcodes (resolver decides!)
  getOp: OP.GET_LOCAL | OP.GET_UPVALUE | OP.GET_GLOBAL,
  setOp: OP.SET_LOCAL | OP.SET_UPVALUE | OP.SET_GLOBAL,

  // Indices
  slot: number,            // Local slot (stable, monotonic)
  upvalueIndex: number,    // Upvalue index
  globalConst: number,     // Global constant pool index

  // Metadata
  declaredAt: nodeId,      // Declaration node
  isCaptured: bool,        // Captured by closure?
  depth: number,           // Scope depth
}
```

### scopeInfo[nodeId]

For Block, Function, For nodes:

```lx
.{
  scopeType: "global" | "function" | "block" | "loop",
  depth: number,

  // CRITICAL: Declaration order preserved!
  locals: [
    .{
      name: string,
      slot: number,      // Stable slot ID
      kind: "var" | "fn" | "param",
      depth: number,     // -1 during init, depth after
      isCaptured: bool,
    }
  ],

  // Upvalues (functions only)
  upvalues: [
    .{
      index: number,
      isLocal: bool,
      slot: number,
    }
  ],

  // Hoisting
  hoistedFns: [nodeId],
  lastHoistedFunctionIndex: number,

  nextSlot: number,  // Monotonic counter
}
```

### nodes[nodeId]

Map from node ID to AST node for O(1) lookup:

```lx
.{
  1: { id: 1, type: "Number", value: 42, ... },
  2: { id: 2, type: "Identifier", name: "x", ... },
  // ... all nodes
}
```

Built during resolve via tree walk.

## Scope Structures

```lx
fn Scope(enclosing, type) {.{
  enclosing: enclosing,
  type: type,
  depth: enclosing and enclosing.depth + 1 or 0,

  localsByName: .{},       // name → Local
  localsArray: [],         // [Local] - preserve order!
  hoistedFns: .{},         // name → HoistedFunction
  lastHoistedFunctionIndex: -1,
  upvalues: [],
  nextSlot: 0,
}}

fn Local(name, depth, kind, slot, nodeId) {.{
  name: name,
  depth: depth,  // -1 = uninitialized
  kind: kind,
  slot: slot,
  nodeId: nodeId,
  isCaptured: false,
}}

fn Upvalue(index, isLocal, slot) {.{
  index: index,
  isLocal: isLocal,
  slot: slot,
}}

fn HoistedFunction(declNodeId, declIndex, slot) {.{
  declNodeId: declNodeId,
  declIndex: declIndex,  // Position in block
  slot: slot,
}}
```

## Key Functions

### Scope Management

- `beginScope(resolver, type)` - Push new scope
- `endScope(resolver)` - Pop scope, record scopeInfo
- `getCurrentScope(resolver)` - Get current scope

### Name Resolution

- `resolveProgram(resolver, ast)` - Entry point
- `resolveExpr(resolver, node, context)` - Dispatch by type
- `resolveBlock(resolver, node)` - Two-phase with hoisting
- `resolveIdentifier(resolver, node, context)` - Name lookup + ordering
- `resolveName(resolver, name, scope, context)` - Walk scope chain

### Local Declaration

- `declareLocal(resolver, name, kind)` - Add local, return slot
- `markInitialized(resolver, local)` - Set depth to current
- `checkDuplicate(resolver, name, nodeId)` - Duplicate check

### Upvalue Handling

- `resolveUpvalue(resolver, scope, name)` - Capture from enclosing
- `addUpvalue(resolver, scope, isLocal, slot)` - Add upvalue entry
- `markCaptured(resolver, local)` - Set isCaptured flag

### Hoisting

See `docs/algorithms.md` for full algorithm.

- Two-phase `resolveBlock()`:
  1. Prescan: build hoistedFns, track lastHoistedFunctionIndex
  2. Resolve: enforce ordering (exprIndex >= lastHoistedFunctionIndex)

### Import Handling

```lx
fn resolveImport(resolver, node) {
  let path = resolvePath(node.path.value)

  let cached = resolver.importCache[path]
  if cached {
    if cached.status != "done" and cached.status != "failed" {
      addError(resolver, node.id, "Circular import: " + path)
      return
    }
  }

  // Compile via callback
  resolver.compileModule(path)
}
```

## Semantic Validation

### Required Checks

**Undefined variables**:
```lx
if !resolved {
  addError(resolver, node.id, "Undefined variable '" + name + "'")
}
```

**Duplicate declarations**:
```lx
if scope.localsByName[name] {
  addError(resolver, nodeId, "Variable '" + name + "' already declared")
}
```

**Read-before-initialization**:
```lx
if local.depth == -1 {
  addError(resolver, nodeId, "Can't read local variable in its own initializer")
}
```

**Return placement**:
```lx
fn validateReturn(resolver, node) {
  if !isInFunction(scope) {
    if !isAtEndOfFile(node) {
      addError(resolver, node.id, "Can only return at end of file")
    }
  } else {
    if !isAtEndOfBlock(node) {
      addError(resolver, node.id, "Can only return at end of block")
    }
  }
}
```

**Break/continue placement**:
```lx
fn validateBreak(resolver, node) {
  if !isInLoop(getCurrentScope(resolver)) {
    addError(resolver, node.id, "Can only break inside a loop")
  }
}
```

### Helper Functions

```lx
fn isInLoop(scope) {
  let current = scope
  while current {
    if current.type == "loop" { return true }
    current = current.enclosing
  }
  return false
}

fn isInFunction(scope) {
  let current = scope
  while current {
    if current.type == "function" { return true }
    current = current.enclosing
  }
  return false
}
```

## Error Collection

```lx
fn addError(resolver, nodeId, message, severity) {
  push(resolver.errors, .{
    nodeId: nodeId,
    message: message,
    severity: severity or "error",
  })
  if severity != "warning" {
    resolver.hadError = true
  }
}
```

## Testing

- ✅ Scope nesting works
- ✅ Local resolution correct
- ✅ Upvalue resolution correct
- ✅ Global fallback works
- ✅ Hoisting: mutual recursion allowed
- ✅ Hoisting: block-level ordering enforced
- ✅ All semantic errors caught
- ✅ Import caching works
- ✅ Circular imports detected
- ✅ Side tables correct
- ✅ Nodes map complete
