# lx Parser

A parser for the lx language that generates Abstract Syntax Trees (AST) for static analysis and LSP implementation.

## Usage

```lx
let parse = import "src/parser.lx"

let result = parse(sourceCode, filename)

if result.success {
  // Access the AST
  let ast = result.ast
  // ast.body contains array of statement nodes
} else {
  // Handle errors
  each(result.errors, fn(err) { println(err) })
}
```

## AST Node Structure

All AST nodes share a common structure:

```lx
{
  type: "NodeType",        // The type of the node
  filename: "path.lx",     // Full import path
  line: 1,                 // Starting line number
  col: 5,                  // Starting column number
  endLine: 1,              // Ending line number
  endCol: 10,              // Ending column number
  // ... node-specific fields
}
```

### Node Types

#### Literals

- **Number**: `{ type: "Number", value: 42, lexeme: "42", ... }`
- **String**: `{ type: "String", value: "hello", lexeme: "\"hello\"", ... }`
- **Bool**: `{ type: "Bool", value: true, lexeme: "true", ... }`
- **Nil**: `{ type: "Nil", lexeme: "nil", ... }`

#### Identifiers

- **Identifier**: `{ type: "Identifier", name: "foo", lexeme: "foo", ... }`

#### Expressions

- **Binary**: `{ type: "Binary", left: node, operator: {type, lexeme, line, col}, right: node, ... }`
- **Unary**: `{ type: "Unary", operator: {type, lexeme, line, col}, operand: node, ... }`
- **Logical**: `{ type: "Logical", left: node, operator: {type, lexeme, line, col}, right: node, ... }`
- **Grouping**: `{ type: "Grouping", expression: node, ... }`
- **Assignment**: `{ type: "Assignment", target: node, value: node, ... }`

#### Collections

- **Array**: `{ type: "Array", elements: [nodes], ... }`
- **Hashmap**: `{ type: "Hashmap", pairs: [{key: node, value: node}], ... }`
- **Index**: `{ type: "Index", object: node, index: node, ... }`
- **Dot**: `{ type: "Dot", object: node, property: identifier, ... }`

#### Control Flow

- **If**: `{ type: "If", condition: node, then: block, else: block/nil, ... }`
- **For**: `{ type: "For", init: node/nil, condition: node/nil, update: node/nil, body: block, ... }`
- **Break**: `{ type: "Break", value: node/nil, ... }`
- **Continue**: `{ type: "Continue", ... }`
- **Return**: `{ type: "Return", value: node/nil, ... }`

#### Functions

- **Function**: `{ type: "Function", name: identifier/nil, params: [identifiers], body: block, ... }`
- **Call**: `{ type: "Call", callee: node, args: [nodes], ... }`
- **Arrow**: `{ type: "Arrow", left: node, right: node, ... }`

#### Statements

- **Let**: `{ type: "Let", name: identifier, init: node/nil, ... }`
- **Block**: `{ type: "Block", expressions: [nodes], ... }`
- **Import**: `{ type: "Import", path: stringNode, ... }`

#### Program

- **Program**: `{ type: "Program", filename: "path.lx", body: [nodes], ... }`

## Features

- **Position Tracking**: Every node includes precise source location (line, col, endLine, endCol)
- **Filename Tracking**: All nodes know their source file
- **Error Recovery**: Parser continues after errors and collects all error messages
- **Parent-less Design**: AST nodes don't have parent references, avoiding circular dependencies

## Design Decisions

1. **Position Information**: Both start and end positions are tracked for better error reporting and range queries
2. **Lexeme Storage**: Only leaf nodes (literals, identifiers) store lexemes to save memory
3. **No Parent References**: The AST is designed without parent pointers. If upward traversal is needed, build parent links in a separate pass
4. **Import Handling**: The parser creates Import AST nodes. Recursive import resolution should be handled by a higher-level driver (similar to the current compiler's import cache)
5. **Operator Storage**: Binary/Unary nodes store operator info (type, lexeme, position) for precise error messages

## Testing

The parser has comprehensive test coverage with 48 tests and 230 assertions covering:

- All literal types (numbers, strings, bools, nil)
- Identifiers and variables
- Binary, unary, and logical expressions
- Operator precedence
- Collections (arrays, hashmaps, indexing)
- Functions (anonymous, named, calls, arrow operator)
- Control flow (if/else, for, break, continue, return)
- Blocks and grouping
- Assignments (simple, dot, index)
- Import statements
- Position and filename tracking
- Error handling and recovery

Run parser tests:

```bash
../out/lx run test/parser.test.lx
```

Or run all tests:

```bash
make test
```

## Next Steps

For a complete static analysis system, you might want to add:

1. **Symbol Table**: Track variable declarations and scopes
2. **Type System**: Add type inference/checking
3. **Import Resolution**: Recursively parse imported modules with caching
4. **Parent Links**: Add a separate pass to build parent references if needed
5. **Scope Analysis**: Track which identifiers are in scope at each point
6. **LSP Features**: Use the AST for autocomplete, go-to-definition, etc.

## Example

See `examples/parser-demo.lx` for usage examples.
