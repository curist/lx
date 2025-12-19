# Implementation Algorithms

This document explains the tricky implementation details for each phase.

## Arrow Lowering (lower.lx)

### Transformation Rule

```
x->f(a, b)  →  f(x, a, b)
```

Chained:
```
x->a()->b()->c()  →  1. a(x)->b()->c()
                      2. b(a(x))->c()
                      3. c(b(a(x)))
```

### Algorithm

```lx
fn lowerArrow(lowerer, node) {
  let left = lowerExpr(lowerer, node.left)
  let right = lowerExpr(lowerer, node.right)

  // Validate RHS is a call (lowering-time check, not grammar)
  if right.type != "Call" {
    addError(lowerer, node.id, "Arrow operator requires function call on right side")
    return left  // Error recovery: just return left side
  }

  // Rewrite: x->f(a, b) becomes f(x, a, b)
  let newCall = copyNode(right)
  newCall.id = lowerer.nextId
  lowerer.nextId = lowerer.nextId + 1
  newCall.args = [left] + right.args

  // CRITICAL: Copy position from original arrow node
  newCall.line = node.line
  newCall.col = node.col
  newCall.endLine = right.endLine    // Ends where RHS call ends
  newCall.endCol = right.endCol

  // Track provenance: this call came from the arrow
  lowerer.origin[newCall.id] = node.id

  return newCall
}

fn lowerCall(lowerer, node) {
  // Recursively lower callee and args
  let newNode = copyNode(node)
  newNode.id = lowerer.nextId
  lowerer.nextId = lowerer.nextId + 1
  newNode.callee = lowerExpr(lowerer, node.callee)
  newNode.args = map(node.args, fn(arg) { lowerExpr(lowerer, arg) })

  // Copy position spans from original node
  newNode.line = node.line
  newNode.col = node.col
  newNode.endLine = node.endLine
  newNode.endCol = node.endCol

  // Track provenance: maps to original call
  lowerer.origin[newNode.id] = node.id

  return newNode
}
```

### Key Points

- New IDs continue from `parseResult.nextNodeId` (same module ID space)
- Position spans copied from original → errors show correct location
- `origin` map for LSP/tooling (not used for diagnostics)
- Error recovery: return left operand on invalid arrow

## Hoisting Prescan (resolve.lx)

### Two-Phase Block Resolution

#### Phase 1: Prescan for Hoisted Functions

```lx
fn resolveBlock(resolver, blockNode) {
  beginScope(resolver, "block")

  // STEP 1: Pre-scan for hoisted functions
  let scope = getCurrentScope(resolver)
  let lastHoistedIndex = -1  // Track last hoisted function position

  for let i = 0; i < len(blockNode.expressions); i = i + 1 {
    let expr = blockNode.expressions[i]
    if expr.type == "Function" and expr.name {
      // Named function declaration - hoist it
      let fnName = expr.name.name
      let slot = declareLocal(resolver, fnName, "fn")

      scope.hoistedFns[fnName] = HoistedFunction(
        expr.id,    // declNodeId
        i,          // declIndex (position in block)
        slot        // slot assigned
      )

      lastHoistedIndex = i  // Update last hoisted position
    }
  }

  // Store for safety check
  scope.lastHoistedFunctionIndex = lastHoistedIndex

  // STEP 2: Resolve expressions in order
  for let i = 0; i < len(blockNode.expressions); i = i + 1 {
    let expr = blockNode.expressions[i]

    let context = .{
      exprIndex: i,          // Current position
      inFunctionBody: false, // Block-level expression
    }

    resolveExpr(resolver, expr, context)
  }

  endScope(resolver)
}
```

#### Phase 2: Resolve with Ordering Check

```lx
fn resolveIdentifier(resolver, node, context) {
  let name = node.name
  let scope = getCurrentScope(resolver)

  // Check if this is a hoisted function in current block
  let hoisted = scope.hoistedFns[name]
  if hoisted {
    // Found a hoisted function!

    // STRICTER RULE: Block-level calls to hoisted functions require
    // exprIndex >= lastHoistedFunctionIndex (entire mutual recursion group initialized)
    if !context.inFunctionBody {
      if context.exprIndex < scope.lastHoistedFunctionIndex {
        addError(resolver, node.id,
          "Cannot use hoisted function '" + name + "' before all hoisted functions are declared")
        return
      }
    }

    // Valid usage - record resolution
    resolver.resolvedNames[node.id] = .{
      kind: "local",
      getOp: OP.GET_LOCAL,
      setOp: OP.SET_LOCAL,
      slot: hoisted.slot,
      declaredAt: hoisted.declNodeId,
      depth: scope.depth,
    }
    return
  }

  // Not hoisted - continue with normal resolution
  resolveName(resolver, name, scope, context)
}
```

