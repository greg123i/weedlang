# WeedLang Language Specification

This document describes the syntax and current language surface implemented by the WeedLang compiler in this repository. It is intentionally practical: it matches the parser, lexer, and examples currently in tree.

## File Structure

A `.weed` file is a sequence of top-level statements. Semicolons are used to terminate most statements.

Example:

```weed
fn main() -> int {
    return 0;
}
```

## Lexical Rules

- Whitespace is ignored.
- Line comments start with `//` and run to end of line.
- String literals use double quotes.
- Integer literals are base-10.
- Identifiers use letters, digits, and underscores, and must not start with a digit.

## Types

Supported type forms:

- Signed integers: `i8`, `i16`, `i32`, `i64`
- Unsigned integers: `u8`, `u16`, `u32`, `u64`
- C aliases: `char`, `short`, `int`, `long`, `void`
- Struct names: `Point`, `Node`, etc.
- Pointers: `i8*`, `Point*`, `void*`, `i64**`
- Arrays: `u8[64]`, `Point[8]`

Notes:

- `void` is only valid as a pointer base, such as `void*`.
- Pointer suffixes may repeat.
- Array size must be a constant integer literal at parse time.

## Declarations

### Variables

```weed
i64 count = 10;
Point* p = 0;
```

### Consts

```weed
const i64 N = 42;
const x = 42;
```

The parser currently accepts a simplified `const` form and infers `i32` when no explicit type is given.

### Structs

```weed
struct Point {
    i64 x;
    i64 y;
};
```

Struct members are declared with a type and a name, one per line or statement.

Current restriction:

- by-value struct members are not supported yet; struct fields should be pointers or scalar types.

### Foreign Imports

```weed
foreign "kernel32.dll" WriteFile;
```

This declares an external symbol from a DLL.

### Imports

```weed
import "lib/core.weed";
```

The syntax exists in the language and parser. Module semantics are still limited in the compiler.

### Inline Assembly

```weed
asm {
    mov rax, 0
}
```

The contents are copied into the generated assembly as-is.

## Macros (Experimental)

WeedLang supports a simple Rust-like macro system using `macro_rules!`. Macros operate on tokens and are expanded before parsing.

### Definition

```weed
macro_rules! add1 {
    ($x:expr) => { $x + 1 };
}
```

- `$name:frag` defines a fragment. Supported fragments: `tt` (token tree), `ident` (identifier), `expr` (expression), `type` (type).
- The right-hand side is the replacement token stream.

### Invocation

```weed
add1!(41) // expands to 41 + 1
```

Macros must be defined before they are used.

## Comptime

The `comptime` keyword allows executing code at compile-time. It is often used for constant folding and meta-programming.

```weed
comptime {
    const N = 2 + 3;
}

const X = N * 2; // N is available here
```

- `comptime` blocks are evaluated by the `ConstantEvaluator`.
- Side effects (like `const` declarations) persist in the compiler's environment for subsequent parsing/codegen.
- If a `comptime` block contains an expression, it may be lowered to a constant value.

## Functions

```weed
fn add(i64 a, i64 b) -> i64 {
    return a + b;
}
```

Rules:

- Functions are declared with `fn`.
- Parameters are typed.
- Return type is optional; if omitted, the current compiler uses `void`.
- Function bodies are blocks.

## Statements

Supported statement forms:

- variable declaration
- const declaration
- struct definition
- function definition
- `if` / `else`
- `while`
- `return`
- `import`
- `foreign`
- `comptime`
- block statement
- expression statement

### If

```weed
if x > 0 {
    return x;
} else {
    return 0;
}
```

### While

```weed
while i < 10 {
    i = i + 1;
}
```

### Return

```weed
return expr;
```

If no value is returned, the function returns `void`-style control flow.

### Comptime

```weed
comptime {
    const N = 2 + 3;
}
```

`comptime` is a compile-time phase marker. The compiler lowers it before code generation.

## Expressions

Supported expression operators and forms:

- arithmetic: `+ - * /`
- comparisons: `< > <= >= == !=`
- logical: `&& ||`
- assignment: `lhs = rhs`
- address-of: `&expr`
- dereference: `*expr`
- function call: `name(arg1, arg2)`
- cast: `expr as Type`
- array indexing: `expr[index]`
- member access: `expr.field`
- parenthesized expressions: `(expr)`
- string literals
- number literals

Operator precedence follows the parser implementation:

1. call, index, member access, cast
2. unary `&` and `*`
3. `*` and `/`
4. `+` and `-`
5. relational operators
6. equality operators
7. `&&`
8. `||`
9. assignment

## Pointers

Pointers are first-class values in WeedLang. You can:

- declare them directly
- take addresses with `&`
- dereference with `*`
- pass them to functions
- return them from functions

Example:

```weed
fn main() -> int {
    i64 x = 42;
    i64* p = &x;
    *p = 99;
    return x;
}
```

## Current Practical Restrictions

The language surface is broader than the runtime support in a few places. Current limitations in the compiler include:

- by-value struct members are not fully supported
- `import` syntax exists, but module loading is still minimal
- `macro_rules` is still experimental
- some metaprogramming features are evolving quickly

## Example Program

```weed
foreign "kernel32.dll" GetStdHandle;
foreign "kernel32.dll" WriteFile;

fn main() -> int {
    void* handle = GetStdHandle(0 - 11);
    u32 written = 0;
    i8* msg = "Hello, world!\n";
    WriteFile(handle, msg, 14 as u32, &written, 0);
    0
}
```

