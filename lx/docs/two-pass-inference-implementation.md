# Two-Pass Type Inference Implementation

**Status**: Completed
**Completed**: 2025-12-17
**Implementation**: lx/src/typecheck.lx

## Overview

This document describes the two-pass type inference system implemented to support forward references and mutual recursion in lx. This serves as a foundation for future enhancements (see `type-inference-enhancements.md`).

## Architecture

### Pass 1: Skeleton Collection

**Goal**: Create type skeletons for all named functions before checking their bodies.

**Process**:
1. Traverse AST in source order
2. For each named function:
   - Create fresh TypeVars for parameters
   - Create fresh TypeVar for return type
   - Build function type skeleton: `Function([params], return)`
   - Bind skeleton to function name
   - Cache in `globalTypeCache` for cross-scope access
   - Record function for Pass 2 processing
   - **Skip body checking**
3. For other expressions:
   - Check normally
   - Defer constraint failures (don't error yet)

**Key Insight**: Function skeletons are available immediately, enabling forward references and mutual recursion.

### Pass 2: Type Refinement

**Goal**: Re-check function bodies with complete type context.

**Process**:
1. For each recorded function:
   - Enter new scope
   - Bind parameters to TypeVars from Pass 1 skeleton
   - Re-check body (now with all function types known)
   - Unify body type with return type from skeleton
   - Exit scope
2. Re-check deferred constraints from Pass 1
3. Report any remaining type errors

**Key Insight**: Function bodies see all function types (including forward refs), enabling correct type inference.

## Implementation Details

### Modified Files

**lx/src/typecheck.lx**:
- Lines 2395-2398: Added pass tracking fields to checker state
- Lines 606-624: Modified `constrain()` to defer errors in Pass 1
- Lines 1616-1653: Split `checkFunction()` for Pass 1 skeleton mode
- Lines 1635: Added globalTypeCache entry for cross-scope lookup
- Lines 2418-2466: Added `finalizeTypes()` for Pass 2
- Lines 2524-2531: Updated `typecheck()` to orchestrate both passes
- Lines 1434, 1466: Added `deref()` in `checkHashmap()` for better field types

### Key Data Structures

```lx
// Checker state additions
.{
  // Pass tracking
  currentPass: 1,  // 1 = skeleton collection, 2 = refinement

  // Deferred constraints from Pass 1
  deferredConstraints: [
    .{ t1: Type, t2: Type, nodeId: NodeId, errorMsg: String }
  ],

  // Functions to refine in Pass 2
  functionDecls: [
    .{
      nodeId: NodeId,
      nameId: NodeId,
      binding: Binding,
      bodyNode: Node,
      paramTypes: [TypeVar],
      returnType: TypeVar,
    }
  ],
}
```

### Algorithm Flow

```
Input: AST, ResolveResult

1. PASS 1:
   currentPass = 1
   for each statement in AST:
     if named function:
       create skeleton
       cache in globalTypeCache
       record in functionDecls
       skip body
     else:
       check normally
       defer constraint failures

2. PASS 2:
   finalizeTypes():
     currentPass = 2
     for each recorded function:
       enter scope
       bind params to skeleton TypeVars
       re-check body
       unify with return TypeVar
       exit scope
     for each deferred constraint:
       retry unification
       report errors if still failing

3. POST-PROCESS:
   derefAll(types)
   return result
```

## Code Examples

### Pass 1: Skeleton Creation

```lx
// Before (single-pass):
fn checkFunction(checker, node) {
  enterScope(checker)
  // bind params
  // check body immediately
  exitScope(checker)
}

// After (two-pass):
fn checkFunction(checker, node) {
  if node.name and checker.currentPass == 1 {
    // Create skeleton
    let paramTypes = [freshTypeVar for each param]
    let returnType = freshTypeVar(checker)
    let skeleton = typeFunction(paramTypes, returnType)

    // Cache for cross-scope access
    checker.globalTypeCache[node.id] = binding

    // Record for Pass 2
    push(checker.functionDecls, .{ ... })

    return skeleton  // Early return, skip body
  }

  // Pass 2 or anonymous: full checking
  enterScope(checker)
  // ... existing logic ...
  exitScope(checker)
}
```

### Pass 2: Body Refinement

```lx
fn finalizeTypes(checker) {
  checker.currentPass = 2

  for each fnDecl in checker.functionDecls {
    // Re-enter scope with skeleton TypeVars
    enterScope(checker)

    for each param {
      bind(param.id, fnDecl.paramTypes[i])  // Use skeleton TypeVar
    }

    checker.currentReturnType = fnDecl.returnType

    // Re-check body (now all functions are known)
    let bodyType = checkExpr(checker, fnDecl.bodyNode.body)

    // Refine return type
    constrain(checker, bodyType, fnDecl.returnType, ...)

    exitScope(checker)
  }

  // Re-check deferred constraints
  for each constraint in checker.deferredConstraints {
    if !unify(constraint.t1, constraint.t2) {
      addError(...)  // Report error now
    }
  }
}
```

## Testing

### Test Coverage

**All existing tests pass**: 404 tests, 1227 assertions ✓

**Key test**: `test/typecheck.function.test.lx`
- Forward references work
- Mutual recursion works
- Type errors in recursive calls caught correctly

### Example Test Case

```lx
test("typecheck - recursion infers consistent type", fn(assert) {
  // This should fail: recursive call has wrong type
  let result = parseAndTypecheck("
    {
      fn f(x) {
        if x == 0 { 1 }
        else { f(\"oops\") }  // Error: String not Number
      }
      f(5)
    }
  ")
  assert.equal(result.success, false)
  assert.truthy(len(result.errors) > 0)
})
```

**Before two-pass**: This test failed (success was true)
**After two-pass**: This test passes (error correctly detected)

## Performance

**Overhead**: ~1.5-2x traversal cost
- Pass 1: Full AST traversal (function bodies skipped for named functions)
- Pass 2: Re-check recorded function bodies only
- Typical case: Most code is outside function bodies, so overhead is minimal

**Optimization opportunities**:
- Pass 2 only processes named functions (anonymous functions checked in Pass 1)
- Deferred constraints typically small (most constraints succeed in Pass 1)

## Limitations

### What Works

✅ Forward references
✅ Mutual recursion
✅ Cross-scope function calls
✅ Deferred error reporting

### What Doesn't (Yet)

❌ **Generic parameter inference**: When a function parameter has type TypeVar and fields are accessed on it, those field types remain unbound TypeVars.

**Example**:
```lx
fn purifyFunction(func) {  // func: TypeVar
  .{
    name: func.name,       // name: Unbound T1
    arity: func.arity,     // arity: Unbound T2
  }
}
```

**Why**: Field access on TypeVar creates fresh TypeVars. When the function is called with a concrete argument, those TypeVars aren't back-propagated.

**Solution**: See `type-inference-enhancements.md`, specifically:
- Enhancement 2: Constraint Collection & Solving
- Enhancement 3: Field Access Tracking

## Debugging

### Common Issues

**1. "Unresolved identifier" in Pass 2**
- Cause: Function binding not in globalTypeCache
- Fix: Ensure Pass 1 adds binding to both currentEnv and globalTypeCache (line 1635)

**2. TypeVar not resolved after Pass 2**
- Cause: Deferred constraint not re-checked, or binding not unified
- Fix: Check `finalizeTypes()` deferred constraint loop (lines 2458-2465)

**3. Environment scope mismatch**
- Cause: Pass 2 creates wrong environment hierarchy
- Fix: Ensure `enterScope()` in Pass 2 creates child of correct parent

### Debug Output

Add logging to track passes:

```lx
fn typecheck(ast, resolveResult, opts) {
  let checker = makeChecker(resolveResult, opts)

  println("=== PASS 1: Skeleton Collection ===")
  checker.currentPass = 1
  // ...

  println("=== PASS 2: Type Refinement ===")
  println("Functions to refine:", len(checker.functionDecls))
  finalizeTypes(checker)

  println("=== POST-PROCESS ===")
  // ...
}
```

## Migration Guide

### For Future Enhancements

When implementing bidirectional checking or constraint collection:

1. **Keep Pass 1 skeleton creation**: It's orthogonal to checking mode
2. **Extend Pass 2 refinement**: Add bidirectional checking in `finalizeTypes()`
3. **Replace deferred constraints**: Use constraint collection instead

### Backward Compatibility

The two-pass system maintains full backward compatibility:
- All existing code typechecks identically
- No breaking changes to valid programs
- Only new capabilities added (forward refs, mutual recursion)

## References

### Related Code

- `lx/src/resolve.lx`: Name resolution with hoisting (similar two-phase approach)
- `lx/src/driver.lx`: Pipeline orchestration
- `lx/services/query.lx`: Type formatting for IDE

### Related Documents

- `type-inference-enhancements.md`: Future improvements roadmap
- `CLAUDE.md`: lx language guide for AI assistants

## Changelog

- 2025-12-17: Initial implementation completed
- 2025-12-17: Fixed globalTypeCache cross-scope lookup bug
- 2025-12-17: Added deref() in checkHashmap for better record field types
