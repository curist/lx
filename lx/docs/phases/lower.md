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

## Missing Else Normalization

**Purpose**: Make "everything is expression" uniform for typecheck/LSP

Parser allows `if cond { then }` with no else branch. Lower normalizes this:

```lx
fn lowerIf(lowerer, node) {
  let condition = lowerExpr(lowerer, node.condition)
  let thenBranch = lowerExpr(lowerer, node.thenBranch)

  let elseBranch =
    if node.elseBranch {
      lowerExpr(lowerer, node.elseBranch)
    } else {
      // Missing else → explicit nil block
      createNilBlock(lowerer, node.endLine, node.endCol)
    }

  let newIf = copyNode(node)
  newIf.id = lowerer.nextId
  lowerer.nextId = lowerer.nextId + 1
  newIf.condition = condition
  newIf.thenBranch = thenBranch
  newIf.elseBranch = elseBranch  // Always present after lowering

  // Copy position
  newIf.line = node.line
  newIf.col = node.col
  newIf.endLine = node.endLine
  newIf.endCol = node.endCol

  lowerer.origin[newIf.id] = node.id

  return newIf
}

fn createNilBlock(lowerer, line, col) {
  let nilLiteral = NilNode()
  nilLiteral.id = lowerer.nextId
  lowerer.nextId = lowerer.nextId + 1
  nilLiteral.line = line
  nilLiteral.col = col
  nilLiteral.endLine = line
  nilLiteral.endCol = col

  let block = BlockNode([nilLiteral])
  block.id = lowerer.nextId
  lowerer.nextId = lowerer.nextId + 1
  block.line = line
  block.col = col
  block.endLine = line
  block.endCol = col

  return block
}
```

**Why this helps**:
- Typecheck sees uniform if-expression shape: always has both branches
- Type of `if` is `join(thenType, elseType)` - no special case needed
- LSP can analyze both branches consistently
- Codegen doesn't need nil-branch logic

## Future Transformations

Lower phase can handle other desugarings:
- Spread operators
- Optional chaining
- Destructuring
- Template strings
- For-loop → while-loop normalization

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
