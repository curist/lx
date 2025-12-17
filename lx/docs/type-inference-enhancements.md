# Type Inference Enhancement Roadmap

**Status**: Planning
**Created**: 2025-12-17
**Last Updated**: 2025-12-17

## Executive Summary

This document outlines a three-phase enhancement plan to improve lx's type inference system, addressing the limitations discovered in the two-pass inference implementation. The goal is to fully resolve generic parameter types and eliminate "Unbound" TypeVars in common use cases.

**Current State**: Two-pass inference handles forward references and mutual recursion, but cannot infer concrete types for generic function parameters.

**Target State**: Full type inference for record field accesses on parameters, with bidirectional type flow and sophisticated constraint solving.

---

## Background: The Problem

### Current Limitation Example

```lx
fn purifyFunction(func) {  // func: TypeVar T1
  .{
    name: func.name,        // Creates TypeVar T2 for name field
    arity: func.arity,      // Creates TypeVar T3 for arity field
    upvalueCount: func.upvalueCount,  // Creates TypeVar T4
    chunk: .{
      filename: func.chunk.filename,  // Creates TypeVar T5, T6...
      bytecode: func.chunk.bytecode,
      constants: func.chunk.constants,
      lines: func.chunk.lines,
    },
  }
}

// Call site:
fn compile(...) {
  let func = endCompiler()  // Returns concrete type
  return .{
    success: true,
    function: purifyFunction(func)  // TypeVars T2, T3, T4... remain unbound
  }
}
```

**Issue**: Even though `func` at the call site has a concrete type with known field types, the return type of `purifyFunction` still contains unbound TypeVars because:

1. Parameter type inference is **forward-only** (argument → parameter)
2. No mechanism to propagate field type constraints from parameter usage to return type
3. Record field access on TypeVars creates fresh TypeVars that are never unified

---

## Enhancement 1: Bidirectional Type Checking

**Goal**: Propagate expected types downward to guide inference

**Priority**: High (Foundation for other enhancements)
**Complexity**: Medium
**Estimated Effort**: 2-3 weeks
**Dependencies**: None (builds on current two-pass system)

### Overview

Bidirectional type checking flows type information in both directions:
- **Checking mode**: Given an expected type, verify an expression matches
- **Synthesis mode**: Infer the type of an expression without expectations

### Key Concepts

```lx
// Synthesis mode: infer from expression
let x = 42  // Synthesize: Number

// Checking mode: check against expected type
let y: Number = 42  // Check: 42 against Number

// Hybrid: function calls
fn foo(x: Number) -> String { ... }
foo(42)  // Synthesize 42 → Number, Check against parameter type
```

### Implementation Design

#### 1.1 Add Type Annotations (Optional)

```lx
// File: lx/src/parser.lx
// Add optional type annotations to AST nodes

// Variable declarations
let x: Number = 42

// Function parameters and returns
fn foo(x: Number, y: String): Bool {
  x > 0 and len(y) > 0
}

// Currently not implemented - deferred to future phase
// This phase focuses on internal bidirectional flow
```

#### 1.2 Split checkExpr into check and synth

```lx
// File: lx/src/typecheck.lx (lines 2258-2413)

// Current: unified checkExpr
fn checkExpr(checker, node) {
  // ... 150 lines of type synthesis
}

// Proposed: split into two modes
fn synthExpr(checker, node) {
  // Synthesize type from expression (current behavior)
  // Returns inferred type
}

fn checkExpr(checker, node, expectedType) {
  // Check expression against expected type
  // Returns refined type or error

  // Strategy: synth first, then unify with expected
  let synthType = synthExpr(checker, node)
  constrain(checker, synthType, expectedType, node.id,
    "Expression type doesn't match expected type")
  return synthType
}
```

#### 1.3 Update Call Sites Progressively

**Function Bodies** (checking mode):
```lx
fn checkFunction(checker, node) {
  // ...

  // OLD:
  let bodyType = checkExpr(checker, node.body)

  // NEW:
  let bodyType = checkExpr(checker, node.body, returnType)
  // Now body is checked against expected return type
}
```

