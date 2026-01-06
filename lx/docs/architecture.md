# Lx Compiler Architecture

## Pipeline

```
Source
  → passes/frontend/parser.lx
  → AST
  → passes/frontend/lower.lx
  → Lowered AST
  → passes/frontend/anf.lx
  → ANF AST
  → passes/frontend/resolve.lx
  → Resolved AST + Side Tables
  → passes/frontend/anf-inline.lx (optional, default on)
  → Optimized AST
  → passes/frontend/lower-intrinsics.lx (optional, default on)
  → Optimized AST
  → passes/frontend/typecheck.lx (optional)
  → Type Information
  → select.lx (opcode selection policy)
  → passes/backend/codegen.lx
  → Bytecode + Chunk
  → passes/backend/verify-bytecode.lx
  → Verified Bytecode
  → (a) objbuilder.lx → Object File
  → (b) LSP / tooling outputs
```

## Source Layout

See `lx/docs/source-layout.md` for the intended long-term folder structure (passes/IR split, typed backend boundary, and entrypoint/tooling conventions).

Each pass has a **single responsibility**.
Most passes treat their inputs as immutable; a small number of optimization passes
(notably `anf-inline.lx`) intentionally mutate the AST in-place for performance.

**Error handling** is centralized in `errors.lx`, which provides unified error
formatting and position resolution across all compilation passes.

---

## Passes

### **parser.lx** — Syntax validation pass → AST

* Recognizes lx grammar
* Creates AST with node IDs and position spans
* Reports **syntax errors only**
* Preserves full surface syntax (Arrow nodes, etc.)
* **Structural "everything is expression"**: the file root is an implicit `Block`, and both root + `{...}` blocks store `[Expr]` in `Block.expressions` (no separate `[Stmt]`)
* Does **NOT** enforce contextual legality (return/break placement, etc.)

---

### **lower.lx** — Desugaring pass → Canonical AST

* Pure syntactic transformations
* Arrow operator: `x->f(a)` → `f(x, a)`
* Missing else normalization: `if cond {a}` → `if cond {a} else {nil}`
* Creates new AST with fresh node IDs (continuation of module ID space)
* Copies position spans for accurate diagnostics
* Makes "everything is expression" semantics uniform for later passes
* Future: other syntax sugar (spread, destructuring, for-loops, etc.)

---

### **anf.lx** — Evaluation-order normalization pass

* Lowers expressions into A-normal form (ANF)
* Introduces temp `let` bindings to preserve left-to-right evaluation
* Makes block expression values explicit without changing semantics
* Does **not** perform semantic analysis or name resolution
* Does **not** introduce IntrinsicCall fast paths (those are handled post-resolve)
* Enabled by default (can be disabled via driver option `withAnf: false`)

---

### **resolve.lx** — Binding + semantic validation pass

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

### **anf-inline.lx** — ANF temp elimination pass (optional, default on)

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

### **lower-intrinsics.lx** — Intrinsic lowering pass (optional, default on)

* **Post-resolve optimization** — runs after binding resolution (and after ANF)
* Rewrites select generic AST patterns into `IntrinsicCall` nodes that map to specialized opcodes
  * Example: `x % 8` → `IntrinsicCall("mod_const", [x], modulus=8)`
  * Example: `x == 3` → `IntrinsicCall("eq_const", [x], compareTo=3)`
* Mutates the AST in-place and **preserves `node.id`** so resolver/typecheck side tables remain valid
* Keeps `anf.lx` scoped to evaluation-order normalization (ANF) rather than performance shaping

---

### **typecheck.lx** — Static analysis pass (monomorphic, best-effort)

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

> This pass is designed to support **tooling and IDE features** independently
> of backend code generation.

---

### **select.lx** — Opcode selection policy module

* Centralizes all policy decisions for bytecode generation
* Separates "what to emit" (policy) from "how to emit" (mechanism)
* Pure functions that query analysis facts and return decisions
* No bytecode emission or state mutation
* Responsibilities:
  * **Opcode selection**: Choose specialized opcodes based on type facts
    * Unary ops: `NEGATE` vs `NEGATE_INT`
    * Binary ops: `ADD` vs `ADD_INT` vs `ADD_STR` vs `ADD_NUM`
  * **Micro-optimizations**: Recognize patterns for superinstructions
    * `v or CONST` → `COALESCE_CONST`
    * `i = i + n` → `ADD_LOCAL_IMM`
  * **Facts abstraction**: Queries `gen.fastcheck` (today) or `gen.rep` (future)
* Enables evolution:
  * Swap analysis passes without touching codegen
  * Add new optimizations in one place
  * Test policy decisions independently

**API Functions:**
* `unaryOpcode(gen, node)` → OP
* `binaryCode(gen, node)` → OP | [OP, OP]
* `logicalOrCoalesceConst(gen, node)` → plan | nil
* `assignmentSuper(gen, node, mode)` → plan | nil

---

### **codegen.lx** — Mechanical bytecode emission pass

* Walks resolved AST in **source order**
* Delegates policy decisions to `select.lx`
* Emits bytecode and builds chunks
* Tracks **nodeId for each bytecode instruction** (`chunk.nodeIds[]`)
* Responsibilities:
  * Evaluate operands in correct order
  * Query select module for opcode choices
  * Emit bytes and manage constant pool
  * Maintain stack and local slot discipline
  * Patch jumps and handle control flow
* No semantic analysis or type reasoning
* No reordering or deduplication
* All invariants enforced by resolver

