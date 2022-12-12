![status](https://github.com/curist/lx/actions/workflows/test.yml/badge.svg)

# Lx programming language
Yet another [Lox](https://github.com/munificent/craftinginterpreters) derived programming language.

[Web](https://curist.github.io/lx/)

## Lx is
* Dynamic
* Minimal
* Compiled
* Alpha quality
* Almost everything is expression
* (Half) self hosted; Lx can compile itself, but relies on a C runtime.

## Lx is not
* Object oriented; Lx doesn't come with class.
* Static type; Lx isn't suitable to build your next big project.
* Inspiring; there are no big ideas in Lx.
* That serious; it doesn't come with error handling, for ~~instance~~ example.

# CLI

```sh
❯ lx version
lx version 2022.12.12-f57332b (Darwin)

❯ lx
Usage:

  lx <command> [arguments]

The commands are:
  run          Run source or lxobj
  eval         Evaluate expression
  repl         Start REPL
  compile      Compile source to lxobj
  disasm       Disassemble lxobj
  version      Print version
  help         Print this helpful page
```

# Lx in Y minutes

```javascript
// Lx only has single line comments

// here goes some primitive types

nil // good old humble nil

// boolean
true false

// Lx numbers are in 64bit float
123
-3.00000000004
// ^ above will be treated as 123 - 3.00000000004, btw

// string
"Lx strings are double quoted"
"few escape sequences are supported: \r \n \t \\ \" "
"there's no utf-8 support currently though :("

// falsy values: only `false` & `nil` are treated as falsy, just like in lua

// expressions, let's start with unary
false == !true // `!`, not operator
let x = -a // `-`, negate operator

// binary, basic arithmetic, no surprise, + - * / %
let y = 1 + 2 * 3 / 4 % 5 - 6

// few bitwise operators: & | ^ << >>
let z = 500 & 8 | 7 ^ 6 << 3 >> 2

// variable declaration using `let`
let a = "var"

// braces pair form a block, and block is also an expression in Lx
// so we can
let a = {
  let b = 10
  b * 12 // block implicitly returns last evaluated value
} // a == 120

// function is declared using `fn` keyword
fn foo(bar, baz) { // args are comma seperated
  print(bar)  // print is a builtin function, that, prints
  groan(baz)  // groan is a builtin function, that prints to stderr
}

// function are first-class citizen, so we can
let adder = fn(a) {
  fn (b) { a + b }  // both the fn & `a+b` are implicity returned
}

// array
let arr = [4, 5, 6]
print(arr[2]) // 6 printed

// or using range builtin
let countToFive = range(5) // [0, 1, 2, 3, 4]

// hashmap... ah, I have to confess; having a different syntax here,
// is mostly because I want to keep the single pass compiler simpler.
// it's certainly possible to let Lx to have regular hashmap syntax
// like most other languages, a plain good old `{}`
// but that complicates the parser code quite a bit...
// so, in Lx, hashmap begins with `.{`, and ends with `}`
let mymap = .{
  key: "value", // key in string
  [40]: "a number key", // key could also in numbers
  ["nil"]: "couldn't use keywords directly as key",
}
print(mymap.key) // `value` printed
mymap[42] = "forty-two" // updating value


// conditions
// and & or keywords, remember that lua pattern?
let howdy = foo and bar or baz // ternary expression

let ifResult = if foo {
  print("then clause")
} else if {
  "else if"
} // if-else resolves to last evaluted result, or nil

// loops
for true { print("endsless") } // while loop
let result = for let i = 0; i < 10; i = i + 1 {
  // traditional loop
  print(i)
  i
} // for loop resolves to last evaluated value, => result == 9

// break
result = for let i = 0; i < 10; i = i + 1 {
  if i == 3 { break "ohnoez" } // break could optionally pass a value
  i
} // result == "ohnoez"

// arrow chainging, `->`, which pass lhs as first arg of rhs
each(Lx.globals(), fn(x) { print(x) }) // could be rewritten as
Lx.globals()->each(fn(x) { print(x) })

// Lx has a builtin function defined as `_1`
fn _1(cb) { fn(x) { cb(x) } }
// so we could also write above line like below
Lx.globals()->each(_1(print))

```

## Bulitin functions

Lx currently has enough of builtin functions to compile itself, and a few other QOL collection functions, those could be checked with `Lx.globals()`.


# License

MIT License

Copyright (c) 2022 Chung-Yu Chang

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