**Return Statements** (checking mode):
```lx
fn checkReturn(checker, node) {
  // OLD:
  let valueType = checkExpr(checker, node.value)
  constrain(checker, valueType, checker.currentReturnType, ...)

  // NEW:
  let valueType = checkExpr(checker, node.value, checker.currentReturnType)
  // Constraint is implicit in checkExpr
}
```

**Record Literals** (checking mode when expected type is known):
```lx
fn checkHashmap(checker, node, expectedType) {
  // If expectedType is Record, we know which fields to expect
  if expectedType and expectedType.kind == "Record" {
    // Check each field against expected field types
    for each field in expectedType.fields {
      let valueNode = findFieldValue(node, field)
      let valueType = checkExpr(checker, valueNode, expectedType.fields[field])
      // Type flows downward!
    }
  } else {
    // Synthesis mode (current behavior)
    // ...
  }
}
```

#### 1.4 Refactor for Backward Compatibility

**Migration Strategy**:
1. Keep `checkExpr(checker, node)` as wrapper that calls `synthExpr`
2. Add `checkExprAgainst(checker, node, expectedType)` for new behavior
3. Gradually migrate call sites from `checkExpr` to `checkExprAgainst`
4. Once all call sites migrated, rename functions for clarity

### Implementation Phases

**Phase 1A: Core Infrastructure (Week 1)**
- [ ] Create `synthExpr(checker, node)` that wraps current `checkExpr` logic
- [ ] Create `checkExprAgainst(checker, node, expectedType)` that synthesizes then constrains
- [ ] Add wrapper `checkExpr` that calls `synthExpr` for backward compatibility
- [ ] Test: All existing tests pass (no behavior change yet)

**Phase 1B: Function Body Checking (Week 1-2)**
- [ ] Update `checkFunction` to use `checkExprAgainst` for body against return type
- [ ] Update `checkReturn` to use `checkExprAgainst`
- [ ] Update `checkIf` branches to use `checkExprAgainst` when return type known
- [ ] Test: Function return type mismatches caught earlier

**Phase 1C: Record Literal Checking (Week 2)**
- [ ] Update `checkHashmap` to accept optional expected type
- [ ] Implement checking mode for records when expected type is Record
- [ ] Propagate expected field types to field value expressions
- [ ] Test: Record field types match expected types

**Phase 1D: Call Site Refinement (Week 2-3)**
- [ ] Update `checkCall` to use `checkExprAgainst` for arguments
- [ ] Implement subsumption checking (more specific types can be used where general expected)
- [ ] Test: Argument type errors report more specific mismatches

**Phase 1E: Integration & Cleanup (Week 3)**
- [ ] Migrate remaining call sites
- [ ] Remove backward compatibility wrapper once all migrated
- [ ] Performance profiling (should be similar to current)
- [ ] Test: Full test suite passes

### Expected Improvements

**Before**:
```lx
fn foo() {
  return 42
}
// Return type: TypeVar T1 (unified with Number later)
```

**After**:
```lx
fn foo(): Number {  // Explicit or inferred from context
  return 42  // Checked against Number immediately
}
// Return type: Number (known immediately)
```

### Testing Strategy

```lx
// test/typecheck.bidirectional.test.lx

test("bidirectional - function body checked against return type", fn(assert) {
  let result = parseAndTypecheck("
    fn foo(): Number {
      \"oops\"  // Should error: String not Number
    }
  ")
  assert.equal(result.success, false)
  assert.truthy(len(result.errors) > 0)
})

test("bidirectional - record literal checked against expected type", fn(assert) {
  let result = parseAndTypecheck("
    fn makePoint(x: Number, y: Number): .{ x: Number, y: Number } {
      .{ x: x, y: \"oops\" }  // Should error: y expects Number
    }
  ")
  assert.equal(result.success, false)
})
```

---

## Enhancement 2: Constraint Collection & Solving

**Goal**: Separate constraint generation from solving for better type propagation

**Priority**: High (Enables sophisticated inference)
**Complexity**: High
**Estimated Effort**: 4-6 weeks
**Dependencies**: Enhancement 1 (bidirectional checking)

### Overview

Current system uses **eager unification** - constraints are solved immediately as they're generated. This limits type propagation because:
- Once a TypeVar is bound, that binding is final
- No way to accumulate constraints before solving
- Can't perform global constraint optimization

