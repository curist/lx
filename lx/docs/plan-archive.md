# Multi-Pass Compiler Architecture Plan

## Overview

Transition from single-pass compiler to a multi-pass architecture:

```
Current:  Source → [compiler.lx] → Bytecode

Planned:  Source → [parser.lx] → AST → [lower.lx] → Lowered AST → [resolve.lx] → Resolved AST → [codegen.lx] → Bytecode
                                                                           ↓
                                                                      Side Tables
```

**Phases**:
1. **parser.lx** - Syntax → AST (with Arrow nodes, full syntax preserved)
2. **lower.lx** - Desugar syntax (Arrow → Call, pure syntactic transformations)
3. **resolve.lx** - Semantics (name resolution, scopes, validation)
4. **codegen.lx** - Bytecode emission (mechanical translation)

## Goals

1. **Eliminate forward declarations** - Support mutual recursion via function hoisting
2. **Better error reporting** - Collect all semantic errors before codegen
3. **LSP foundation** - Node IDs + side tables enable fast position-based queries
4. **Cleaner separation** - Syntax (parser) → Semantics (resolver) → Codegen
5. **Maintainability** - Each phase has clear responsibilities

## Key Design Decisions

### 0. Parser vs Resolver Responsibilities

**Decision**: Strict separation between syntax and semantic validation

**Parser (parser.lx)**:
- ✅ Validate grammar/syntax (unexpected token, missing paren, etc.)
- ✅ Build AST with accurate position info
- ✅ Report **syntax errors** only
- ❌ NO semantic validation (scopes, placement rules, name resolution)
- ❌ NO functionDepth/loopDepth tracking

**Resolver (resolve.lx)**:
- ✅ Validate **semantic rules**:
  - `return` only at end of block/file
  - `break`/`continue` only inside loops
  - No duplicate declarations in same scope
  - No undefined variables
- ✅ Track scopes, function depth, loop depth
- ✅ Collect all semantic errors with node IDs
- ✅ Use scope context for better error messages

**Rationale**:
- Parser focuses solely on "is this valid lx syntax?"
- Resolver focuses on "does this code make sense?"
- Clean separation makes both easier to test and maintain
- All semantic errors collected before codegen (better UX)
- Node IDs + scope info available for rich error messages

**Current state**: parser.lx has some semantic validation (return/break placement)
**Action needed**: Remove semantic checks from parser.lx, move to resolve.lx

### 1a. Lowering Phase

**Decision**: Add dedicated `lower.lx` phase between parser and resolver

**What it does**:
- Pure syntactic transformations (desugaring)
- Main job: `Arrow` → `Call` rewriting
  - `x->a()` becomes `a(x)`
  - `x->a()->b()->c()` becomes `c(b(a(x)))`
- Future: other syntax sugar (spread operators, etc.)

**Why separate from resolver**:
- Keeps parser simple (just recognizes syntax)
- Keeps resolver simple (no syntactic rewrites, just semantics)
- Testable in isolation (golden AST tests)
- Resolver/codegen see canonical AST (no Arrow nodes)

**Provenance tracking**:
- Lowering creates **new AST** with new node IDs allocated from the **module's ID counter**
- New IDs continue from `parseResult.nextNodeId` (e.g., parse uses 1..N, lowering uses N+1..M)
- All IDs remain within the **same per-module ID space**
- **Copies position spans** (line/col/endLine/endCol) from original nodes to new nodes
- Maintains `origin[newId] = oldId` for **LSP/tooling only** (not diagnostics)
- **Error reporting uses lowered node's position directly** (spans already copied, no lookup needed!)
- LSP "go to definition" or source attribution can trace through `origin` if needed for tooling

### 1. Node IDs + Side Tables

**Decision**: Add auto-increment IDs to all AST nodes. Store analysis results in side tables keyed by node ID.

**Node ID scope**: **Per-module**
- Node IDs are unique **within a module**, not globally
- Each module has its own ID space (starts at 1)
- Side tables (resolvedNames, scopeInfo, nodes) are **per-module**
- LSP queries use `(modulePath, nodeId)` as identity
- Import cache stores per-module side tables

**Rationale**:
- Keeps AST immutable and clean
- Multiple passes can build independent tables
- Enables efficient LSP queries: `(modulePath, nodeId) → info`
- Easy to serialize/cache for incremental compilation
- Future-proof for type checking, optimization passes

**Alternative considered**: Mutating AST nodes with analysis results
- ❌ Makes AST structure messy
- ❌ Different passes interfere with each other
- ❌ Harder to debug and test

### 1b. Bytecode Compatibility

**Decision**: **NOT** required to match compiler.lx bytecode exactly

**Rationale**:
- Simplifies resolver logic significantly
- Can use stable slot IDs instead of compressed indices
- Eliminates `lowerResolvedLocal()` complexity
- Easier to verify correctness
- VM doesn't care about exact slot numbers, just semantics

**Trade-off**: Can't do byte-for-byte comparison during migration
- Instead: test that programs produce same **output**
- Verify with integration/regression tests, not bytecode diffs

### 1c. Object Format Compatibility and Function Ordering Invariant

**Constraint (existing object format)**:
In the current `.lxobj` format, a constant of type `OBJ_FUNCTION` stores **no function identifier** in the constant payload. Instead, `objbuilder.lx` appends actual function chunks in build order, and `objloader.c` later patches `OBJ_FUNCTION` constants by assuming **the i-th function constant refers to the i-th non-main chunk** (with additional handling for module REF chunks).

This means the compiler must preserve a deterministic, compatible ordering between:

1. The sequence of `OBJ_FUNCTION` constants encountered during serialization, and
2. The sequence of function chunks appended to the object.

**Decision**: Preserve the existing implicit ordering; do not change object layout.

**Invariant (must hold)**:

> **The only time the compiler may create an `OBJ_FUNCTION` constant is when emitting `OP.CLOSURE` for a function expression/declaration.**
> The order of `OP.CLOSURE` emission in codegen must correspond to the order in which the function objects are reachable from `main` when `objbuilder` traverses `chunk.constants` and enqueues functions.

**Practical implications**:

* `resolve.lx` (including hoisting) must never pre-materialize function objects or insert placeholders into constant pools.
* `codegen.lx` must not "finalize", reorder, sort, or deduplicate function constants in a way that changes encounter order.
* The compilation pipeline must ensure the produced `Function` objects remain reachable from the main function's constant graph in the same order as `OP.CLOSURE` emission.

