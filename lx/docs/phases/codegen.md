# Codegen Phase (codegen.lx)

## Responsibility

Mechanical bytecode emission from resolved AST + side tables.

**Input**: Lowered AST + resolveResult (side tables)
**Output**: Compiled function (bytecode)

## API

```lx
fn codegen(ast, resolveResult, opts) → {
  success: bool,
  function: Function,
}

// opts:
.{
  main: bool,  // Is this the main/module root?
}
```

## Philosophy

**Codegen should be dumb.**

All decisions made by resolver. Codegen just:
1. Walks AST in source order
2. Looks up from side tables
3. Emits bytecode

No semantic analysis, no name resolution, no control flow validation.

## Generator State

```lx
fn codegen(ast, resolveResult, opts) {
  let generator = .{
    currentFunction: Function(...),
    resolvedNames: resolveResult.resolvedNames,
    scopeInfo: resolveResult.scopeInfo,
    nodes: resolveResult.nodes,
    currentLine: 1,
  }

  compileProgram(generator, ast)

  return .{
    success: true,
    function: generator.currentFunction,
  }
}
```

## Key Functions

### Program Compilation

```lx
fn compileProgram(gen, ast) {
  // Create module root function
  let func = Function(
    name: "",           // ← Module root has empty name!
    arity: 0,
    chunk: Chunk(filename: ast.filename),
    upvalueCount: 0
  )

  gen.currentFunction = func

  // Compile body in order
  each(ast.body, fn(expr) {
    compileExpr(gen, expr)
  })

  emitByte(OP.RETURN, gen.currentLine)
  return func
}
```

### Expression Compilation

```lx
fn compileExpr(gen, node) {
  // Dispatch by node type
  if node.type == "Number" {
    compileNumber(gen, node)
  } else if node.type == "Identifier" {
    compileIdentifier(gen, node)
  } else if node.type == "Call" {
    compileCall(gen, node)
  } else if node.type == "Function" {
    compileFunction(gen, node)
  } else if node.type == "Block" {
    compileBlock(gen, node)
  }
  // ... etc
}
```

### Identifier Compilation (Mechanical!)

```lx
fn compileIdentifier(gen, node) {
  let resolved = gen.resolvedNames[node.id]

  // Resolver already decided the opcode!
  // Just emit it.
  if resolved.kind == "local" or resolved.kind == "upvalue" {
    emitBytes(resolved.getOp, resolved.slot or resolved.upvalueIndex, node.line)
  } else if resolved.kind == "global" {
    emitBytes(resolved.getOp, resolved.globalConst, node.line)
  }
}
```

No logic! Just table lookup + emit.

### Function Compilation

```lx
fn compileFunction(gen, node) {
  // Determine function name (preserve current compiler behavior!)
  let name =
    if node.name {
      node.name.name  // Named: "foo"
    } else {
      "fn"            // Anonymous: "fn" (NEVER empty!)
    }

  // Create function object
  let func = Function(
    name: name,
    arity: len(node.params),
    chunk: Chunk(filename: node.filename),
    upvalueCount: 0  // Will be set from scopeInfo
  )

  // ASSERT: non-module functions must have non-empty names
  if func.name == "" {
    panic("Compiler bug: user function has empty name (should be identifier or \"fn\")")
  }

  // Save current function, switch to nested
  let enclosing = gen.currentFunction
  gen.currentFunction = func

  // Compile body
  compileBlock(gen, node.body)

  // Restore
  gen.currentFunction = enclosing

  // Look up upvalue count from scopeInfo
  let scopeInfo = gen.scopeInfo[node.id]
  func.upvalueCount = len(scopeInfo.upvalues)

  // Emit closure (ONLY place OBJ_FUNCTION constant created!)
  let constIndex = makeConstant(gen.currentFunction.chunk, OBJ_FUNCTION(func))
  emitBytes(OP.CLOSURE, constIndex, node.line)

  // Emit upvalue metadata
  each(scopeInfo.upvalues, fn(uv) {
    emitByte(if uv.isLocal { 1 } else { 0 }, node.line)
    emitByte(uv.slot, node.line)
  })
}
```

### Block Compilation with Cleanup

```lx
fn compileBlock(gen, node) {
  // Compile expressions in order
  each(node.expressions, fn(expr) {
    compileExpr(gen, expr)
  })

  // Look up scope info for cleanup
  let scopeInfo = gen.scopeInfo[node.id]
  if scopeInfo {
    endScope(gen, scopeInfo)
  }
}

fn endScope(gen, scopeInfo) {
  // CRITICAL: Reverse order!
  for let i = len(scopeInfo.locals) - 1; i >= 0; i = i - 1 {
    let local = scopeInfo.locals[i]

    if local.isCaptured {
      emitByte(OP.CLOSE_UPVALUE, gen.currentLine)
    } else {
      emitByte(OP.POP_LOCAL, gen.currentLine)
    }
  }
}
```

## Constant Pool Discipline

**CRITICAL**: `makeConstant` must be append-only for `OBJ_FUNCTION`.

```lx
fn makeConstant(chunk, value) {
  // NEVER deduplicate OBJ_FUNCTION!
  push(chunk.constants, value)
  return len(chunk.constants) - 1
}
```

If you add string interning later, exclude `OBJ_FUNCTION`:

```lx
fn makeConstant(chunk, value) {
  // Only intern primitives, NEVER functions
  if value.type != OBJ_FUNCTION {
    // ... check for existing constant, reuse if found
  }

  // Always append functions
  push(chunk.constants, value)
  return len(chunk.constants) - 1
}
```

## Bytecode Emission Helpers

```lx
fn emitByte(byte, line) {
  push(gen.currentFunction.chunk.code, byte)
  push(gen.currentFunction.chunk.lines, line)
}

fn emitBytes(byte1, byte2, line) {
  emitByte(byte1, line)
  emitByte(byte2, line)
}

fn emitConstant(value, line) {
  let constIndex = makeConstant(gen.currentFunction.chunk, value)
  emitBytes(OP.CONSTANT, constIndex, line)
}
```

## VM Invariants to Preserve

- ✅ Scope cleanup in reverse declaration order
- ✅ POP_LOCAL vs CLOSE_UPVALUE based on isCaptured flag
- ✅ Upvalue list ordering matches scopeInfo.upvalues order
- ✅ Stable slot IDs (no compression)
- ✅ Module root has `name == ""`
- ✅ User functions have non-empty names
- ✅ Function constants append-only (no dedup)

## Testing

- ✅ Output equivalence with compiler.lx (semantic, not bytecode)
- ✅ Mechanical emission (no semantic decisions)
- ✅ Closure handling correct
- ✅ Import emission correct
- ✅ Scope cleanup in reverse order
- ✅ POP_LOCAL vs CLOSE_UPVALUE correct
- ✅ Module root naming correct
- ✅ User function naming correct
- ✅ No function constant deduplication
- ✅ All VM invariants preserved