**Constraint collection** separates these phases:
1. **Collection**: Traverse AST, generate constraints, don't solve yet
2. **Solving**: Apply constraint solving algorithm to find satisfying assignment
3. **Substitution**: Replace TypeVars with solved types

### Key Concepts

```lx
// Eager unification (current):
fn foo(x) {
  let y = x + 1  // Immediately: x must be Number
  return x       // x already bound to Number
}

// Constraint collection (proposed):
fn foo(x) {
  let y = x + 1  // Record: constraint "x ~ Number"
  return x       // Record: constraint "return ~ x"
  // Solve all constraints together:
  // Solution: x = Number, return = Number
}
```

### Implementation Design

#### 2.1 Constraint Representation

```lx
// File: lx/src/typecheck.lx

// Constraint types
fn constraintEqual(t1, t2, origin) {
  .{ kind: "Equal", t1: t1, t2: t2, origin: origin }
}

fn constraintInstance(t1, scheme, origin) {
  .{ kind: "Instance", t1: t1, scheme: scheme, origin: origin }
}

fn constraintHasField(recordType, fieldName, fieldType, origin) {
  .{ kind: "HasField", record: recordType, field: fieldName, type: fieldType, origin: origin }
}

// Constraint store
fn makeConstraintStore() {
  .{
    constraints: [],  // All generated constraints
    typeVarBounds: .{},  // Explicit bounds (e.g., T <: Number)
  }
}
```

#### 2.2 Constraint Generation

```lx
// Modified checkExpr functions generate constraints instead of solving

fn synthExpr(checker, node) {
  if node.type == "Binary" {
    let leftType = synthExpr(checker, node.left)
    let rightType = synthExpr(checker, node.right)

    if node.operator.lexeme == "+" {
      let resultType = freshTypeVar(checker)

      // OLD: constrain(checker, leftType, typeNumber(), ...)
      // NEW: Generate constraint, don't solve yet
      addConstraint(checker, constraintEqual(leftType, typeNumber(), node.left.id))
      addConstraint(checker, constraintEqual(rightType, typeNumber(), node.right.id))

      return resultType
    }
  }
  // ...
}
```

#### 2.3 Constraint Solver

```lx
// Implement Algorithm W (Hindley-Milner) or Algorithm M (bidirectional)

fn solveConstraints(checker, constraints) {
  let substitution = .{}  // TypeVarId -> Type

  for let i = 0; i < len(constraints); i = i + 1 {
    let constraint = constraints[i]

    if constraint.kind == "Equal" {
      let solution = unify(checker, constraint.t1, constraint.t2)
      if !solution {
        addError(checker, constraint.origin, "Type mismatch")
      } else {
        // Apply solution to remaining constraints
        substitution = composeSubs(substitution, solution)
        constraints = applySubToConstraints(solution, constraints)
      }
    }

    if constraint.kind == "HasField" {
      // Solve record field constraints
      let recordType = deref(checker, constraint.record)
      if recordType.kind == "TypeVar" {
        // Refine TypeVar to Record with field
        let fields = .{}
        fields[constraint.field] = constraint.type
        let solution = bindTypeVar(recordType.id, typeRecord(fields))
        substitution = composeSubs(substitution, solution)
      } else if recordType.kind == "Record" {
        // Check field exists and unify types
        if !recordType.fields[constraint.field] {
          addError(checker, constraint.origin, "Field not found: " + constraint.field)
        } else {
          let fieldSolution = unify(checker, recordType.fields[constraint.field], constraint.type)
          substitution = composeSubs(substitution, fieldSolution)
        }
      }
    }
  }

  return substitution
}
```

#### 2.4 Integration with Two-Pass System

```lx
fn typecheck(ast, resolveResult, opts) {
  let checker = makeChecker(resolveResult, opts)
  checker.currentEnv = TypeEnv(nil)

  // PASS 1: Collect constraints (don't solve yet)
  checker.currentPass = 1
  checker.constraintMode = "collect"
  for let i = 0; i < len(ast.body); i = i + 1 {
    synthExpr(checker, ast.body[i])
  }

  // SOLVE: Apply constraint solver
  let substitution = solveConstraints(checker, checker.constraints)
  applySubstitution(checker, substitution)

  // PASS 2: Refine function bodies with solved types
  finalizeTypes(checker)

  // SOLVE AGAIN: Re-solve with additional constraints from Pass 2
  let substitution2 = solveConstraints(checker, checker.deferredConstraints)
  applySubstitution(checker, substitution2)

  // Post-process: dereference all types
  // ...
}
```

