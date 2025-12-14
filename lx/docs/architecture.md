# Lx Compiler Architecture

## Pipeline

```
Source → parser.lx → AST → lower.lx → Lowered AST → resolve.lx → Resolved AST + Side Tables → codegen.lx → Bytecode
```

Each phase has a single responsibility and produces immutable outputs.

## Phases

**parser.lx** — Syntax validation → AST
- Recognizes lx grammar
- Creates AST with node IDs and position spans
- Reports syntax errors only (no semantic validation)
- Preserves full syntax (Arrow nodes, etc.)

**lower.lx** — Desugaring → Canonical AST
- Pure syntactic transformations
- Arrow operator: `x->f(a)` becomes `f(x, a)`
- Creates new AST with fresh node IDs (continuation of module ID space)
- Copies position spans for error reporting
- Future: other syntax sugar (spread operators, etc.)

**resolve.lx** — Binding + Semantic validation
- Name resolution (locals, upvalues, globals)
- Function hoisting for mutual recursion
- Semantic validation (undefined vars, duplicate decls, control flow placement)
- Builds side tables (resolvedNames, scopeInfo, nodes)
- No AST mutation

**codegen.lx** — Mechanical bytecode emission
- Walks resolved AST in source order
- Looks up decisions from side tables
- Emits bytecode, builds chunks
- No semantic analysis (all decisions made by resolver)

## Driver

Orchestrates phases, owns import cache, manages compilation lifecycle.

## Data Structures

**Node IDs** — Auto-increment per module, starts at 1
- Parse: 1..N
- Lower: N+1..M (same module ID space)
- Used to key side tables

**Side Tables** — Per-module, keyed by node ID
- `resolvedNames[nodeId]` → binding info (kind, opcode, slot, etc.)
- `scopeInfo[nodeId]` → scope metadata (locals, upvalues, etc.)
- `nodes[nodeId]` → AST node (O(1) lookup for errors, LSP)

**Import Cache** — Driver-owned, keyed by canonical path
- Lifecycle states: parsing → lowering → resolving → codegen → done | failed
- Circular import detection via status check
- Returns same Function object for module path (enables REF chunk dedup)

## Design Goals

1. **Enable mutual recursion** — Eliminate forward declarations via function hoisting
2. **Better error reporting** — Collect all semantic errors before codegen
3. **LSP foundation** — Node IDs + side tables enable fast position-based queries
4. **Preserve object format** — Compatible with existing objbuilder/objloader
5. **Maintainability** — Clear phase separation, testable in isolation

## Non-Goals

- **Type checking** — Not yet; resolver handles binding and control flow only
- **Bytecode-level compatibility** — Semantic equivalence (same output), not same bytecode
- **Optimization passes** — Keep it simple; focus on correctness
- **AST mutation** — Phases consume and produce; no in-place modification

## Migration Strategy

1. Implement new pipeline alongside compiler.lx
2. Test semantic equivalence (output, not bytecode)
3. Switch default to new pipeline
4. Deprecate compiler.lx

## Future Work

- Type inference and checking
- Optimization passes
- Incremental compilation
- LSP server implementation
