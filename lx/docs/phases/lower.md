# Lower Phase (lower.lx)

## Responsibility

Desugar syntax into canonical AST.

**Input**: Parsed AST with Arrow nodes
**Output**: Lowered AST with Arrow → Call transformations

## API

```lx
fn lower(ast, opts) → {
  success: bool,
  ast: ProgramNode,         // Lowered AST (no Arrow nodes)
  origin: .{ newId → oldId },  // Provenance map
  errors: [{ nodeId, message, severity }],
  nextNodeId: number,       // For next phase to continue
}

// opts:
.{
  startNodeId: number,  // Where to start fresh IDs (from parseResult.nextNodeId)
}
```

## Lowerer State

```lx
fn lower(ast, opts) {
  let lowerer = .{
    nextId: opts.startNodeId or 1,  // Continue module ID sequence
    origin: .{},                     // newId → oldId mapping
    errors: [],                      // Lowering errors
  }

  let loweredAst = lowerProgram(lowerer, ast)

  return .{
    success: len(lowerer.errors) == 0,
    ast: loweredAst,
    origin: lowerer.origin,
    errors: lowerer.errors,
    nextNodeId: lowerer.nextId,
  }
}
```

## Main Transformations

### Arrow → Call

See `docs/algorithms.md` for full algorithm.

**Key points**:
- `x->f(a)` becomes `f(x, a)`
- New node gets fresh ID (from `lowerer.nextId`)
- Position span copied from original node
- `origin[newId] = oldId` for provenance

### Error Handling

```lx
fn lowerArrow(lowerer, node) {
  let left = lowerExpr(lowerer, node.left)
  let right = lowerExpr(lowerer, node.right)

  // Validate RHS is Call
  if right.type != "Call" {
    addError(lowerer, node.id, "Arrow operator requires function call on right side")
    return left  // Error recovery: return left side
  }

  // ... transform
}
```

### Position Span Copying

**Critical**: Copy position from original node to new node!

```lx
newCall.line = node.line
newCall.col = node.col
newCall.endLine = right.endLine
newCall.endCol = right.endCol
```

This ensures error messages point to correct location.

## AST Walking

Dispatch to type-specific lowering functions:

```lx
fn lowerExpr(lowerer, node) {
  if !node { return nil }

  if node.type == "Arrow" {
    return lowerArrow(lowerer, node)
  } else if node.type == "Call" {
    return lowerCall(lowerer, node)
  } else if node.type == "Binary" {
    return lowerBinary(lowerer, node)
  } else if node.type == "Function" {
    return lowerFunction(lowerer, node)
  } else if node.type == "Block" {
    return lowerBlock(lowerer, node)
  } else if node.type == "Identifier" {
    // Leaf nodes just get new ID, keep same data
    return copyNode(lowerer, node)
  }
  // ... etc
}
```

## Node Copying

Most nodes just need recursive child lowering:

```lx
fn lowerBinary(lowerer, node) {
  let newNode = copyNode(node)
  newNode.id = lowerer.nextId
  lowerer.nextId = lowerer.nextId + 1

  newNode.left = lowerExpr(lowerer, node.left)
  newNode.right = lowerExpr(lowerer, node.right)

  // Copy position
  newNode.line = node.line
  newNode.col = node.col
  newNode.endLine = node.endLine
  newNode.endCol = node.endCol

  // Track provenance
  lowerer.origin[newNode.id] = node.id

  return newNode
}
```

## Future Transformations

Lower phase can handle other desugarings:
- Spread operators
- Optional chaining
- Destructuring
- Template strings

All follow same pattern:
1. Create new nodes with fresh IDs
2. Copy position spans
3. Track provenance
4. Return canonical AST

## Testing

- ✅ `x->f()` becomes `f(x)`
- ✅ `x->f(a, b)` becomes `f(x, a, b)`
- ✅ `x->a()->b()` chains correctly
- ✅ `x->42` produces error (not a call)
- ✅ Position spans preserved
- ✅ Node IDs continue from parse
- ✅ Origin map accurate
- ✅ No Arrow nodes in output
- ✅ Golden AST tests (before/after lowering)