### Implementation Phases

**Phase 2A: Constraint Infrastructure (Week 1)**
- [ ] Define constraint types (Equal, Instance, HasField, etc.)
- [ ] Implement constraint store data structure
- [ ] Create `addConstraint(checker, constraint)` function
- [ ] Test: Constraints can be collected without solving

**Phase 2B: Modify Constraint Generation (Week 2-3)**
- [ ] Update `synthExpr` to generate constraints instead of solving
- [ ] Update binary operators to emit equality constraints
- [ ] Update function calls to emit instantiation constraints
- [ ] Update field access to emit HasField constraints
- [ ] Test: All constraints generated correctly (without solving)

**Phase 2C: Implement Constraint Solver (Week 3-4)**
- [ ] Implement `unify` that returns substitution instead of mutating
- [ ] Implement `solveConstraints` with Algorithm W
- [ ] Implement substitution composition
- [ ] Implement constraint substitution application
- [ ] Test: Simple constraints solve correctly

**Phase 2D: Integration with Two-Pass (Week 4-5)**
- [ ] Add constraint solving between Pass 1 and Pass 2
- [ ] Add second solving phase after Pass 2
- [ ] Update `finalizeTypes` to work with collected constraints
- [ ] Test: Two-pass with constraint solving produces correct types

**Phase 2E: Advanced Constraint Types (Week 5-6)**
- [ ] Implement HasField constraint solving for records
- [ ] Implement Instance constraint solving for polymorphism
- [ ] Implement constraint simplification/normalization
- [ ] Test: Complex constraints (nested records, polymorphic functions)

**Phase 2F: Error Reporting & Optimization (Week 6)**
- [ ] Improve error messages to reference constraint origins
- [ ] Implement constraint graph visualization for debugging
- [ ] Performance optimization (constraint indexing, early pruning)
- [ ] Test: Full test suite passes with improved errors

### Expected Improvements

**Before** (Eager unification):
```lx
fn foo(x) {
  let a = x.field1  // Immediately: x ~ Record({ field1: T1 })
  let b = x.field2  // Immediately: x ~ Record({ field2: T2 })
  // Two separate constraints, may fail to unify if fields overlap
}
```

**After** (Constraint collection):
```lx
fn foo(x) {
  let a = x.field1  // Collect: x has field1 : T1
  let b = x.field2  // Collect: x has field2 : T2
  // Solve together: x ~ Record({ field1: T1, field2: T2 })
}
```

### Testing Strategy

```lx
// test/typecheck.constraints.test.lx

test("constraints - field access accumulation", fn(assert) {
  let result = parseAndTypecheck("
    fn foo(obj) {
      let x = obj.a
      let y = obj.b
      obj
    }
  ")
  assert.truthy(result.success)
  // obj should be inferred as Record({ a: T1, b: T2 })
})

test("constraints - error reporting with origins", fn(assert) {
  let result = parseAndTypecheck("
    fn foo(x) {
      let y = x + 1
      let z = x + \"oops\"
    }
  ")
  assert.equal(result.success, false)
  // Error should reference both constraint origins
})
```

---

## Enhancement 3: Field Access Tracking

**Goal**: Link parameter field accesses to call-site argument types

**Priority**: Medium (Solves the purifyFunction issue)
**Complexity**: Medium
**Estimated Effort**: 2-3 weeks
**Dependencies**: Enhancement 2 (constraint solving)

### Overview

When a function accesses fields on a parameter, and that function is called with a concrete argument, we should propagate the argument's field types to the parameter's usage.

```lx
fn purifyFunction(func) {  // func: TypeVar
  .{ name: func.name }      // Create constraint: func has field 'name'
}

// Call site:
let concreteFunc = .{ name: "foo", arity: 2 }
purifyFunction(concreteFunc)  // Should resolve: name has type String
```

### Key Concepts

**Field Access Constraints**:
- Track which fields are accessed on parameters
- When function is called, propagate argument field types to those accesses
- Requires constraint solving to connect parameter usage to argument types

### Implementation Design