### 1d. Codegen Ordering Discipline (How to Preserve the Current Build Order)

To comply with `objbuilder.lx` and `objloader.c`, `codegen.lx` will follow these rules:

1. **Single-pass emission over the lowered AST, in source order**

   * When compiling a `Block`, compile its `expressions[]` strictly in ascending index order.
   * When compiling a `Program`, compile its top-level `body[]` strictly in order.
   * Do not reorder statements during lowering or resolve.

2. **Emit function constants only at `OP.CLOSURE` sites**

   * When codegen encounters a function-producing node (e.g., named `fn a(){...}` or anonymous `fn(){...}`), it:

     1. compiles the nested function body into a `Function` object;
     2. calls `makeConstant(OBJ_FUNCTION(func))`;
     3. emits `OP.CLOSURE <constIndex>` (and upvalue metadata).
   * No other phase may insert `OBJ_FUNCTION` constants.

3. **Hoisting is semantic-only; it does not change emission order**

   * `resolve.lx` may allow a function body to reference later functions (mutual recursion) via hoisted name resolution.
   * `codegen.lx` still emits closures in source order (the declaration order in the block), and it still enforces that *block-level calls before declaration* are invalid (as resolved errors).

4. **Do not introduce a "function pre-scan emission" pass**

   * Even though resolver does a pre-scan for hoisting, codegen must **not** emit closures during that pre-scan.
   * Codegen emits closures only when it compiles the corresponding AST node in-order.

5. **Imports: keep the current shape**

   * The current runtime expects `import "path"` to compile into something equivalent to:

     * `OP.CLOSURE <const(Function(module))>`
     * `OP.CALL 0`
   * **Import execution semantics**:
     * The module root is a **zero-arity function** whose execution initializes the module
     * The function returns the module's exported value (module table/value)
     * Import expression evaluates to that returned value
     * This matches current `compiler.lx` behavior: `initCompiler(FunctionType.SCRIPT, filename)` creates a zero-arity function that executes the module body
   * **Import caching**:
     * Driver's `importCache` ensures the same `Function` object is returned for a given module path
     * This enables `objbuilder` dedup via `builtModuleCache[filename]` → REF chunks
     * Multiple imports of same module compile to multiple `OP.CLOSURE` + `OP.CALL 0` sequences, but **same Function object** (REF chunk)
   * The object-format implication: imported module function becomes just another `OBJ_FUNCTION` constant, in the order it is emitted by codegen.

**Result**: The function constant encounter order remains deterministic and compatible with `objbuilder.lx` traversal and `objloader.c` patching logic, without changing the object format.

### 1e. Required Tests (Order Preservation)

Add an order-preservation regression suite to ensure the invariant holds:

1. **Sequential functions**

```lx
{
  fn a() { 1 }
  fn b() { 2 }
  fn c() { 3 }
}
```

Expected: the `OBJ_FUNCTION` constants appear in the same order as source: `a`, `b`, `c`.

2. **Nested functions**

```lx
{
  fn outer() {
    fn inner1() { 1 }
    fn inner2() { 2 }
    inner2()
  }
  outer()
}
```

Expected: function constants are enqueued in the same order `OP.CLOSURE` is emitted during codegen traversal.

3. **Mutual recursion (hoisting enabled, no pre-call)**

```lx
{
  fn a() { b() }
  fn b() { a() }
  a()
}
```

Expected: resolves successfully; `OBJ_FUNCTION` constants order remains `a`, `b`.

4. **Illegal pre-call (enforced by resolver)**

```lx
{
  a()
  fn a() { 1 }
}
```

Expected: semantic error. Codegen must not run.

### 1f. Implementation Note (Preventing Accidental Breakage)

In `codegen.lx`, document and enforce:

* `emitClosureForFunction(node)` is the **only** routine allowed to call `makeConstant(OBJ_FUNCTION(...))`.
* Any future optimizations must preserve traversal order or explicitly opt into a new object format (out of scope).

**Critical: Constant Pool Discipline**

`objbuilder.lx` walks `chunk.constants` in list order (not bytecode). The ordering invariant depends on **constant pool append order**, not just emission order.

**Requirements**:

1. **`makeConstant()` must be append-only for `OBJ_FUNCTION`**
   - Do **NOT** deduplicate `OBJ_FUNCTION` constants (even if pointer-equal)
   - Dedup would reduce `OBJ_FUNCTION` occurrences and break loader patching
   - Each function literal/declaration gets a unique constant slot

2. **If you add constant interning later (for strings/numbers)**:
   - Explicitly exclude `OBJ_FUNCTION` from interning
   - Only intern primitive constants (strings, numbers, booleans)
   - Document this exclusion permanently (unless object format changes)

**Example of what NOT to do**:
```lx
// ❌ WRONG - deduplicating functions
fn makeConstant(value) {
  for let i = 0; i < len(chunk.constants); i = i + 1 {
    if chunk.constants[i] == value {
      return i  // BREAKS loader if value is OBJ_FUNCTION!
    }
  }
  push(chunk.constants, value)
  return len(chunk.constants) - 1
}

// ✅ CORRECT - append-only for functions
fn makeConstant(value) {
  // Never search for existing OBJ_FUNCTION
  push(chunk.constants, value)
  return len(chunk.constants) - 1
}
```

**One Function AST Node → One Unique Function Object**

Each function-producing AST node corresponds to exactly one unique `Function` object (unique chunk), **except** module roots which may be referenced multiple times and handled via REF chunks.

This means:
- Each `fn a(){}` declaration emits exactly one `OP.CLOSURE`
- Each anonymous `fn(){}` expression emits exactly one `OP.CLOSURE`
- No duplicate emissions for the same AST node (e.g., via both prescan and main pass)
- No reusing function objects for distinct function literals

The loader expects: `chunkIndexes.count == chunks_count - 1 - shared_module_count`

If you accidentally emit `OP.CLOSURE` twice for the same node, or reuse function objects, you'll get chunk count mismatches and loader patching failures.

### 1g. Module Chunk Classification Invariant

**Constraint (objbuilder module dedup)**:
`objbuilder.lx` treats a function as a "module chunk" if and only if `func.name == ""`. Only module chunks are eligible for:
- **Module deduplication** via `builtModuleCache[filename]`
- **REF chunk emission** instead of ACTUAL chunk for repeat imports

This classification directly affects loader behavior: REF chunks change how `objloader.c` patches `OBJ_FUNCTION` constants (compensates using `shared_module_count`).

**Invariants (must hold)**:

1. **Exactly one module root per file**: Each compiled source file must produce exactly one function with `name == ""` and `chunk.filename == <canonical module path>`
2. **Named functions must have non-empty names**: All user-declared functions (e.g., `fn a() {}`) must have `name == "a"` (never empty string)
3. **Stable path canonicalization**: Always use the same resolved/absolute path string for a given module, so `builtModuleCache[filename]` hits consistently

**What breaks if violated**:
- Empty name on user function → misclassified as module, incorrect dedup/REF behavior
- Multiple empty-name functions per file → duplicate module chunks, incorrect REF indexing
- Inconsistent paths (relative vs absolute) → failed dedup, duplicate ACTUAL chunks instead of REF
- Wrong filename on module root → dedup key mismatch, duplicate module chunks

**Implementation requirements**:

**A. Module root (top-level compilation)**:
```lx
// In codegen.lx or driver, when compiling a file/module:
fn compileModule(absPath, importCache) {
  // ... parse, lower, resolve ...

  // Codegen produces module root function:
  let moduleFunc = Function(
    name: "",                    // ← CRITICAL: empty for module root!
    arity: 0,
    chunk: Chunk(
      filename: absPath          // ← Canonical path for dedup key
    ),
    upvalueCount: 0
  )

  // This mirrors current compiler.lx:
  //   initCompiler(FunctionType.SCRIPT, filename)
}
```

**B. Named functions (user declarations)**:
```lx
// In codegen.lx, when compiling `fn a() { ... }`:
fn compileFunction(gen, node) {
  let func = Function(
    name: node.name.name,        // ← "a", "b", etc. (NEVER "")
    arity: len(node.params),
    chunk: Chunk(
      filename: gen.currentFile  // ← Same file, but name != ""
    ),
    upvalueCount: len(upvalues)
  )

  // This mirrors current compiler.lx:
  //   if type != FunctionType.SCRIPT {
  //     current.function.name = parser.previous.lexeme
  //   }
}
```

**C. Path canonicalization (driver)**:
```lx
// ALWAYS use resolvePath() before caching/compiling:
fn compileModule(path, importCache, opts) {
  let absPath = resolvePath(path)  // ← Canonical path

  // Use absPath consistently:
  importCache[absPath] = ...
  parse(src, absPath)
  // ... all phases use absPath
}
```

**D. Import dedup expectations**:
- When resolving `import "foo.lx"` multiple times, `builtModuleCache` should hit
- This produces a REF chunk instead of duplicate ACTUAL chunk
- Loader compensates REF chunks when patching function indices
- **Critical**: If dedup fails, you get duplicate chunks AND incorrect loader patching

**Why this matters for the new pipeline**:
- **Parser**: No impact (just builds AST)
- **Lower**: No impact (transforms syntax)
- **Resolve**: Must use canonical paths for import cache keys
- **Codegen**: Must set `func.name` correctly (empty for module root, non-empty for named functions)
- **Driver**: Must canonicalize paths before passing to all phases

**Enforcement (compiler bug guardrail)**:

Add assertions in codegen to catch violations early:

```lx
fn codegenModuleRoot(gen, ast) {
  let func = Function(...)

  // ASSERT: module root must have empty name
  if func.name != "" {
    panic("Compiler bug: module root must have name == \"\", got: " + func.name)
  }

  return func
}

fn codegenNamedFunction(gen, node) {
  let func = Function(name: node.name.name, ...)

  // ASSERT: user functions must have non-empty name
  if func.name == "" {
    panic("Compiler bug: user function must have non-empty name")
  }

  return func
}
```

These are **not** user errors - they indicate compiler implementation bugs that will corrupt object layout. Hard error immediately.

### 2. Function Hoisting Strategy

**Decision**: Hoisting for function bodies only (not block-level calls)

**The Rule**:
Inside a `{ ... }` block:
1. Named `fn a() {}` declarations are **hoistable for function bodies** (enables mutual recursion)
2. But **NOT callable from block-level expressions before declaration** (preserves ordering)

**Examples**:
```lx
// ❌ ILLEGAL - block-level call before declaration
{
  a()         // Error: "Cannot use function 'a' before its declaration"
  fn a() { ... }
}

// ✅ LEGAL - mutual recursion in function bodies
{
  fn a() { b() }  // ✅ b() callable inside a's body (hoisted)
  fn b() { a() }  // ✅ a() callable inside b's body (hoisted)
  a()             // ✅ call after declaration
}

// ❌ ILLEGAL - let doesn't hoist
{
  a()
  let a = fn() {}  // Error: undefined variable 'a'
}

// ❌ EDGE CASE - block-level call before mutual recursion group is complete
{
  fn a() { b() }   // a references b (allowed via hoisting)
  a()              // Should this be allowed?
  fn b() { a() }   // b declared after a() call
}
// RECOMMENDED STRICTER RULE: Fail this during semantic validation
// Reason: a() executes before b's slot is initialized (runtime error)
// Solution: Block-level calls to hoisted functions only allowed AFTER
//           the LAST hoisted function declaration in the block
// This ensures entire mutual recursion group is initialized before any call
```

**What gets hoisted**:
- `fn foo() {}` - ✅ hoisted (named function declaration)
- `let foo = fn() {}` - ❌ not hoisted (regular let semantics)
- Anonymous functions - ❌ not applicable

**Why this rule**:
- **Block-level ordering preserved**: Top-level expressions execute in order
- **Mutual recursion enabled**: Function bodies see all hoisted functions
- **Intuitive**: Matches how developers think about code flow
- **Type-checker friendly**: Can predeclare function signatures, then check bodies

**Refinement for safety** (recommended):
To prevent runtime errors from partially-initialized mutual recursion groups, enforce:
- Block-level calls to hoisted functions are only allowed **after the last hoisted function declaration** in that block
- This is conservative but guarantees all mutual recursion participants are initialized before any block-level call
- Function bodies remain unrestricted (can reference any hoisted function)

**Implementation note**:
During prescan, track `lastHoistedFunctionIndex`. During resolution, block-level calls to ANY hoisted function must have `exprIndex >= lastHoistedFunctionIndex`.

### 3. Import Caching

**Decision**: Single import cache owned by driver (compile pipeline)

**Cache ownership**: Driver, not individual phases
- Driver maintains `importCache[path]` and passes it to all phases
- Phases read from cache but don't manage lifecycle
- Clean separation: phases don't coordinate with each other

