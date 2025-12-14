# Extraction Map: plan-archive.md → New Docs

This document maps valuable content from `plan-archive.md` to new documentation locations.

## algorithms.md (Create this)

### Arrow Lowering Algorithm
**Source**: plan-archive.md lines 290-365
**Content**:
- `lowerArrow()` pseudocode
- `lowerCall()` pseudocode
- Position span copying logic
- Error recovery pattern

### Hoisting Prescan Algorithm
**Source**: plan-archive.md lines 710-825
**Content**:
- `resolveBlock()` two-phase algorithm
- `resolveIdentifier()` with ordering check
- `resolveFunction()` body context
- Runtime invariant explanation

### Import Cache Lifecycle
**Source**: plan-archive.md lines 1048-1143
**Content**:
- `compileModule()` driver flow
- Circular import detection logic
- Status state transitions
- Cache key canonicalization

### Error Propagation Flow
**Source**: plan-archive.md lines 951-1042
**Content**:
- Phase-by-phase error collection
- Error reporting with position lookup
- `addError()` pattern
- `reportErrors()` with sorting

### Scope Cleanup Algorithm
**Source**: plan-archive.md lines 1051-1100
**Content**:
- `endScope()` reverse-order logic
- POP_LOCAL vs CLOSE_UPVALUE decision
- isCaptured flag usage

## tests.md (Create this)

### Order Preservation Tests
**Source**: plan-archive.md lines 197-239
**Content**:
- Sequential functions test
- Nested functions test
- Mutual recursion test
- Illegal pre-call test

### Hoisting Edge Cases
**Source**: plan-archive.md lines 438-468
**Content**:
- Legal mutual recursion examples
- Illegal pre-call examples
- Let vs fn hoisting
- Transitive reference edge case

### Unit Test Categories
**Source**: plan-archive.md lines 1537-1589
**Content**:
- Parser: node IDs, syntax errors only
- Lower: arrow chains, error recovery
- Resolve: hoisting, semantic errors, imports
- Codegen: output equivalence, VM invariants
- Integration: full pipeline tests

### Regression Test Strategy
**Source**: plan-archive.md lines 1590-1600
**Content**:
- Semantic equivalence (not bytecode)
- Performance benchmarks
- Edge case coverage

## phases/parser.md (Create this)

### Input/Output
**Source**: plan-archive.md lines 935-975
**Content**:
- `parse(src, filename)` signature
- Return value: `{ success, ast, errors, nextNodeId }`
- Error types: syntax only

### Implementation Tasks
**Source**: plan-archive.md lines 523-581
**Content**:
- Add nodeIdCounter
- Modify Node() constructor
- Remove functionDepth/loopDepth
- Remove return/break/continue validation
- Specific lines to delete (with examples)

### Node Constructor Pattern
**Source**: plan-archive.md lines 593-629
**Content**:
- `nextNodeId()` function
- `Node(type, filename, token)` with ID assignment
- Position calculation logic

## phases/lower.md (Create this)

### Input/Output
**Source**: plan-archive.md lines 267-288
**Content**:
- `lower(ast, opts)` signature
- opts: `{ startNodeId }`
- Return value: `{ success, ast, origin, errors, nextNodeId }`

### Implementation Tasks
**Source**: plan-archive.md lines 582-681
**Content**:
- Arrow → Call transformation
- Position span copying
- Provenance map maintenance
- Error handling for invalid arrows

### Lowering State
**Source**: plan-archive.md lines 269-288
**Content**:
- Lowerer structure
- nextId counter (continuation)
- origin map building
- errors array

## phases/resolve.md (Create this)

### Input/Output
**Source**: plan-archive.md lines 976-1000
**Content**:
- `resolve(ast, opts)` signature
- opts: `{ importCache, compileModule }`
- Return value: `{ success, ast, nodes, resolvedNames, scopeInfo, errors }`

### Side Tables Produced
**Source**: plan-archive.md lines 1136-1535
**Content**:
- resolvedNames schema (with opcodes)
- scopeInfo schema (with locals ordering)
- nodes map (id → node)
- errors array (structured)

### Scope Structures
**Source**: plan-archive.md lines 1051-1100
**Content**:
- Scope() constructor
- Local() structure
- Upvalue() structure
- HoistedFunction() structure

### Key Functions
**Source**: plan-archive.md lines 1027-1050
**Content**:
- resolveProgram()
- resolveExpr() with context
- resolveBlock() two-phase
- resolveName() scope chain walk
- resolveImport() with caching
- declareLocal()
- markCaptured()

### Semantic Validation Checklist
**Source**: plan-archive.md lines 688-696
**Content**:
- Undefined variables
- Duplicate declarations
- Read-before-initialization
- Invalid assignment targets
- return placement
- break/continue placement

## phases/codegen.md (Create this)

### Input/Output
**Source**: plan-archive.md lines 1101-1135
**Content**:
- `codegen(ast, resolveResult, opts)` signature
- opts: `{ main: bool }`
- Return value: `{ success, function }`

### Implementation Tasks
**Source**: plan-archive.md lines 827-836
**Content**:
- Walk resolved AST in source order
- Look up from side tables
- Emit bytecode mechanically
- No semantic decisions

### Key Functions
**Source**: plan-archive.md lines 1107-1135
**Content**:
- compileProgram()
- compileExpr() dispatcher
- compileIdentifier() mechanical emission
- compileFunction() with scope info
- compileBlock() with cleanup

### Mechanical Emission Pattern
**Source**: plan-archive.md lines 1112-1124
**Content**:
- Example: compileIdentifier just emits from resolvedNames
- No logic, just table lookup + emit
- "Codegen should be dumb"

## What NOT to Extract

These are already in architecture.md or contracts.md:

- ❌ Pipeline overview (in architecture.md)
- ❌ Design goals (in architecture.md)
- ❌ Phase contracts/invariants (in contracts.md)
- ❌ Hoisting rules (in contracts.md)
- ❌ Object format constraints (in contracts.md)
- ❌ Module chunk classification (in contracts.md)
- ❌ Migration strategy overview (in architecture.md)
- ❌ Success criteria (in COMPILER_PLAN.md)

## Priority Order for Extraction

1. **HIGH**: tests.md — Executable spec, needed before implementation
2. **HIGH**: algorithms.md — Tricky bits you'll reference during coding
3. **MEDIUM**: phases/resolve.md — Most complex phase, needs detail
4. **LOW**: phases/parser.md, phases/lower.md, phases/codegen.md — Simpler, can reference archive directly

## Estimated Sizes

If extracted:

- algorithms.md: ~400 lines (hoisting, lowering, imports, errors, cleanup)
- tests.md: ~250 lines (test categories, examples, expectations)
- phases/resolve.md: ~200 lines (API, tables, functions, validation)
- phases/parser.md: ~100 lines (API, modifications, patterns)
- phases/lower.md: ~100 lines (API, transformations, state)
- phases/codegen.md: ~100 lines (API, emission, patterns)

Total: ~1150 lines (but optional and separated by concern)
