we want typecheck, and a lsp that helps.
we probably will introduce a record type, or more of a struct in our case.
(since generally record means pure data, and struct means data with behavior)

but we also want to skip type declaration; having type declaration will
make lx a very different language than it is today.

so for case like
```lx
fn foo(bar, baz) {.{
  bar: bar,
  baz: baz,
}}
```
we will treat the first usage of such function as their type declaration.

for example, if we call above function with `foo("x", 123)`, `bar` became type
`string` and `baz` became type `number`.
and we will use that info to typecheck the subsequent `foo` calls.

and to make our life easier, let's having a different syntax for `struct`;
we want to defferenciate hashmap usage from struct.

since struct will always use `dot` access, let's make `.{}` as literal struct syntax.
and hashmap would be `#{}`, since it's a hash table.