**Cache structure** (with lifecycle states):
```lx
importCache[path] = .{
  status: "parsing" | "lowering" | "resolving" | "codegen" | "done" | "failed",
  parseResult: { ast, errors, nextNodeId },
  lowerResult: { ast, origin, errors, nextNodeId },
  resolveResult: { resolvedNames, scopeInfo, nodes, errors },
  function: compiledFunction,  // Only set when status == "done"
  errors: [],                  // Accumulated errors (if status == "failed")
}
```

**Circular import detection**: Check status field
- If status is `"parsing"`, `"lowering"`, `"resolving"`, or `"codegen"` → circular import error
- Status `"done"` → safe to use
- Status `"failed"` → compilation failed, use cached errors (don't recompile)
- No need for separate import depth counter

**Cache key**: Absolute resolved file path

## Implementation Phases

### Phase 1: Add Node IDs to parser.lx + Remove Semantic Validation ✅ (Current)

**Changes needed**:
1. Add `nodeIdCounter` to parser state
2. Modify `Node()` constructor to assign IDs
3. All node constructors automatically inherit IDs
4. Return `nextNodeId` in parse result
5. **Remove semantic validation**:
   - Remove `functionDepth` tracking
   - Remove `loopDepth` tracking
   - Remove return/break/continue placement checks
   - Parser only reports syntax errors

**Files modified**: `src/parser.lx`

**What to keep in parser.lx**:
- Arrow nodes! Parser still creates `ArrowNode(left, right)`
- Lowering phase will transform them

**What to remove from parser.lx**:
```lx
// Remove these lines:
let functionDepth = 0
let loopDepth = 0

// In fnExpr(), remove:
functionDepth = functionDepth + 1
// ...
functionDepth = functionDepth - 1

// In forExpr(), remove:
loopDepth = loopDepth + 1
// ...
loopDepth = loopDepth - 1

// In returnStatement(), remove validation:
if functionDepth == 0 {
  if !check(TOKEN.EOF) {
    error("Can only return at end of file.")
  }
} else {
  if !check(TOKEN.RIGHT_BRACE) {
    error("Can only return at end of block.")
  }
}

// In breakExpr(), remove:
if loopDepth == 0 {
  error("Can only break inside a loop.")
}

// In continueExpr(), remove:
if loopDepth == 0 {
  error("Can only continue inside a loop.")
}
```

**Result**: Parser only builds AST, no semantic validation

### Phase 2: Implement lower.lx 🔨 (Next)

**Core responsibilities**:
- Transform Arrow nodes into Call nodes
- Validate arrow RHS is a call expression
- Create new AST with fresh node IDs
- Maintain provenance mapping (`origin[newId] → oldId`)
- Copy non-Arrow nodes with updated child references

**Files to create**: `src/lower.lx`

**API**:
```lx
fn lower(ast, opts) {
  // opts: { startNodeId } - where to start fresh IDs

  let lowerer = .{
    nextId: opts.startNodeId or 1,
    origin: .{},           // newId → originalId
    errors: [],            // lowering errors (e.g., "x->f" without call)
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

**Lowering algorithm**:
```lx
fn lowerExpr(lowerer, node) {
  if !node { return nil }

  if node.type == "Arrow" {
    return lowerArrow(lowerer, node)
  } else if node.type == "Call" {
    return lowerCall(lowerer, node)
  } else if node.type == "Binary" {
    return lowerBinary(lowerer, node)
  }
  // ... dispatch to type-specific lowering
}

fn lowerArrow(lowerer, node) {
  let left = lowerExpr(lowerer, node.left)
  let right = lowerExpr(lowerer, node.right)

  // Validate RHS is a call (lowering-time check, not grammar)
  if right.type != "Call" {
    addError(lowerer, node.id, "Arrow operator requires function call on right side")
    return left  // Error recovery: just return left side
  }

  // Rewrite: x->f(a, b) becomes f(x, a, b)
  let newCall = copyNode(right)  // Copy the Call node
  newCall.id = lowerer.nextId
  lowerer.nextId = lowerer.nextId + 1
  newCall.args = [left] + right.args

  // Copy position from original arrow node
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

**Result**: Canonical AST with no Arrow nodes, ready for resolver

### Phase 3: Implement resolve.lx 🔜

**Core responsibilities**:
- Build scope chain (global → function → block → loop)
- Resolve all identifiers (local/upvalue/global)
- Function hoisting
- **Semantic validation**:
  - `return` only at end of block/file (check scope type + position)
  - `break`/`continue` only inside loops (check scope chain)
  - Duplicate declarations in same scope
  - Undefined variable usage
  - **Read-before-initialization**: Can't read local in its own initializer (`depth == -1`)
  - Invalid assignment targets
- Handle imports and caching
- Build side tables

**Side tables to build**:
```lx
.{
  nodes: .{},           // nodeId → node (for O(1) lookup)
  resolvedNames: .{},   // nodeId → { kind, index, depth, declaredAt, isCaptured }
  scopeInfo: .{},       // nodeId → { scopeType, depth, locals, upvalues }
  errors: [],           // [{ nodeId, message, severity }] (structured errors)
}
```

**Files to create**: `src/resolve.lx`

**Hoisting Implementation Algorithm**:

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

fn resolveFunction(resolver, node, context) {
  // ... setup function scope

  // Resolve function body with inFunctionBody = true
  let bodyContext = .{
    exprIndex: 0,
    inFunctionBody: true,  // ← Key flag!
  }

  resolveBlock(resolver, node.body, bodyContext)

  // ... cleanup
}

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
          "Cannot use hoisted function '" + name + "' before all hoisted functions are declared (last at index " +
          scope.lastHoistedFunctionIndex + ")")
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

**Key insights**:
1. `hoistedFns` records **position** (`declIndex`) for each hoisted function
2. `lastHoistedFunctionIndex` tracks the last hoisted function in the block
3. `context.inFunctionBody` flag enables/disables ordering check
4. **Block-level expressions**: `exprIndex < lastHoistedFunctionIndex` → error (stricter rule)
5. **Function bodies**: ordering check bypassed (mutual recursion works)

**Runtime invariant** (why the stricter rule is necessary):
- Hoisted function reference resolves to **slot lookup at call time**
- Slot is populated during block initialization (when `fn a(){}` executes)
- **Block-level call safety**: Call site must execute after ALL hoisted functions are declared
- This ensures entire mutual recursion group is initialized before any member is called
- Example: `{ fn a(){b()} fn b(){a()} a() }` works because:
  - Prescan creates slots for both `a` and `b`
  - `fn a(){}` executes, populates `a`'s slot
  - `fn b(){}` executes, populates `b`'s slot
  - `a()` call executes (after lastHoistedFunctionIndex=1), all slots ready
- Counter-example: `{ fn a(){b()} a() fn b(){a()} }` fails because:
  - `a()` call at index 1 < lastHoistedFunctionIndex=2
  - Semantic error prevents runtime crash from uninitialized `b` slot

### Phase 4: Implement codegen.lx 🔜

**Core responsibilities**:
- Walk resolved AST
- Look up node info from side tables
- Emit bytecode (reuse logic from compiler.lx)
- Generate chunks, constants, lines

**Files to create**: `src/codegen.lx`

### Phase 5: Integration & Testing 🔜

**Integration points**:
1. Create driver function that orchestrates all phases
2. Driver owns and manages `importCache`
3. Driver passes cache to resolve phase
4. Error aggregation and reporting with provenance

**Driver implementation**:
```lx
fn compile(src, filename, opts) {
  let importCache = opts.importCache or .{}

  return compileModule(filename, importCache, .{ source: src })
}

fn compileModule(path, importCache, opts) {
  let absPath = resolvePath(path)

  // Check cache + circular detection
  let cached = importCache[absPath]
  if cached {
    if cached.status == "done" {
      return .{ success: true, function: cached.function }
    } else {
      return .{
        success: false,
        errors: ["Circular import: " + absPath + " (" + cached.status + ")"]
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
    return .{ success: false, errors: lowerResult.errors }
  }

  // Phase 3: Resolve (pass compileModule callback)
  importCache[absPath].status = "resolving"
  let resolveResult = resolve(lowerResult.ast, .{
    importCache: importCache,
    compileModule: fn(path) { compileModule(path, importCache, .{}) }
  })
  importCache[absPath].resolveResult = resolveResult
  if !resolveResult.success {
    importCache[absPath].status = "failed"
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

**Testing strategy**:
- Unit tests for each phase
- Regression tests against current compiler
- Test mutual recursion examples
- Test error messages

### Phase 6: Migrate & Deprecate 🔜

1. Run both compilers in parallel (compare outputs)
2. Fix any discrepancies
3. Switch default to new pipeline
4. Mark compiler.lx as deprecated
5. Eventually remove compiler.lx

## Detailed Design

### parser.lx Modifications

```lx
fn parse(src, filename) {
  let scanner = initScanner(src)

  // Add node ID counter
  let nodeIdCounter = 0
  fn nextNodeId() {
    nodeIdCounter = nodeIdCounter + 1
    nodeIdCounter
  }

  // Modify Node constructor
  fn Node(type, filename, token) {
    let tokenLen = len(token.lexeme)
    let startCol = token.col - tokenLen
    if startCol < 0 { startCol = 0 }

    .{
      id: nextNodeId(),    // ← NEW
      type: type,
      filename: filename,
      line: token.line,
      col: startCol,
      endLine: token.line,
      endCol: token.col,
    }
  }

  // ... rest of parser unchanged

  return .{
    success: !parser.hadError,
    ast: ProgramNode(filename, body),
    errors: parser.errors,
    nextNodeId: nodeIdCounter + 1,  // ← NEW
  }
}
```

### resolve.lx API

```lx
fn resolve(ast, opts) {
  // opts: { importCache, compileModule }

  let resolver = initResolver(ast, opts)

  // Build node ID → node map for O(1) lookups
  buildNodeMap(resolver, ast)

  // Resolve program
  resolveProgram(resolver, ast)

  return .{
    success: len(resolver.errors) == 0,
    ast: ast,  // unchanged
    nodes: resolver.nodes,               // ← NEW: id → node map
    resolvedNames: resolver.resolvedNames,
    scopeInfo: resolver.scopeInfo,
    errors: resolver.errors,             // ← Array of structured errors
    importCache: resolver.importCache,
  }
}

fn buildNodeMap(resolver, node) {
  if !node { return }

  // Register this node
  if node.id {
    resolver.nodes[node.id] = node
  }

  // Recursively walk children based on node type
  if node.type == "Program" {
    each(node.body, fn(child) { buildNodeMap(resolver, child) })
  } else if node.type == "Block" {
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

**Key functions**:
- `resolveProgram(resolver, ast)` - Entry point
- `resolveExpr(resolver, node, context)` - Dispatch by node type
  - `context.exprIndex` - Current position in block (for ordering check)
  - `context.inFunctionBody` - Are we inside a function body?
- `resolveBlock(resolver, node)` - **Two-phase with ordering**:
  1. Pre-scan: Record hoisted functions
  2. Resolve in order: Enforce "no call before declaration" at block level
- `resolveName(resolver, name, scope, context)` - Walk scope chain + ordering check
- `resolveImport(resolver, node)` - Import handling
- `declareLocal(resolver, name, kind)` - Add to scope
- `markCaptured(resolver, name, scope)` - Track closures
- **Hoisting functions**:
  - `preScanBlock(resolver, block)` - Build `hoistedFns` table
  - `checkHoistedUsage(resolver, name, hoisted, context)` - Enforce ordering
- **Semantic validation functions**:
  - `validateReturn(resolver, node)` - Check scope + position
  - `validateBreak(resolver, node)` - Check in loop
  - `validateContinue(resolver, node)` - Check in loop
  - `isInLoop(scope)` - Walk scope chain for loop
  - `isInFunction(scope)` - Walk scope chain for function

**Scope management**:
```lx
fn Scope(enclosing, type) {.{
  enclosing: enclosing,
  type: type,              // "global" | "function" | "block" | "loop"
  depth: enclosing and enclosing.depth + 1 or 0,

  // Track locals by name for lookup
  localsByName: .{},       // name → Local

  // Track locals in declaration order for cleanup
  localsArray: [],         // [Local] - MUST preserve order!

  // Hoisted functions (for mutual recursion)
  hoistedFns: .{},         // name → { declNodeId, declIndex, slot }
  lastHoistedFunctionIndex: -1,  // Position of last hoisted function (for safety check)

  // Upvalues (functions only)
  upvalues: [],            // [Upvalue] - indexed by upvalue number

  // Monotonic slot counter
  nextSlot: 0,
}}

fn Local(name, depth, kind, slot, nodeId) {.{
  name: name,
  depth: depth,            // -1 during init, scopeDepth after markInitialized
  kind: kind,              // "var" | "fn" | "param"
  slot: slot,              // stable slot ID
  nodeId: nodeId,          // AST node for this declaration
  isCaptured: false,       // set to true if captured by nested function
}}

fn Upvalue(index, isLocal, slot) {.{
  index: index,            // upvalue index in this function
  isLocal: isLocal,        // true = captures local, false = captures upvalue
  slot: slot,              // slot/upvalue in parent scope
}}

fn HoistedFunction(declNodeId, declIndex, slot) {.{
  declNodeId: declNodeId,  // AST node of the function declaration
  declIndex: declIndex,    // Position in block's expression list
  slot: slot,              // Local slot assigned to this function
}}
```

**Critical**:
- `localsArray` preserves declaration order for proper scope cleanup
- `hoistedFns[name].declIndex` tracks position for ordering check

### codegen.lx API

```lx
fn codegen(ast, resolveResult, opts) {
  // opts: { main: bool }

  let generator = initGenerator(ast, resolveResult, opts)

  let func = compileProgram(generator, ast)

  return .{
    success: true,
    function: func,
  }
}
```

**Key functions**:
- `compileProgram(gen, ast)` - Entry point
- `compileExpr(gen, node)` - Dispatch by node type
- `compileIdentifier(gen, node)` - Purely mechanical! Just emit the opcodes:
  ```lx
  fn compileIdentifier(gen, node) {
    let resolved = gen.resolvedNames[node.id]
    // Resolver already decided the opcode!
    emitBytes(resolved.getOp, resolved.slot or resolved.upvalueIndex, node.line)
  }
  ```
- `compileAssignment(gen, node)` - Same mechanical pattern
- `compileFunction(gen, node)` - Look up scope info for upvalues
- `compileBlock(gen, node)` - Look up scope info for cleanup
- `emitByte/emitBytes/emitConstant` - Bytecode emission (reused from compiler.lx)

**Philosophy**: Codegen should be **dumb**. All decisions made in resolver.

## Side Table Schemas

### resolvedNames[nodeId]

For `Identifier` nodes (variable access/assignment):
```lx
.{
  kind: "local" | "upvalue" | "global",

  // Opcodes for mechanical codegen (no logic in codegen!)
  getOp: OP.GET_LOCAL | OP.GET_UPVALUE | OP.GET_GLOBAL,
  setOp: OP.SET_LOCAL | OP.SET_UPVALUE | OP.SET_GLOBAL,

  // Index to use with the opcode
  slot: number,            // local slot (stable, monotonic per function)
  upvalueIndex: number,    // upvalue index (if kind == "upvalue")
  globalConst: number,     // constant pool index (if kind == "global")

  // Metadata
  declaredAt: nodeId,      // points to declaration node (Let/Function/Param)
  isCaptured: bool,        // true if captured by nested function (locals only)
  depth: number,           // scope depth where declared
}
```

**Key insight**: Resolver decides **which opcode** to use, codegen just emits it.

**Stable slot IDs**: Each local gets a monotonic slot number in declaration order
- No "compressed index" logic
- No `lowerResolvedLocal()` complexity
- Uninitialized locals (`depth == -1`) still get a slot
- Simpler and more correct

### scopeInfo[nodeId]

For `Block`, `Function`, `For` nodes:
```lx
.{
  scopeType: "global" | "function" | "block" | "loop",
  depth: number,

  // Locals in DECLARATION ORDER (critical for endScope cleanup)
  locals: [
    .{
      name: string,
      slot: number,        // stable slot ID
      kind: "var" | "fn" | "param",
      depth: number,       // -1 during initialization, scopeDepth after
      isCaptured: bool,    // true if used by nested closure
    }
  ],

  // Upvalues (functions only) - captured variables from enclosing scopes
  upvalues: [
    .{
      index: number,       // upvalue index
      isLocal: bool,       // true if captures local, false if captures upvalue
      slot: number,        // slot/upvalue index in parent scope
    }
  ],

  // Hoisted functions in this scope (for mutual recursion)
  hoistedFns: [nodeId],

  // Next slot to assign (monotonic counter)
  nextSlot: number,
}
```

**Why declaration order matters**:
When ending a scope, we emit POP_LOCAL or CLOSE_UPVALUE in reverse declaration order.
Resolver must track this exactly as compiler.lx does in `endScope()`.

### nodes[nodeId]

Map from node ID to AST node (built during resolve phase):
```lx
.{
  1: { id: 1, type: "Number", value: 42, line: 1, col: 5, ... },
  2: { id: 2, type: "Identifier", name: "x", line: 1, col: 10, ... },
  // ... all nodes indexed by ID
}
```

**Purpose**: O(1) node lookup for error reporting, LSP queries, and codegen

### errors (array of structured errors)

```lx
[
  .{
    nodeId: 42,
    message: "Undefined variable 'x'",
    severity: "error",  // "error" | "warning" | "info"
  },
  .{
    nodeId: 123,
    message: "Variable 'y' is never used",
    severity: "warning",
  },
]
```

## Migration Strategy

### Step 1: Parallel Implementation
- Keep compiler.lx working
- Build new pipeline alongside
- Compare outputs for identical inputs

### Step 2: Feature Parity
- Ensure all compiler.lx features work
- **Semantic equivalence** (not bytecode equivalence)
- Handle all edge cases
- Programs produce same output (not same bytecode)

### Step 3: Testing
- Run entire test suite - verify **output** not bytecode
- Add new tests for mutual recursion
- Test edge cases:
  - Uninitialized locals
  - Deeply nested closures
  - Complex upvalue chains
- Performance benchmarks

### Step 4: Gradual Rollout
- Add flag to choose compiler: `opts.useNewCompiler`
- Default to new compiler for new features
- Eventually deprecate old compiler

### Step 5: Cleanup
- Remove compiler.lx
- Update documentation
- Simplify entry points

## Error Handling Strategy

### Current (compiler.lx)
- Mixes syntax and semantic errors
- Prints errors immediately via `groanln()`
- Stops at first error in panic mode
- Hard to collect all errors

### New (parser.lx + resolve.lx + codegen.lx)

**Syntax Errors (parser.lx)**:
- Unexpected token, missing delimiter, malformed grammar
- Collected in `parseResult.errors` string array
- Reported after parsing completes
- If syntax errors exist, **skip resolve/codegen**

**Semantic Errors (resolve.lx)**:
- Undefined variables, duplicate declarations
- Invalid return/break/continue placement
- Type mismatches (future)
- Collected as structured error objects with node IDs
- Report all semantic errors before codegen
- Skip codegen if semantic errors exist
- Better error messages with full scope context
- Support error severity levels (error/warning/info)

**Error Flow**:
```lx
// 1. Parse (syntax)
let parseResult = parse(src, filename)
if !parseResult.success {
  reportErrors(parseResult.errors)
  return  // Stop, can't build AST
}

// 2. Lower (desugar)
let lowerResult = lower(parseResult.ast, .{ startNodeId: parseResult.nextNodeId })
if !lowerResult.success {
  // Build node map from lowered AST for error reporting
  let lowerNodes = buildNodeMap(lowerResult.ast)
  reportErrors(lowerResult.errors, lowerNodes)
  return  // Stop, lowering errors
}

// 3. Resolve (semantics)
let resolveResult = resolve(lowerResult.ast, opts)
if !resolveResult.success {
  // resolveResult.nodes already built during resolve
  reportErrors(resolveResult.errors, resolveResult.nodes)
  return  // Stop, semantic errors
}

// 4. Codegen
let codegenResult = codegen(lowerResult.ast, resolveResult, opts)
return codegenResult.function
```

**Error reporting** (uses lowered nodes with copied spans):
```lx
fn reportErrors(errors, nodes) {
  // nodes is from lowered/resolved AST
  // Spans are already copied from original, so just use them!
  each(errors, fn(err) {
    let node = nodes[err.nodeId]
    groanln(formatError(node, err.message))
  })
}

// origin map is only used for LSP "trace back to original syntax"
```

**Error collection**:
```lx
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

// Usage:
fn resolveIdentifier(resolver, node) {
  let resolved = resolveName(resolver, node.name)
  if !resolved {
    addError(resolver, node.id, "Undefined variable '" + node.name + "'", "error")
    return
  }
  // ...
}
```

**Error reporting**:
```lx
fn reportErrors(errors, nodes) {
  // Sort by position for consistent output
  let sorted = sortErrors(errors, nodes)

  each(sorted, fn(err) {
    let node = nodes[err.nodeId]  // O(1) lookup!
    let prefix = if err.severity == "warning" { "Warning" } else { "Error" }
    groanln(join([
      "[", node.filename, ":", node.line, ":", node.col, "]",
      " ", prefix, ": ", err.message
    ], ""))
  })
}

fn sortErrors(errors, nodes) {
  // Sort by: filename → line → col
  let sorted = [...errors]
  // ... sorting logic using nodes[err.nodeId]
  return sorted
}
```

## Import Handling

### Cache Structure (Owned by Driver)
```lx
// Global cache managed by compile driver
let importCache = .{
  // Key: absolute resolved path
  // Value: compilation state + results
  "/absolute/path/to/module.lx": .{
    status: "parsing" | "lowering" | "resolving" | "codegen" | "done",
    parseResult: { ast, errors, nextNodeId },
    lowerResult: { ast, origin, errors, nextNodeId },
    resolveResult: { resolvedNames, scopeInfo, nodes, errors },
    function: compiledFunction,  // Only present when status == "done"
  }
}
```

### Import Compilation Flow (Driver)
```lx
fn compileModule(path, importCache) {
  let absPath = resolvePath(path)

  // Check cache
  let cached = importCache[absPath]
  if cached {
    if cached.status == "done" {
      return cached  // Fully compiled
    } else {
      // In-progress = circular import
      error("Circular import detected: " + absPath + " (status: " + cached.status + ")")
      return nil
    }
  }

  // Initialize cache entry with "parsing" status
  importCache[absPath] = .{ status: "parsing" }

  // 1. Parse
  let parseResult = parse(slurp(absPath), absPath)
  importCache[absPath].parseResult = parseResult
  importCache[absPath].status = "lowering"

  // 2. Lower
  let lowerResult = lower(parseResult.ast, .{ startNodeId: parseResult.nextNodeId })
  importCache[absPath].lowerResult = lowerResult
  importCache[absPath].status = "resolving"

  // 3. Resolve (may recursively compile imports)
  let resolveResult = resolve(lowerResult.ast, .{ importCache: importCache })
  importCache[absPath].resolveResult = resolveResult
  importCache[absPath].status = "codegen"

  // 4. Codegen
  let codegenResult = codegen(lowerResult.ast, resolveResult, .{})
  importCache[absPath].function = codegenResult.function
  importCache[absPath].status = "done"

  return importCache[absPath]
}
```

### Circular Import Detection (Automatic)
```lx
// No need for import stack!
// Just check the status field:

fn resolveImport(resolver, node, importCache) {
  let path = resolvePath(node.path.value)

  let cached = importCache[path]
  if cached {
    if cached.status != "done" {
      // Module is still being compiled = circular import
      addError(resolver, node.id,
        "Circular import: " + path + " (currently " + cached.status + ")")
      return
    }
    // Use cached.function
    return
  }

  // Not in cache - compile it via callback
  resolver.compileModule(path, importCache)
}
```

**Resolver-Driver Dependency**:
- Resolver needs to trigger compilation of imports
- Design choice: **Resolver calls back into driver**
- `resolve(ast, .{ compileModule: compileModuleFn })` - driver passes callback
- Avoids module import cycles in implementation
- Driver remains orchestrator, resolver doesn't import driver directly
```

**Key insight**: Status field serves dual purpose:
1. Lifecycle tracking (where are we in compilation?)
2. Circular import detection (is this module in-progress?)

## Future LSP Support

### Position → Symbol Lookup
```lx
fn getSymbolAtPosition(resolveResult, line, col) {
  // 1. Find node at position (tree walk or position index)
  let nodeId = findNodeIdAtPosition(resolveResult.ast, line, col)
  if !nodeId { return nil }

  // 2. O(1) node lookup!
  let node = resolveResult.nodes[nodeId]
  let resolved = resolveResult.resolvedNames[nodeId]
  let scope = resolveResult.scopeInfo[nodeId]

  // 3. O(1) declaration lookup!
  let declNode = resolveResult.nodes[resolved.declaredAt]

  return .{
    name: node.name,
    kind: resolved.kind,
    definition: declNode,
    references: findReferences(resolveResult, node.name),
    scope: scope,
  }
}

// Example: Find all references to a symbol
fn findReferences(resolveResult, targetName) {
  let refs = []
  each(resolveResult.resolvedNames, fn(nodeId, resolved) {
    let node = resolveResult.nodes[nodeId]  // O(1) lookup!
    if node.name == targetName {
      push(refs, node)
    }
  })
  return refs
}
```

### Additional LSP Features Enabled
- **Go to definition**: `nodes[resolved.declaredAt]` - O(1) lookup!
- **Find references**: Search `resolvedNames` for matching `declaredAt`
- **Hover info**: Show type (future), scope, kind via `nodes[nodeId]`
- **Rename**: Update all references with same `declaredAt`
- **Diagnostics**: Already collected in structured `errors` array
- **Document symbols**: Extract from `scopeInfo` and `nodes` map
- **Code navigation**: Fast position ↔ node lookups

## Testing Plan

### Unit Tests

**parser.lx**:
- ✅ Node IDs are unique
- ✅ Node IDs increment sequentially
- ✅ All node types get IDs
- ✅ Arrow nodes preserved in AST

**lower.lx**:
- Arrow → Call transformation correctness
- Provenance tracking (origin map)
- Error recovery (invalid arrow usage)
- Golden tests: AST before/after lowering
- Chain handling: `x->a()->b()->c()` becomes `c(b(a(x)))`

**resolve.lx**:
- Scope management (begin/end, nesting)
- Local resolution (same scope, parent scope)
- Upvalue resolution (capture chain)
- Global fallback
- Function hoisting (mutual recursion)
- **Semantic validation**:
  - Undefined variables
  - Duplicate declarations
  - Return/break/continue placement
  - Assignment target validity
- **Import handling**:
  - Read from importCache (check status)
  - Trigger compilation of imported modules (via driver)
  - Detect circular imports (status != "done")
- Error collection with node IDs

**codegen.lx**:
- Programs produce same **output** as compiler.lx (semantics, not bytecode)
- Purely mechanical opcode emission (resolver decides opcodes)
- Proper closure handling
- Import emission
- **VM invariants preserved**:
  - Scope cleanup in reverse declaration order (POP_LOCAL vs CLOSE_UPVALUE)
  - Upvalue list ordering matches closure capture order
  - Reserved slots (if any) documented in codegen
  - Stable slot IDs (simpler than compressed indices)

### Integration Tests
- Full pipeline: source → AST → resolved → bytecode → run
- Compare output with compiler.lx
- Test all language features
- Performance benchmarks

### Regression Tests
- Run existing test suite
- Ensure no behavior changes
- **Match compiler.lx output/semantics** (not bytecode)

## Performance Considerations

### Node Map Memory Overhead
**Cost**: O(n) space where n = number of AST nodes
- If 10,000 nodes → 10,000 hashmap entries
- Each entry is just a reference to existing node
- Nodes already exist in AST, so minimal extra allocation

**Benefit**: O(n) tree walk → O(1) hashmap lookup
- Error reporting: Much faster (no tree walking)
- LSP queries: Instant node lookup by ID
- Codegen: Fast access to declaration nodes

**Build time**: O(n) - single tree walk during resolve phase
- Already walking AST anyway, so net cost ≈ 0
- Just one extra hashmap insert per node

### Overall Performance Impact
- **Parser**: No change (just increment counter, assign ID)
- **Resolver**: +O(n) for building node map, but saves many O(n) tree walks
- **Codegen**: Faster due to O(1) node lookups
- **Expected net**: Neutral to slightly faster

## Open Questions

1. **Performance**: How much overhead does multi-pass add?
   - Mitigation: Benchmark and optimize hot paths
   - Node map should speed up overall pipeline

2. **Memory**: Storing side tables for large files?
   - Mitigation: Can discard tables after codegen
   - Node map is ~2x AST size (acceptable tradeoff)
   - Import cache grows with transitive imports (but bounded by module count)

3. **Anonymous functions**: Should we hoist `let foo = fn() {}`?
   - Current plan: No, only named `fn foo() {}` syntax

4. **Scope of hoisting**: Only block-level or expression-level?
   - Current plan: Block-level only (matches JavaScript)

5. **Error recovery**: How aggressive should resolver be?
   - Current plan: Continue resolution, mark errors, produce partial results

6. **Arrow grammar vs lowering validation**:
   - Parser allows `x -> anything` (creates Arrow node)
   - Lowering validates RHS is Call, errors otherwise
   - Error recovery: return left operand
   - Alternative: restrict grammar (more complex parser)

7. **Reserved bytecode slots**:
   - Current compiler reserves slot 1 for loop result
   - Decision: Keep this convention in codegen, or eliminate?
   - If keeping: document in codegen phase where reservation happens
   - If eliminating: VM must be updated

## Success Criteria

- ✅ All existing tests pass with new pipeline (**output** matches, not bytecode)
- ✅ Mutual recursion works without forward declarations
- ✅ Simpler, more maintainable code (no `lowerResolvedLocal` complexity)
- ✅ Better error messages (all errors reported at once)
- ✅ No performance regression (< 10% slower)
- ✅ Foundation ready for LSP implementation
- ✅ Stable slot IDs make debugging easier

## Timeline

- **Phase 1** (parser.lx): 1-2 hours
- **Phase 2** (lower.lx): 2-4 hours
- **Phase 3** (resolve.lx): 1-2 days
- **Phase 4** (codegen.lx): 1-2 days
- **Phase 5** (testing): 1 day
- **Phase 6** (migration): 1 day

Total: ~1 week for complete implementation and testing

## Next Steps

1. ✅ Create this plan document
2. **Phase 1**: Modify parser.lx
   - Add node IDs to all nodes
   - Remove `functionDepth` and `loopDepth` tracking
   - Remove return/break/continue placement validation
   - Keep Arrow nodes (don't transform them)
   - Keep only syntax error reporting
3. **Phase 2**: Create lower.lx
   - Implement AST walking and copying
   - Implement Arrow → Call transformation
   - Maintain provenance mapping (origin table)
   - Handle error cases (arrow without call)
   - Unit tests for lowering transformations
4. **Phase 3**: Create resolve.lx
   - Implement scope management
   - Implement name resolution
   - Implement function hoisting
   - **Implement semantic validation**:
     - Return placement (end of block/file)
     - Break/continue (inside loops only)
     - Undefined variables
     - Duplicate declarations
   - Build node map and side tables
5. **Phase 4**: Create codegen.lx
   - Port bytecode emission from compiler.lx
   - Implement mechanical opcode emission
   - Handle scope cleanup (POP_LOCAL vs CLOSE_UPVALUE)
   - Implement closure handling
6. **Phase 5**: Integration & Testing
   - Wire up full pipeline with proper error flow
   - Implement error reporting with provenance
   - Run regression tests (output equivalence)
   - Test mutual recursion
   - Test arrow operator chains
   - Performance benchmarks
7. **Phase 6**: Deploy and deprecate compiler.lx