**Architecture:**
```
codegen:
  1. Compile operands (recursive)
  2. Ask select: "What opcode should I use?"
  3. Emit the opcode (mechanical)

select:
  1. Read type facts from gen.fastcheck/gen.rep
  2. Match patterns (structural)
  3. Return decision (pure, no side effects)
```

---

### **verify-bytecode.lx** — Stack discipline validation pass

* Validates bytecode stack operations
* Ensures balanced stack effects for all execution paths
* Detects stack underflow/overflow
* Uses dataflow analysis (worklist algorithm)
* Reports errors with **nodeId** (resolved via `chunk.nodeIds[]`)
* Returns: `{ success: bool, errors: [...] }`
* **Mandatory pass** — compilation fails if verification fails

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
* **collectErrors(result)** — gathers errors from all passes

**Error Structure:**
* Parser: pre-formatted strings (e.g., `"[file:L1:C5] message"`)
* All semantic passes: `{ nodeId, message, severity: "error" }`

---

## Driver

The driver:

* orchestrates pass execution
* owns the import cache
* manages module compilation lifecycle
* provides callbacks for recursive imports

---

## Pipelines (Profiles)

Lx supports multiple **pipelines** (a.k.a. profiles) that select a **known-good**
sequence of passes for a given product line (compiler vs tooling).

This is intentional: allowing arbitrary pass subsets quickly creates a
combinatorial space that is hard to test and reason about. Instead, Lx prefers:

* a small number of curated pipelines (e.g. `tooling`, `O0`, `O2`, `typed-backend`)
* pass-level toggles used internally by those pipelines (not as a public API)

Within a pipeline:

* passes that establish correctness invariants (e.g. binding resolution) are mandatory
* optional passes are limited to semantics-preserving optimizations or instrumentation

Future direction: a pass manager can enforce this mechanically via
`requires`/`provides` metadata, rejecting invalid combinations and auto-inserting
prerequisites when appropriate.

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

Lowering and ANF passes create new nodes and track their provenance:

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

1. Error occurs in resolve pass → reports nodeId from ANF AST
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

* `nodes[nodeId]` (in resolve pass)

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
* Lifecycle:

  ```
  compiling → done | failed
  ```
* Circular imports detected via status checks
* Keyed by the import path string (as written in source, after any caller-level path mapping)

---

## Design Goals

1. **Enable mutual recursion**

   * Eliminate forward declarations via function hoisting

2. **Better error reporting**

   * Unified error structure across all passes
   * Precise source positions via nodeId tracking and origin chains
   * Centralized error formatting in `errors.lx`
   * Deterministic node lookup with complete AST indexing

3. **Tooling-first architecture**

   * Resolve + typecheck form a complete foundation for LSP, IDEs, and static tools

4. **Preserve object format**

   * Compatible with existing `objbuilder` / `objloader`

5. **Maintainability**

   * Clear pass separation
   * Deterministic behavior
   * Each pass testable in isolation

6. **Evolvable backend architecture**

   * **Policy/mechanism separation**: `select.lx` decouples opcode choice from emission
   * **Analysis-agnostic codegen**: Swap type/representation analysis without touching emission code
   * **Composable optimizations**: Add micro-optimizations in select module, not scattered through codegen
   * **Local reasoning**: Policy decisions have no side effects, emission logic has no semantic reasoning

### Backend Architecture Rationale

The separation of `select.lx` from `codegen.lx` addresses three key problems:

1. **Scalability**: Adding new analysis passes (fixnum flow, enum exhaustiveness, etc.)
   should not require threading logic throughout codegen. The select layer provides a
   stable interface between analysis and emission.

2. **Correctness**: It is hard to reason about correctness when codegen both decides
   semantics and emits bytecode. Separating concerns makes both easier to verify.

3. **Evolution cost**: Replacing or upgrading analysis passes (e.g., fastcheck → rep)
   becomes a localized change to select.lx, not a risky multi-file rewrite.

This follows the principle that **each module should have exactly one reason to change**:
* Analysis passes change when we improve type/representation inference
* Select changes when we add new optimizations or opcode specializations
* Codegen changes when we modify bytecode format or stack discipline

---

## Non-Goals

* **Polymorphic type inference**
* **Perfect soundness**
* **Aggressive optimization** (only conservative, semantics-preserving passes)
* **Pervasive AST mutation** (most passes create new ASTs; a few targeted opts may mutate in-place)
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
parser → lower → anf → resolve → anf-inline → select → codegen → verify-bytecode → objbuilder → runtime
                          ↓                       ↑
                    errors.lx              (policy layer)
                 (error formatting
                  & reporting)
```

### 2. Tooling / LSP

```
parser → lower → anf → resolve → anf-inline → typecheck → diagnostics / IDE features
                          ↓                       ↓
                    errors.lx (error formatting & reporting)
```

These paths share the same frontend passes, error handling, and guarantees.

---

## Error Handling Flow

All compilation passes follow a consistent error handling pattern:

1. **Error Detection** — Each pass detects its own class of errors
   * Parser: syntax errors
   * Lower/ANF: transformation errors (rare)
   * Resolve: semantic errors (undefined vars, duplicates, etc.)
   * Typecheck: type incompatibilities
   * Codegen: bytecode generation errors
   * Verify: stack discipline violations

2. **Error Recording** — Errors stored in standardized format
   * Parser: pre-formatted strings with position
   * Semantic passes: `{ nodeId, message, severity }`

3. **Error Collection** — `errors.collectErrors(result)` gathers from all passes

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
