# Parser Test Fixtures

Test fixtures for parser validation. These files demonstrate valid and invalid lx syntax.

## Directory Structure

- `valid/` - Programs that should parse successfully
- `invalid/` - Programs that should fail parsing with specific errors

## Valid Fixtures

### `for-variations.lx`
Demonstrates all valid for-loop styles:
- While-style: `for condition { }`
- C-style: `for init; condition; update { }`
- Empty init: `for ; condition; update { }`
- Infinite loop: `for ;; { }`

### `control-flow.lx`
Valid control flow examples:
- Return in functions at end of block
- Return at script EOF (top level)

### `loop-control.lx`
Valid loop control flow:
- Break at end of loop block
- Break with value
- Continue at end of loop block
- Nested loops with break/continue

## Invalid Fixtures

### `return-outside-function.lx`
Return statement in middle of script (not at EOF)

### `break-outside-loop.lx`
Break statement outside of any loop

### `continue-outside-loop.lx`
Continue statement outside of any loop

### `return-not-at-end.lx`
Return statement in middle of block (not tail position)

### `break-not-at-end.lx`
Break statement in middle of loop block (not tail position)

## Usage

```lx
// In tests
fn parseFixture(path) {
  let src = Lx.fs.readFile(path)
  parse(src, path)
}

let result = parseFixture("test/fixtures/parser/valid/for-variations.lx")
assert.truthy(result.success)

let result = parseFixture("test/fixtures/parser/invalid/break-outside-loop.lx")
assert.truthy(!result.success)
```

## Adding New Fixtures

When adding test cases:

1. Choose descriptive filenames that indicate what's being tested
2. Add comments explaining the expected behavior
3. Group related tests in the same file when appropriate
4. Use `valid/` for programs that should parse successfully
5. Use `invalid/` for programs that should produce parse errors
