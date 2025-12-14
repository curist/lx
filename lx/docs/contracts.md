# Compiler Invariants and Contracts

This document captures **MUST** and **MUST NOT** rules that ensure correctness.

## Phase Contracts

### Parser

- ✅ MUST assign unique node IDs within module (monotonic, starts at 1)
- ✅ MUST preserve full syntax (Arrow nodes, etc.)
- ✅ MUST report only syntax errors (grammar violations)
- ❌ MUST NOT perform semantic validation (scopes, name resolution, control flow)
- ❌ MUST NOT track functionDepth or loopDepth

### Lower

- ✅ MUST create new AST with fresh node IDs (continue from `parseResult.nextNodeId`)
- ✅ MUST copy position spans from original nodes to new nodes
- ✅ MUST maintain `origin[newId] = oldId` map for tooling
- ✅ MUST validate Arrow RHS is Call expression
- ❌ MUST NOT perform semantic analysis (just syntactic transforms)
- ❌ MUST NOT create new node IDs outside module ID space

### Resolve

- ✅ MUST use canonical paths for import cache keys
- ✅ MUST build `nodes` map from lowered AST (for O(1) lookup)
- ✅ MUST perform all semantic validation (before typecheck / codegen)
- ✅ MUST track hoisting participants and enforce ordering rules
- ❌ MUST NOT mutate AST
- ❌ MUST NOT emit bytecode
- ❌ MUST NOT pre-materialize function objects

### Typecheck

- ✅ MUST run after resolve and before codegen
- ✅ MUST consume resolved AST and side tables (read-only)
- ✅ MUST build a per-module type environment
- ✅ MUST infer monomorphic types using best-effort unification
- ✅ MUST share TypeVars across closure boundaries (captured variables)
- ✅ MUST detect incompatible constraints and record type errors
- ✅ MUST continue analysis after errors (error-tolerant)
- ✅ MUST produce type side tables keyed by node ID
- ❌ MUST NOT mutate AST
- ❌ MUST NOT affect name resolution, hoisting, or scope structure
- ❌ MUST NOT emit bytecode or runtime artifacts
- ❌ MUST NOT be required for codegen correctness

### Codegen

- ✅ MUST traverse AST in source order (no reordering)
- ✅ MUST emit closures only when compiling AST function nodes
- ✅ MUST set `func.name = ""` for module root
- ✅ MUST set `func.name = <actual name>` for user functions (never "")
- ✅ MUST use canonical path for `chunk.filename`
- ❌ MUST NOT perform semantic checks (all errors caught in resolver)
- ❌ MUST NOT reorder, deduplicate, or intern `OBJ_FUNCTION` constants

### Driver

- ✅ MUST canonicalize paths before caching (use `resolvePath()`)
- ✅ MUST use same canonical path across all phases
- ✅ MUST detect circular imports via cache status check
- ✅ MUST preserve import cache across module compilations

## Hoisting & Ordering

### What Gets Hoisted

- ✅ Named function declarations: `fn a() {}`
- ❌ Let bindings: `let a = fn() {}`
- ❌ Anonymous functions

### Ordering Rules

**Inside function bodies**:
- ✅ MAY reference any hoisted function in the block (mutual recursion allowed)
- No ordering restrictions

**At block level** (stricter):
- ✅ MAY call hoisted function only if `exprIndex >= lastHoistedFunctionIndex`
- This ensures entire mutual recursion group is initialized before any call
- Prevents runtime crashes from partially-initialized slots

**Examples**:

```lx
// ✅ LEGAL - mutual recursion in bodies, call after all decls
{
  fn a() { b() }  // OK: b hoisted for function bodies
  fn b() { a() }  // OK: a hoisted for function bodies
  a()             // OK: after lastHoistedFunctionIndex
}

// ❌ ILLEGAL - block-level call before all hoisted functions declared
{
  fn a() { b() }
  a()             // Error: exprIndex < lastHoistedFunctionIndex
  fn b() { a() }
}
```

### Runtime Invariant

- Hoisted function references resolve via slot lookup at call time
- Slots populated when `fn a(){}` declaration executes
- Block-level ordering rule guarantees all slots initialized before any call

## Object Format Compatibility

### Function Ordering Invariant

**Critical**: `objbuilder.lx` walks `chunk.constants` in list order and assumes i-th `OBJ_FUNCTION` constant → i-th non-main chunk.

**Rules**:

1. **Constant pool append-only for functions**
   - `makeConstant(OBJ_FUNCTION(...))` MUST append, never search/deduplicate
   - Each function literal/declaration gets unique constant slot
   - Future constant interning MUST exclude `OBJ_FUNCTION`

2. **One AST function node → One unique Function object**
   - Each `fn a(){}` or `fn(){}` produces exactly one `Function` object
   - Exception: module roots reused via REF chunks
   - No duplicate emissions for same AST node

