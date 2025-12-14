# Parser Phase (parser.lx)

## Responsibility

Convert source text into AST with accurate position information.

**Input**: Source string, filename
**Output**: AST with node IDs, syntax errors only

## API

```lx
fn parse(src, filename) → {
  success: bool,
  ast: ProgramNode,
  errors: [string],        // Syntax error messages
  nextNodeId: number,      // For lowering phase to continue
}
```

## Implementation Tasks

### 1. Add Node ID Counter

```lx
fn parse(src, filename) {
  let scanner = initScanner(src)

  // Add node ID counter
  let nodeIdCounter = 0
  fn nextNodeId() {
    nodeIdCounter = nodeIdCounter + 1
    nodeIdCounter
  }

  // ... rest of parser

  return .{
    success: !parser.hadError,
    ast: ProgramNode(filename, body),
    errors: parser.errors,
    nextNodeId: nodeIdCounter + 1,  // ← NEW
  }
}
```

### 2. Modify Node Constructor

```lx
fn Node(type, filename, token) {
  let tokenLen = len(token.lexeme)
  let startCol = token.col - tokenLen
  if startCol < 0 { startCol = 0 }

  .{
    id: nextNodeId(),    // ← NEW: auto-increment ID
    type: type,
    filename: filename,
    line: token.line,
    col: startCol,
    endLine: token.line,
    endCol: token.col,
  }
}
```

All node constructors automatically inherit IDs through `Node()`.

### 3. Remove Semantic Validation

#### Remove These Variables

```lx
// ❌ DELETE
let functionDepth = 0
let loopDepth = 0
```

#### Remove These Tracking Updates

```lx
// In fnExpr() - DELETE:
functionDepth = functionDepth + 1
// ...
functionDepth = functionDepth - 1

// In forExpr() - DELETE:
loopDepth = loopDepth + 1
// ...
loopDepth = loopDepth - 1
```

#### Remove These Validation Checks

```lx
// In returnStatement() - DELETE:
if functionDepth == 0 {
  if !check(TOKEN.EOF) {
    error("Can only return at end of file.")
  }
} else {
  if !check(TOKEN.RIGHT_BRACE) {
    error("Can only return at end of block.")
  }
}

// In breakExpr() - DELETE:
if loopDepth == 0 {
  error("Can only break inside a loop.")
}

// In continueExpr() - DELETE:
if loopDepth == 0 {
  error("Can only continue inside a loop.")
}
```

### 4. Keep Arrow Nodes

**Do NOT transform arrows!** Just create Arrow nodes:

```lx
fn arrowExpr() {
  let left = callExpr()

  if match(TOKEN.ARROW) {
    let right = arrowExpr()  // Right-associative
    return ArrowNode(left, right)  // ← Keep as Arrow!
  }

  return left
}
```

Lowering phase will transform them.

## What Parser Should Validate

✅ **Syntax only**:
- Unexpected tokens
- Missing delimiters (parens, braces, etc.)
- Malformed expressions
- Grammar violations

❌ **NOT semantic**:
- Variable undefined/redefined
- Return/break/continue placement
- Type errors
- Control flow validity

## Error Handling

```lx
fn error(message) {
  if !parser.panicMode {
    parser.panicMode = true
    parser.hadError = true

    let errorMsg = "[" + parser.current.filename + ":" +
                   parser.current.line + ":" +
                   parser.current.col + "] Error: " + message

    push(parser.errors, errorMsg)
    groanln(errorMsg)
  }
}
```

Errors are strings (not structured) because parser doesn't have node IDs yet for all positions.

## Node Types

All nodes get these base fields from `Node()`:
- `id` - Unique within module
- `type` - Node type string
- `filename` - Source file
- `line`, `col` - Start position
- `endLine`, `endCol` - End position

Specific node types add their own fields:
- `ProgramNode`: `body: [Expr]`
- `FunctionNode`: `name: Identifier?, params: [Identifier], body: Block`
- `ArrowNode`: `left: Expr, right: Expr`
- `CallNode`: `callee: Expr, args: [Expr]`
- `IdentifierNode`: `name: string`
- etc.

## Testing

- ✅ Node IDs unique and sequential
- ✅ All nodes have IDs (spot check different node types)
- ✅ Arrow nodes preserved (not transformed)
- ✅ Only syntax errors in error array
- ✅ No semantic validation (test return/break outside context → no error)
- ✅ Position spans accurate