#### 3.1 Enhanced HasField Constraints

```lx
// Extend constraint system to track field access chains

fn constraintFieldAccess(baseType, accessPath, resultType, origin) {
  .{
    kind: "FieldAccess",
    base: baseType,           // The object being accessed
    path: accessPath,         // e.g., ["chunk", "filename"]
    result: resultType,       // The type of the field value
    origin: origin
  }
}

// Example: func.chunk.filename
// Creates: FieldAccess(func, ["chunk", "filename"], T_result, nodeId)
```

#### 3.2 Parameter Field Access Tracking

```lx
fn checkFunction(checker, node) {
  // ...

  // Track field accesses on parameters during body checking
  let paramFieldAccesses = .{}  // paramId -> [accessPath]

  for each parameter {
    paramFieldAccesses[param.id] = []
  }

  // Set tracking mode
  checker.trackFieldAccesses = paramFieldAccesses

  let bodyType = synthExpr(checker, node.body)

  // Store field access info for this function
  checker.functionFieldAccesses[node.id] = paramFieldAccesses
}

fn checkDot(checker, node) {
  let objType = synthExpr(checker, node.object)
  let fieldName = node.property.value

  // If object is a parameter being tracked, record this access
  if checker.trackFieldAccesses and node.object.type == "Identifier" {
    let paramId = node.object.id
    if checker.trackFieldAccesses[paramId] {
      push(checker.trackFieldAccesses[paramId], [fieldName])
    }
  }

  // Generate field access constraint
  let resultType = freshTypeVar(checker)
  addConstraint(checker, constraintFieldAccess(objType, [fieldName], resultType, node.id))

  return resultType
}
```

#### 3.3 Call-Site Type Propagation

```lx
fn checkCall(checker, node) {
  let calleeType = synthExpr(checker, node.callee)

  // Check arguments
  let argTypes = []
  for let i = 0; i < len(node.args); i = i + 1 {
    push(argTypes, synthExpr(checker, node.args[i]))
  }

  calleeType = deref(checker, calleeType)

  if calleeType.kind == "Function" {
    // Get field access info for this function (if available)
    let functionId = getFunctionId(node.callee)
    let fieldAccesses = checker.functionFieldAccesses[functionId]

    if fieldAccesses {
      // For each parameter, propagate argument field types
      for let i = 0; i < len(node.args); i = i + 1 {
        let argType = deref(checker, argTypes[i])
        let paramAccesses = fieldAccesses[i]  // Field paths accessed on this param

        if argType.kind == "Record" and paramAccesses {
          // Propagate field types to constraints
          for each accessPath in paramAccesses {
            let fieldType = getNestedFieldType(argType, accessPath)
            if fieldType {
              // Add constraint linking field access result to concrete type
              // This happens during constraint solving phase
              propagateFieldTypeConstraint(checker, i, accessPath, fieldType)
            }
          }
        }
      }
    }

    // Standard argument checking
    for let i = 0; i < len(calleeType.params); i = i + 1 {
      addConstraint(checker, constraintEqual(argTypes[i], calleeType.params[i], node.args[i].id))
    }

    return calleeType.return
  }

  // ...
}
```

#### 3.4 Constraint Solving with Field Propagation

```lx
fn solveConstraints(checker, constraints) {
  // First pass: solve equality constraints
  let substitution = solveEqualityConstraints(constraints)

  // Second pass: solve field access constraints with propagated types
  for each constraint in constraints {
    if constraint.kind == "FieldAccess" {
      let baseType = applySubstitution(substitution, constraint.base)

      if baseType.kind == "Record" {
        // Base is concrete record, extract field type
        let fieldType = getNestedFieldType(baseType, constraint.path)
        if fieldType {
          // Unify result type with actual field type
          let fieldSolution = unify(constraint.result, fieldType)
          substitution = composeSubs(substitution, fieldSolution)
        }
      } else if baseType.kind == "TypeVar" {
        // Base is still generic, create structural constraint
        let fieldType = constraint.result
        let structuralType = createRecordTypeFromPath(constraint.path, fieldType)
        let structuralSolution = unify(baseType, structuralType)
        substitution = composeSubs(substitution, structuralSolution)
      }
    }
  }

  return substitution
}

fn getNestedFieldType(recordType, path) {
  let currentType = recordType
  for let i = 0; i < len(path); i = i + 1 {
    let fieldName = path[i]
    if currentType.kind != "Record" or !currentType.fields[fieldName] {
      return nil
    }
    currentType = currentType.fields[fieldName]
  }
  return currentType
}
```

