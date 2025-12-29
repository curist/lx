# ANF Phase (`src/passes/frontend/anf.lx`)

Normalize expressions into A-normal form while preserving evaluation order.

This phase:
- sequences non-atomic subexpressions into temp `let` bindings
- preserves short-circuiting for `and` / `or`
- keeps block expression values explicit
- does not perform name resolution or semantic validation

This phase does **not**:
- change program meaning
- reorder effects
- mutate input AST nodes

## Input

Lowered AST.

## Output

```lx
fn anf(ast, opts) -> .{
  success: bool,
  ast: BlockNode,
  origin: .{ newId -> oldId },
  errors: [],
  nextNodeId: number,
}
```