### Function Body Context

```lx
fn resolveFunction(resolver, node, context) {
  // ... setup function scope

  // Resolve function body with inFunctionBody = true
  let bodyContext = .{
    exprIndex: 0,
    inFunctionBody: true,  // ← Key flag! Disables ordering check
  }

  resolveBlock(resolver, node.body, bodyContext)

  // ... cleanup
}
```

### Why the Stricter Rule Works

**Runtime execution**:
1. Prescan creates slots for all hoisted functions (uninitialized)
2. Block executes in order:
   - `fn a(){}` executes → slot `a` populated with closure
   - `fn b(){}` executes → slot `b` populated with closure
   - `a()` call executes → both slots ready
3. Inside `a`'s body, `b()` reference is safe because call happens AFTER both declarations

**Stricter rule prevents**:
```lx
{
  fn a() { b() }  // Body can reference b (hoisted)
  a()             // ❌ Error: before lastHoistedFunctionIndex
  fn b() { a() }  // Last hoisted at index 2
}
```

If allowed, `a()` would execute before `b`'s slot is initialized → runtime crash.

## Import Cache Lifecycle

### Driver Flow

```lx
fn compileModule(path, importCache, opts) {
  let absPath = resolvePath(path)  // ← Canonical path

  // Check cache + circular detection
  let cached = importCache[absPath]
  if cached {
    if cached.status == "done" {
      return .{ success: true, function: cached.function }
    } else if cached.status == "failed" {
      return .{ success: false, errors: cached.errors }
    } else {
      // In-progress = circular import
      return .{
        success: false,
        errors: ["Circular import: " + absPath + " (status: " + cached.status + ")"]
      }
    }
  }

  // Get source
  let src = opts.source or slurp(absPath)
  if !src {
    return .{ success: false, errors: ["Failed to read: " + absPath] }
  }

  // Initialize cache entry
  importCache[absPath] = .{ status: "parsing" }

  // Phase 1: Parse
  let parseResult = parse(src, absPath)
  importCache[absPath].parseResult = parseResult
  if !parseResult.success {
    importCache[absPath].status = "failed"
    importCache[absPath].errors = parseResult.errors
    return .{ success: false, errors: parseResult.errors }
  }

  // Phase 2: Lower
  importCache[absPath].status = "lowering"
  let lowerResult = lower(parseResult.ast, .{
    startNodeId: parseResult.nextNodeId
  })
  importCache[absPath].lowerResult = lowerResult
  if !lowerResult.success {
    importCache[absPath].status = "failed"
    importCache[absPath].errors = lowerResult.errors
    return .{ success: false, errors: lowerResult.errors }
  }

  // Phase 3: Resolve (pass compileModule callback)
  importCache[absPath].status = "resolving"
  let resolveResult = resolve(lowerResult.ast, .{
    importCache: importCache,
    compileModule: fn(p) { compileModule(p, importCache, .{}) }  // ← Callback
  })
  importCache[absPath].resolveResult = resolveResult
  if !resolveResult.success {
    importCache[absPath].status = "failed"
    importCache[absPath].errors = resolveResult.errors
    return .{ success: false, errors: resolveResult.errors }
  }

  // Phase 4: Codegen
  importCache[absPath].status = "codegen"
  let codegenResult = codegen(lowerResult.ast, resolveResult, .{})
  importCache[absPath].function = codegenResult.function
  importCache[absPath].status = "done"

  return .{ success: true, function: codegenResult.function }
}
```

### Circular Import Detection

No separate import stack needed! Just check status:

```lx
fn resolveImport(resolver, node) {
  let path = resolvePath(node.path.value)

  let cached = resolver.importCache[path]
  if cached {
    if cached.status != "done" and cached.status != "failed" {
      // Module is still being compiled = circular import
      addError(resolver, node.id,
        "Circular import: " + path + " (currently " + cached.status + ")")
      return
    }
    // Use cached.function
    return
  }

  // Not in cache - compile it via callback
  resolver.compileModule(path)
}
```

### Cache Structure

```lx
importCache[absolutePath] = .{
  status: "parsing" | "lowering" | "resolving" | "codegen" | "done" | "failed",
  parseResult: { ast, errors, nextNodeId },
  lowerResult: { ast, origin, errors, nextNodeId },
  resolveResult: { resolvedNames, scopeInfo, nodes, errors },
  function: compiledFunction,  // Only set when status == "done"
  errors: [],                  // If status == "failed"
}
```

