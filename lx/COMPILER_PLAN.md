# Compiler Migration Plan

**Goal**: Replace single-pass compiler.lx with multi-pass architecture.

**Documentation**: See `docs/` for architecture, contracts, and algorithms.

## Phase Checklist

- [ ] **Phase 1**: Modify parser.lx
  - [ ] Add node ID counter and assignment
  - [ ] Remove `functionDepth` and `loopDepth` tracking
  - [ ] Remove return/break/continue placement validation
  - [ ] Keep Arrow nodes (don't transform)
  - [ ] Test: node IDs unique and sequential

- [ ] **Phase 2**: Implement lower.lx
  - [ ] Arrow → Call transformation
  - [ ] Position span copying
  - [ ] Provenance tracking (`origin` map)
  - [ ] Error handling (invalid arrow usage)
  - [ ] Test: arrow chains, edge cases

- [ ] **Phase 3**: Implement resolve.lx
  - [ ] Scope management (global/function/block/loop)
  - [ ] Name resolution (local/upvalue/global)
  - [ ] Function hoisting with `lastHoistedFunctionIndex` check
  - [ ] Semantic validation (all checks from contracts.md)
  - [ ] Build side tables (resolvedNames, scopeInfo, nodes)
  - [ ] Import handling with circular detection
  - [ ] Test: hoisting edge cases, semantic errors

- [ ] **Phase 4**: Implement codegen.lx
  - [ ] Source-order AST traversal
  - [ ] Mechanical opcode emission (from side tables)
  - [ ] Module root vs user function handling
  - [ ] Append-only constant pool for `OBJ_FUNCTION`
  - [ ] Scope cleanup (POP_LOCAL vs CLOSE_UPVALUE)
  - [ ] Test: output equivalence with compiler.lx

- [ ] **Phase 5**: Integration & Testing
  - [ ] Wire up full pipeline (driver)
  - [ ] Import cache implementation
  - [ ] Error reporting with provenance
  - [ ] Regression tests (semantic equivalence)
  - [ ] Performance benchmarks

- [ ] **Phase 6**: Migration
  - [ ] Run both compilers in parallel
  - [ ] Fix discrepancies
  - [ ] Switch default to new pipeline
  - [ ] Deprecate compiler.lx

## Invariants to Preserve

See `docs/contracts.md` for complete list. Critical ones:

- Function ordering (constant pool append-only)
- Module chunk classification (`func.name == ""`)
- Hoisting safety (block-level calls after `lastHoistedFunctionIndex`)
- Import deduplication (canonical paths, REF chunks)
- Semantic errors before codegen

## Success Criteria

- ✅ All existing tests pass (semantic equivalence, not bytecode)
- ✅ Mutual recursion works without forward declarations
- ✅ Better error messages (all errors reported at once)
- ✅ No performance regression (< 10% slower)
- ✅ Foundation ready for LSP