3. **Source-order traversal**
   - Codegen MUST compile `block.expressions[]` in ascending index order
   - No statement reordering during lowering or resolve
   - Hoisting is semantic-only (doesn't change emission order)

4. **Closure emission only at AST traversal**
   - `OP.CLOSURE` emitted only when compiling function AST node
   - Not during prescan, not during finalization
   - `emitClosureForFunction(node)` is the ONLY site allowed to call `makeConstant(OBJ_FUNCTION(...))`

**What breaks if violated**:
- Dedup → reduced constant count → loader patching failure
- Reordering → wrong chunk index mapping → wrong functions called
- Duplicate emission → chunk count mismatch → loader error

### Module Chunk Classification

**Critical**: `objbuilder.lx` treats `func.name == ""` as module chunk eligible for dedup and REF emission.

**Rules**:

1. **Exactly one module root per file**
   - `func.name == ""` (ONLY module root!)
   - `func.arity == 0`
   - `chunk.filename == <canonical module path>`

2. **Named user functions have non-empty names**
   - `func.name == <identifier>` (e.g., "foo", "bar")
   - Never empty string

3. **Anonymous functions MUST have non-empty names**
   - `func.name == "fn"` (current behavior: preserve exactly!)
   - NEVER empty string
   - **Rationale**: Prevents misclassification as module chunk

4. **Stable path canonicalization**
   - Same module path → same canonical string
   - Enables `builtModuleCache[filename]` dedup
   - Prevents duplicate ACTUAL chunks instead of REF

**Enforcement**:
```lx
// In codegen - compiler bug guardrails
if compilingModuleRoot and func.name != "" {
  panic("Compiler bug: module root must have name == \"\", got: " + func.name)
}
if !compilingModuleRoot and func.name == "" {
  panic("Compiler bug: non-module function must have non-empty name (named or \"fn\" for anonymous)")
}
```

**Current compiler behavior** (preserve exactly):
- Module root: consumes no name token → `func.name == ""`
- Named function: `fn foo(){}` → `parser.previous.lexeme == "foo"` → `func.name == "foo"`
- Anonymous function: `fn(){}` → `parser.previous.lexeme == "fn"` → `func.name == "fn"`

**What breaks if violated**:
- Empty name on user function → misclassified as module → wrong dedup/REF
- Multiple empty names per file → duplicate module chunks → wrong REF indexing
- Inconsistent paths → dedup failure → duplicate chunks

### Import Execution Semantics

**Module root**:
- Zero-arity function
- Executes module body
- Returns exported value (module table/value)

**Import compilation**:
```
import "path"  →  OP.CLOSURE <const(Function(module))>
                  OP.CALL 0
```

**Import caching**:
- Driver ensures same `Function` object for same module path
- Enables `builtModuleCache` dedup → REF chunks
- Multiple imports → multiple `OP.CLOSURE` + `OP.CALL 0`, same Function object

## Node IDs

- ✅ MUST be unique within module (not globally)
- ✅ MUST be monotonic (each new node increments counter)
- ✅ Parse uses 1..N, lower uses N+1..M (same module ID space)
- ✅ LSP queries use `(modulePath, nodeId)` as identity
- ❌ MUST NOT reuse IDs within a module
- ❌ MUST NOT share IDs across modules

## Side Tables

- ✅ MUST be per-module (not global)
- ✅ `resolvedNames` keys are from lowered/resolved AST
- ✅ `nodes` map built during resolve phase
- ✅ Resolver decides opcodes (GET_LOCAL vs GET_UPVALUE, etc.)
- ❌ Codegen MUST NOT make binding decisions (just emit from side tables)

### Type Side Tables (Typecheck)

- `types[nodeId]` → inferred Type
- `typeVars[nodeId]` → underlying TypeVar (for debugging / tooling)
- `typeErrors[]` → structured diagnostics

Rules:
- Type tables MUST be per-module
- Type tables MUST use node IDs from lowered AST
- Resolver side tables MUST NOT depend on type tables
- Codegen MUST NOT read type tables

## Error Handling

- ✅ Resolver collects all semantic errors before typecheck/codegen
- ✅ Typecheck collects all type errors without aborting compilation
- ✅ Error reporting uses lowered node positions (spans already copied)
- ✅ `origin` map used for LSP/tooling only (not diagnostics)
- ❌ Codegen MUST NOT run if parser/lower/resolve had errors
- ⚠️ Codegen MAY run even if typecheck produced errors

## Semantic Validation (Resolver)

### Required Checks

- Undefined variables
- Duplicate declarations in same scope
- Read-before-initialization (`depth == -1` check)
- Invalid assignment targets
- `return` placement (end of block/file only)
- `break`/`continue` placement (inside loops only)
- Hoisting ordering violations

### NOT Checked (Yet)

- Type correctness
- Exhaustiveness
- Unreachable code

## VM Invariants (Codegen)

- ✅ Scope cleanup in reverse declaration order
- ✅ Emit `POP_LOCAL` or `CLOSE_UPVALUE` based on `isCaptured` flag
- ✅ Upvalue list ordering matches closure capture order
- ✅ Stable slot IDs (monotonic per function, no compression)

## Import Cache Lifecycle

**Status states**:
`parsing | lowering | resolving | typechecking | codegen | done | failed`

**Rules**:
- Cache entry created when compilation starts (status = "parsing")
- Status updated as phases complete
- Typechecking MAY be skipped by the driver
- Circular detection: if status is not "done" or "failed" → circular import
- Failed compilations cache errors, don't retry
- Cache key is absolute canonical path

## Semantic vs Type Validation

**Semantic validation (resolver)**:
- Enforces language correctness
- Errors are fatal
- Prevents codegen

**Type validation (typecheck)**:
- Best-effort static analysis
- Errors are advisory by default
- Intended for tooling, IDEs, CI, and gradual adoption
- Does not affect runtime semantics

This separation is intentional.

## Testing Requirements

See `tests.md` for executable spec.

Key categories:
- Function ordering preservation
- Module chunk classification
- Hoisting safety (edge cases)
- Import deduplication
- Error collection completeness
- Type inference correctness
- Record shape freezing
- Monomorphic function inference
- Closure type propagation
- Error recovery after type conflicts