## Error Propagation Flow

### Phase-by-Phase Collection

```lx
// 1. Parse (syntax)
let parseResult = parse(src, filename)
if !parseResult.success {
  reportErrors(parseResult.errors, nil)  // String array, no nodes needed
  return
}

// 2. Lower (desugar)
let lowerResult = lower(parseResult.ast, .{ startNodeId: parseResult.nextNodeId })
if !lowerResult.success {
  // Build node map from lowered AST for error reporting
  let lowerNodes = buildNodeMap(lowerResult.ast)
  reportErrors(lowerResult.errors, lowerNodes)
  return
}

// 3. Resolve (semantics)
let resolveResult = resolve(lowerResult.ast, opts)
if !resolveResult.success {
  // resolveResult.nodes already built during resolve
  reportErrors(resolveResult.errors, resolveResult.nodes)
  return
}

// 4. Codegen (no errors expected, just bugs)
let codegenResult = codegen(lowerResult.ast, resolveResult, opts)
return codegenResult.function
```

### Error Reporting

```lx
fn reportErrors(errors, nodes) {
  // Sort by position for consistent output
  let sorted = sortErrors(errors, nodes)

  each(sorted, fn(err) {
    if typeof(err) == "string" {
      // Parser error (no node)
      groanln(err)
    } else {
      // Structured error with nodeId
      let node = nodes[err.nodeId]  // O(1) lookup
      let prefix = if err.severity == "warning" { "Warning" } else { "Error" }
      groanln(join([
        "[", node.filename, ":", node.line, ":", node.col, "]",
        " ", prefix, ": ", err.message
      ], ""))
    }
  })
}

fn addError(resolver, nodeId, message, severity) {
  push(resolver.errors, .{
    nodeId: nodeId,
    message: message,
    severity: severity or "error",
  })
  if severity != "warning" and severity != "info" {
    resolver.hadError = true
  }
}
```

### Error Format

```lx
// Structured error (lower, resolve)
.{
  nodeId: 42,
  message: "Undefined variable 'x'",
  severity: "error",  // "error" | "warning" | "info"
}

// Parser error (just string)
"[foo.lx:10:5] Error: Expected ')' after arguments"
```

## Scope Cleanup (codegen.lx)

### Reverse-Order Emission

```lx
fn endScope(gen) {
  let scope = gen.scopeInfo[gen.currentScopeNodeId]

  // CRITICAL: Emit cleanup in REVERSE declaration order
  for let i = len(scope.locals) - 1; i >= 0; i = i - 1 {
    let local = scope.locals[i]

    if local.isCaptured {
      // Captured by nested function → close upvalue
      emitByte(OP.CLOSE_UPVALUE, gen.currentLine)
    } else {
      // Not captured → just pop
      emitByte(OP.POP_LOCAL, gen.currentLine)
    }
  }
}
```

### Why Reverse Order Matters

Locals are allocated in stack order:
```
[slot 0] [slot 1] [slot 2] [slot 3]
   a        b        c        d
```

Must pop in reverse:
```
POP d  →  [a, b, c]
POP c  →  [a, b]
POP b  →  [a]
POP a  →  []
```

If you pop forward (a, b, c, d), stack gets corrupted!

### isCaptured Flag

Set during resolution when upvalue is created:

```lx
fn resolveUpvalue(resolver, scope, name) {
  // ... find local in enclosing scope

  // Mark local as captured
  local.isCaptured = true

  // ... create upvalue entry
}
```

Codegen reads this flag to decide POP_LOCAL vs CLOSE_UPVALUE.

## Node Map Building

### O(1) Lookup for Errors and LSP

```lx
fn buildNodeMap(resolver, node) {
  if !node { return }

  // Register this node
  if node.id {
    resolver.nodes[node.id] = node
  }

  // Recursively walk children based on node type
  if node.type == "Block" {
    each(node.expressions, fn(child) { buildNodeMap(resolver, child) })
  } else if node.type == "Binary" or node.type == "Logical" {
    buildNodeMap(resolver, node.left)
    buildNodeMap(resolver, node.right)
  } else if node.type == "Function" {
    if node.name { buildNodeMap(resolver, node.name) }
    each(node.params, fn(p) { buildNodeMap(resolver, p) })
    buildNodeMap(resolver, node.body)
  } else if node.type == "Call" {
    buildNodeMap(resolver, node.callee)
    each(node.args, fn(arg) { buildNodeMap(resolver, arg) })
  }
  // ... etc for all node types
}
```

**Cost**: O(n) single tree walk during resolve phase
**Benefit**: O(1) node lookup for errors, LSP queries, codegen
