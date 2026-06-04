# Bootstrapping Weedc In WeedLang

Weedc can be rewritten in WeedLang, but only incrementally. The compiler supports an external bootstrap mode where each stage (lexer, parser, etc.) can be replaced by an external executable.

## Bootstrap Mechanism

The compiler driver (`src/compiler/Compiler.cpp`) can be configured to shell out to external tools for specific tasks:

- **`--bootstrap`**: Enables bootstrapping using default tool names (e.g., `bootstrap_parser.exe`).
- **`--bootstrap-parser <path>`**: Uses a specific executable as the parser.
- **`--bootstrap-tokenizer <path>`**: Uses a specific executable as the tokenizer.

When an external tool is used, the compiler communicates with it via temporary files containing token streams or AST dumps. If an external tool is missing or fails, the compiler will report an error and abort, rather than producing empty output.

## Roadmap

1. Keep the current C++ compiler as stage 0.
2. Move runtime helpers into `lib/core.weed`.
3. Build a small stage-1 tool in WeedLang that can read files, tokenize them, and print diagnostics.
4. Reimplement parser and AST utilities in WeedLang.
5. Move compile-time lowering and supporting passes next.
6. Leave codegen and linking for later, once the front end is stable.
7. Insert a TAC IR layer before backend codegen.

## What `core.weed` is for

`lib/core.weed` is the standard runtime layer:

- Windows API bindings
- string helpers
- memory helpers
- console I/O
- file I/O

That file is enough to write small tools in WeedLang without reimplementing the OS-facing parts each time.

## What should be rewritten first

The first useful self-hosted pieces are:

- a tokenizer
- a diagnostic printer
- a simple parser for declarations and expressions
- AST pretty-printing
- a TAC lowering pass

That gives you a compiler front end that can explain itself before it can fully replace the C++ compiler.

## What not to try first

Do not try to rewrite the whole compiler in one pass.

The early blockers are not syntax. They are:

- stable file I/O
- reliable command-line handling
- source locations
- error reporting
- a clean module story

Once those are solid, the rest can be moved over piece by piece.

## TAC IR

The suggested intermediate form is documented in [docs/TAC.md](TAC.md). In short: use a small, typed, linear three-address IR between the parser and NASM codegen.