### Implementation Phases

**Phase 3A: Field Access Tracking (Week 1)**
- [ ] Add `trackFieldAccesses` mode to checker
- [ ] Update `checkDot` to record field accesses on parameters
- [ ] Store field access info per function
- [ ] Test: Field accesses correctly recorded

**Phase 3B: Enhanced Constraints (Week 1-2)**
- [ ] Implement `constraintFieldAccess` type
- [ ] Update `checkDot` to generate FieldAccess constraints
- [ ] Implement nested field path tracking (e.g., `obj.a.b.c`)
- [ ] Test: FieldAccess constraints generated correctly

**Phase 3C: Call-Site Propagation (Week 2)**
- [ ] Update `checkCall` to look up function field accesses
- [ ] Implement argument field type extraction
- [ ] Generate propagation constraints at call sites
- [ ] Test: Argument types propagate to parameter field accesses

**Phase 3D: Constraint Solver Integration (Week 2-3)**
- [ ] Update `solveConstraints` to handle FieldAccess constraints
- [ ] Implement nested field type extraction
- [ ] Implement structural type creation from field paths
- [ ] Test: Field constraints solve correctly

**Phase 3E: End-to-End Integration (Week 3)**
- [ ] Test with purifyFunction example
- [ ] Test with deeply nested field accesses
- [ ] Test with multiple parameters with field accesses
- [ ] Performance optimization

### Expected Improvements

**Before**:
```lx
fn purifyFunction(func) {
  .{
    name: func.name,      // name: Unbound T1
    arity: func.arity,    // arity: Unbound T2
  }
}

fn compile(...) {
  let f = .{ name: "foo", arity: 2 }  // Concrete type
  return .{
    function: purifyFunction(f)  // Still has Unbound T1, T2
  }
}
```

**After**:
```lx
fn purifyFunction(func) {
  .{
    name: func.name,      // Tracked: func.name accessed
    arity: func.arity,    // Tracked: func.arity accessed
  }
}

fn compile(...) {
  let f = .{ name: "foo", arity: 2 }  // name: String, arity: Number
  return .{
    function: purifyFunction(f)  // Propagated: name: String, arity: Number
  }
}
```

### Testing Strategy

```lx
// test/typecheck.field-tracking.test.lx

test("field tracking - basic parameter field access", fn(assert) {
  let result = parseAndTypecheck("
    fn getter(obj) { obj.field }
    let x = .{ field: 42 }
    getter(x)
  ")
  assert.truthy(result.success)
  // getter return type should be Number (from x.field)
})

test("field tracking - nested field access", fn(assert) {
  let result = parseAndTypecheck("
    fn getFilename(obj) { obj.chunk.filename }
    let x = .{ chunk: .{ filename: \"test.lx\" } }
    getFilename(x)
  ")
  assert.truthy(result.success)
  // Return type should be String
})

test("field tracking - multiple call sites", fn(assert) {
  let result = parseAndTypecheck("
    fn getName(obj) { obj.name }
    let a = .{ name: \"Alice\" }
    let b = .{ name: 42 }
    getName(a)
    getName(b)  // Should error: inconsistent name types
  ")
  assert.equal(result.success, false)
})
```

---

## Integration Timeline

### Recommended Order

1. **Enhancement 1 (Weeks 1-3)**: Bidirectional Type Checking
   - Provides foundation for better type flow
   - Relatively low risk, incremental migration
   - Immediate benefits for explicit type annotations

2. **Enhancement 2 (Weeks 4-9)**: Constraint Collection & Solving
   - Major architectural change, high value
   - Enables sophisticated inference patterns
   - Required for Enhancement 3

3. **Enhancement 3 (Weeks 10-12)**: Field Access Tracking
   - Solves the original purifyFunction issue
   - Builds on constraint solver
   - Relatively straightforward once constraints in place

**Total Timeline**: ~12 weeks (3 months)

### Incremental Delivery

