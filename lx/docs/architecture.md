# Lx Compiler Architecture

## Pipeline

```
Source
  → parser.lx
  → AST
  → lower.lx
  → Lowered AST
  → anf.lx
  → ANF AST
  → resolve.lx
  → Resolved AST + Side Tables
  → anf-inline.lx (optional, default on)
  → Optimized AST
  → typecheck.lx (optional)
  → Type Information
  → codegen.lx
  → Bytecode + Chunk
  → verify-bytecode.lx
  → Verified Bytecode
  → (a) objbuilder.lx → Object File
  → (b) LSP / tooling outputs
```

Each phase has a **single responsibility** and produces **immutable outputs**.
Later phases consume earlier results but never mutate them.

**Error handling** is centralized in `errors.lx`, which provides unified error
formatting and position resolution across all compilation phases.

---

## Phases

### **parser.lx** — Syntax validation → AST

* Recognizes lx grammar
* Creates AST with node IDs and position spans
* Reports **syntax errors only**
* Preserves full surface syntax (Arrow nodes, etc.)
* **Structural "everything is expression"**: the file root is an implicit `Block`, and both root + `{...}` blocks store `[Expr]` in `Block.expressions` (no separate `[Stmt]`)
* Does **NOT** enforce contextual legality (return/break placement, etc.)

---

### **lower.lx** — Desugaring → Canonical AST

* Pure syntactic transformations
* Arrow operator: `x->f(a)` → `f(x, a)`
* Missing else normalization: `if cond {a}` → `if cond {a} else {nil}`
* Creates new AST with fresh node IDs (continuation of module ID space)
* Copies position spans for accurate diagnostics
* Makes "everything is expression" semantics uniform for later phases
* Future: other syntax sugar (spread, destructuring, for-loops, etc.)

---

### **anf.lx** — Evaluation-order normalization

* Lowers expressions into A-normal form (ANF)
* Introduces temp `let` bindings to preserve left-to-right evaluation
* Makes block expression values explicit without changing semantics
* Does **not** perform semantic analysis or name resolution
* **Mandatory phase** — always runs after lower, before resolve

---

### **resolve.lx** — Binding + Semantic validation

* Name resolution (locals, upvalues, globals)
* Function hoisting for mutual recursion
* **Contextual legality** ("everything is expression" enforcement):
  * `return` placement (end of block/file, inside function)
  * `break`/`continue` placement (inside loops only)
  * Invalid assignment targets
* **Semantic validation**:
  * undefined variables
  * duplicate declarations
  * read-before-initialization
* Builds **side tables**:
  * `resolvedNames`
  * `scopeInfo`
  * `nodes`
* Does **not** mutate the AST

---

### **anf-inline.lx** — ANF temp elimination (optional, default on)

* **Post-resolve optimization** — runs after binding resolution
* Inlines single-use ANF temporary variables (`$anf.N`)
* Uses **binding metadata** (`declaredAt`) instead of name matching
* Conservative safety predicate:
  * Only inlines pure expressions (literals, identifiers, arithmetic, etc.)
  * Skips function calls, assignments, and other side-effecting operations
* **Binder-aware**:
  * Immune to shadowing bugs
  * Immune to scope capture errors
  * Correct by construction
* Reduces stack pressure by eliminating unnecessary `UNWIND` operations
* Does **not** change semantics — only reduces intermediate bindings
* Can be disabled via driver option: `withAnfInline: false`

**Algorithm:**
1. Collect all ANF temp Let nodes (names starting with `$anf.`)
2. Count uses by binding identity (via `resolvedNames[nodeId].declaredAt`)
3. For each single-use safe temp: substitute RHS into usage site and delete Let
4. Validate: no orphaned temp references remain

**Design rationale:**
Following the principle that **any pass manipulating bindings must be binder-aware**,
`anf-inline` runs post-resolve to leverage binding metadata rather than implementing
its own scope tracking. This aligns with how production compilers (Chez, OCaml) handle
administrative let elimination.

---

### **typecheck.lx** — Static analysis (monomorphic, best-effort)

* Infers stable types for:

  * variables
  * functions
  * records
  * arrays
* Detects incompatible usages:

  * shape mismatches
  * conflicting assignments
  * inconsistent call sites
* Supports closures (captured variables share TypeVars)
* Flow-insensitive, local, fast
* Produces:

  * `types[nodeId]`
  * structured type diagnostics
* **No runtime or codegen impact**

> This phase is designed to support **tooling and IDE features** independently
> of backend code generation.

---

### **codegen.lx** — Mechanical bytecode emission

* Walks resolved AST in **source order**
* Looks up decisions from side tables
* Emits bytecode and builds chunks
* Tracks **nodeId for each bytecode instruction** (`chunk.nodeIds[]`)
* No semantic analysis
* No reordering or deduplication
* All invariants enforced by resolver

---

### **verify-bytecode.lx** — Stack discipline validation

* Validates bytecode stack operations
* Ensures balanced stack effects for all execution paths
* Detects stack underflow/overflow
* Uses dataflow analysis (worklist algorithm)
* Reports errors with **nodeId** (resolved via `chunk.nodeIds[]`)
* Returns: `{ success: bool, errors: [...] }`
* **Mandatory phase** — compilation fails if verification fails

---

### **errors.lx** — Error handling utilities

* Centralizes all error formatting and reporting
* **buildNodesIndex(ast)** — builds complete `{ nodeId: node }` map
  * Uses dynamic property traversal with `keys(node)`
  * Filters metadata properties (id, type, line, col, etc.)
  * O(1) lookup for any node ID
  * Lazy — only built when errors occur
