# Lx Compiler Architecture

## Pipeline

```
Source
  → parser.lx
  → AST
  → lower.lx
  → Lowered AST
  → anf.lx (optional)
  → resolve.lx
  → Resolved AST + Side Tables
  → typecheck.lx
  → (a) codegen.lx → Bytecode
  → (b) LSP / tooling outputs
```

Each phase has a **single responsibility** and produces **immutable outputs**.
Later phases consume earlier results but never mutate them.

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

### **anf.lx** — Evaluation-order normalization (optional)

* Lowers expressions into A-normal form (ANF)
* Introduces temp `let` bindings to preserve left-to-right evaluation
* Makes block expression values explicit without changing semantics
* Does **not** perform semantic analysis or name resolution

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
* No semantic analysis
* No reordering or deduplication
* All invariants enforced by resolver

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
* Used as stable keys across all side tables and tooling

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

* `nodes[nodeId]`

  * AST node lookup (O(1))
  * enables diagnostics and LSP queries

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

   * Collect and report semantic and type errors with precise spans

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
* **Optimization passes**
* **AST mutation**
* **Bytecode-level equivalence**
  (semantic equivalence only)

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
parser → lower → resolve → typecheck → codegen → runtime
```

### 2. Tooling / LSP

```
parser → lower → resolve → typecheck → diagnostics / IDE features
```

These paths share the same frontend phases and guarantees.

---

## Future Work

* Optimization passes
* Incremental compilation
* Signature files for modules
* Richer type narrowing
* Full LSP server implementation
