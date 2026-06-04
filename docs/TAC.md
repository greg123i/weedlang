# TAC IR For WeedLang

This is the recommended next internal form for a first-generation WeedLang compiler.

## Why TAC

Once WeedLang can compile itself in stages, direct AST-to-assembly codegen becomes the wrong place to keep adding features. A simple three-address IR makes the compiler easier to:

- lower
- optimize
- debug
- retarget

## Shape

Use a linear, typed, three-address representation.

Example:

```text
func main() -> i64
entry:
  t0 = const i64 2
  t1 = const i64 3
  t2 = add i64 t0, t1
  ret i64 t2
end
```

## Core instruction families

- `const`
- `copy`
- arithmetic: `add`, `sub`, `mul`, `div`
- comparisons: `lt`, `le`, `gt`, `ge`, `eq`, `ne`
- boolean ops: `and`, `or`, `not`
- memory ops: `load`, `store`, `addr`
- control flow: `jmp`, `cjmp`, `label`
- calls: `call`, `ret`

## Types in TAC

Keep the same low-level type set WeedLang already uses:

- integer widths
- pointers
- structs as named layouts
- arrays as sized storage

Do not invent a richer type system unless the frontend already needs it.

## Lowering Order

The clean frontend pipeline is:

1. lex
2. parse
3. macro expansion
4. comptime lowering
5. TAC lowering
6. optimization
7. backend codegen

That gives you a stable place to add features without changing assembly emission every time.

## What TAC should carry

Each instruction should know:

- result type
- operand list
- source location
- optional symbol name

That is enough for diagnostics and latter optimization passes.