* **resolveNodePosition(nodeId, result)** — maps nodeId to position
  * Follows origin chains: ANF → lowered → parser
  * Returns `{ filename, line, col }`
* **formatError(err, result)** — formats single error to string
* **printErrors(errors, result)** — prints to stderr
* **collectErrors(result)** — gathers errors from all phases

**Error Structure:**
* Parser: pre-formatted strings (e.g., `"[file:L1:C5] message"`)
* All semantic phases: `{ nodeId, message, severity: "error" }`

---

## Driver

The driver:

* orchestrates phase execution
* owns the import cache
* manages module compilation lifecycle
* provides callbacks for recursive imports

---

## Data Structures

### **Node IDs**

* Auto-increment per module, starting at 1
* Parse: `1..N`
* Lower: `N+1..M` (same module ID space)
* ANF: `M+1..K` (same module ID space)
* Used as stable keys across all side tables and tooling

---

### **Origin Chains**

Lowering and ANF phases create new nodes and track their provenance:

```
ANF node (id: 31)
  → lowerResult.origin[31] = 20
  → loweredNode (id: 20)
    → lowerResult.origin[20] = 7
    → parserNode (id: 7)
      → has position: { filename, line, col }
```

**Error reporting** follows origin chains backward to find the original
parser node with position information:

1. Error occurs in resolve phase → reports nodeId from ANF AST
2. `errors.resolveNodePosition()` follows chain: ANF → lower → parser
3. Looks up final parser node to extract source position
4. Formats error: `[file.lx:3:6] Variable 'x' already declared`

---

### **Side Tables** (per module, keyed by node ID)

* `resolvedNames[nodeId]`

  * binding kind
  * opcodes
  * slot / upvalue indices

* `scopeInfo[nodeId]`

  * locals (ordered)
  * upvalues
  * hoisting metadata

* `nodes[nodeId]` (in resolve phase)

  * **Partial** AST node lookup
  * Contains nodes visited during main resolution traversal
  * May not include all nodes (e.g., nested identifiers in `let` name properties)

* **Complete node index** (built by `errors.buildNodesIndex()`)

  * **Complete** mapping of all nodeIds to nodes
  * Built on-demand when formatting errors
  * Traverses entire AST with dynamic property iteration
  * Guaranteed O(1) lookup for any nodeId in the AST

---

### **Import Cache**

* Driver-owned
* Keyed by canonical absolute path
* Lifecycle:

  ```
  parsing → lowering → resolving → typechecking → codegen → done | failed
  ```
* Circular imports detected via status checks
* Same module path returns the same Function object
  (enables REF chunk deduplication)

---

## Design Goals

1. **Enable mutual recursion**

   * Eliminate forward declarations via function hoisting

2. **Better error reporting**

   * Unified error structure across all phases
   * Precise source positions via nodeId tracking and origin chains
   * Centralized error formatting in `errors.lx`
   * Deterministic node lookup with complete AST indexing

3. **Tooling-first architecture**

   * Resolve + typecheck form a complete foundation for LSP, IDEs, and static tools

4. **Preserve object format**

   * Compatible with existing `objbuilder` / `objloader`

5. **Maintainability**

   * Clear phase separation
   * Deterministic behavior
   * Each phase testable in isolation

---

## Non-Goals

* **Polymorphic type inference**
* **Perfect soundness**
* **Aggressive optimization** (only conservative, semantics-preserving passes)
* **AST mutation** (phases create new ASTs, never mutate input)
* **Bytecode-level equivalence** (semantic equivalence only)

---

## Migration Strategy

1. Implement new pipeline alongside `compiler.lx`
2. Validate semantic equivalence
3. Adopt new pipeline as default
4. Deprecate legacy compiler incrementally

---

## Product Lines

Lx supports **multiple consumers** of the same frontend pipeline:

### 1. Compiler

```
parser → lower → anf → resolve → anf-inline → codegen → verify-bytecode → objbuilder → runtime
                          ↓
                    errors.lx (error formatting & reporting)
```

### 2. Tooling / LSP

```
parser → lower → anf → resolve → anf-inline → typecheck → diagnostics / IDE features
                          ↓                       ↓
                    errors.lx (error formatting & reporting)
```

These paths share the same frontend phases, error handling, and guarantees.

---

## Error Handling Flow

All compilation phases follow a consistent error handling pattern:

1. **Error Detection** — Each phase detects its own class of errors
   * Parser: syntax errors
   * Lower/ANF: transformation errors (rare)
   * Resolve: semantic errors (undefined vars, duplicates, etc.)
   * Typecheck: type incompatibilities
   * Codegen: bytecode generation errors
   * Verify: stack discipline violations

2. **Error Recording** — Errors stored in standardized format
   * Parser: pre-formatted strings with position
   * Semantic phases: `{ nodeId, message, severity }`

3. **Error Collection** — `errors.collectErrors(result)` gathers from all phases

4. **Error Formatting** — `errors.formatError(err, result)` resolves positions
   * Follows origin chains for semantic errors
   * Uses `buildNodesIndex()` for O(1) node lookup

5. **Error Reporting** — `errors.printErrors(errors, result)` outputs to stderr

---

## Future Work

* Additional optimization passes:
  * Dead code elimination
  * Constant folding
  * Inline expansion for small functions
* Incremental compilation
* Signature files for modules
* Richer type narrowing
* Full LSP server implementation