**Month 1**: Bidirectional checking
- Deliverable: Better type errors, explicit annotations work
- Risk: Low

**Month 2-3**: Constraint collection & solving
- Deliverable: Advanced type inference, better polymorphism
- Risk: Medium-High (major refactoring)

**Month 3**: Field access tracking
- Deliverable: Generic parameter inference
- Risk: Low (builds on solid foundation)

---

## Testing & Validation

### Test Coverage Requirements

Each enhancement must achieve:
- [ ] 100% of existing tests pass
- [ ] 90%+ coverage of new code paths
- [ ] Performance regression < 20% (per enhancement)
- [ ] All examples in this doc work correctly

### Regression Test Suite

Create comprehensive regression tests:
```lx
// test/regression/issue-purify-function.test.lx
test("regression - purifyFunction type inference", fn(assert) {
  // Original issue from 2025-12-17
  let result = parseAndTypecheck("...")
  // Should fully resolve all field types
})
```

### Performance Benchmarks

Track performance impact:
```lx
// test/bench/typecheck-performance.test.lx
- Current: ~X ms for 1000-line file
- After Enhancement 1: < 1.2X ms
- After Enhancement 2: < 1.5X ms (constraint solving overhead)
- After Enhancement 3: < 1.6X ms
```

---

## Risk Mitigation

### High-Risk Areas

1. **Constraint Solver Complexity**
   - Risk: Solver could be slow or buggy
   - Mitigation: Start with simple Algorithm W, optimize later
   - Fallback: Keep eager unification as option

2. **Breaking Changes**
   - Risk: Type errors in previously working code
   - Mitigation: Feature flag to enable/disable new inference
   - Rollback: Incremental migration allows reverting individual pieces

3. **Performance Degradation**
   - Risk: Constraint collection/solving adds overhead
   - Mitigation: Profile early and often, optimize hot paths
   - Fallback: Add caching, memoization, constraint indexing

### Rollback Strategy

Each enhancement is independent:
- Enhancement 1: Can disable bidirectional checking, fallback to synthesis
- Enhancement 2: Can disable constraint collection, use eager unification
- Enhancement 3: Can disable field tracking without breaking Enhancements 1-2

---

## Success Metrics

### Qualitative Goals

- [ ] `purifyFunction` example shows fully resolved types
- [ ] Forward references work reliably
- [ ] Type errors are more specific and helpful
- [ ] Developer experience feels "magical"

### Quantitative Goals

- [ ] 100% test pass rate (404+ tests)
- [ ] < 20% performance regression per enhancement
- [ ] < 5% "Unbound" TypeVars in real codebases (vs ~20% currently)
- [ ] Zero breaking changes to valid code

---

## Future Work (Beyond Scope)

These enhancements enable future improvements:

1. **Full Type Annotations** (Phase 2 feature)
   - User-written type signatures
   - Type alias definitions
   - Interface/trait types

2. **Subtyping** (Requires constraint solver)
   - Structural subtyping for records
   - Width subtyping (extra fields OK)
   - Depth subtyping (covariant fields)

3. **Effect System** (Separate project)
   - Track side effects in types
   - IO, mutation, exceptions as effects

4. **Dependent Types** (Research)
   - Types that depend on values
   - Refinement types

---

## References

### Papers & Resources

1. **Bidirectional Type Checking**
   - "Bidirectional Typing" by Dunfield & Krishnaswami (2019)
   - "Local Type Inference" by Pierce & Turner (2000)

2. **Constraint-Based Type Inference**
   - "A Theory of Type Polymorphism in Programming" by Milner (1978)
   - "HM(X): Constraint-based type inference" by Odersky et al. (1999)

3. **Field Access Inference**
   - "Colored Local Type Inference" by Odersky & Zenger (2001)
   - "Type Classes" by Wadler & Blott (1989) - similar propagation

### Existing Implementations

- **TypeScript**: Bidirectional checking + constraint solving
- **Rust**: Hindley-Milner with bidirectional hints
- **OCaml**: Classic HM with local type annotations
- **Haskell**: Advanced constraint solving with type classes

---

## Document Maintenance

**Update Frequency**: After each enhancement completes
**Owner**: Core team
**Review**: Before starting each enhancement phase

**Changelog**:
- 2025-12-17: Initial roadmap created after two-pass implementation
