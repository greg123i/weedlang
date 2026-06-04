# WeedLang

WeedLang is a small, experimental systems programming language and compiler targeting Windows x64. It is designed to be a lightweight alternative for low-level tasks, with a focus on simplicity, metaprogramming, and self-hosting.

The compiler is written in C++20 and features a custom end-to-end pipeline, including a recursive descent parser, macro expansion, compile-time evaluation, and an internal PE linker.

## Features

- **X64 Targeting:** Generates native Windows x64 assembly (NASM compatible).
- **Internal PE Linker:** Produces standalone `.exe` files without requiring an external linker.
- **FFI Support:** Easily call Windows API or any other DLL functions.
- **Metaprogramming:** Rust-like `macro_rules!` and `comptime` blocks for compile-time logic.
- **Systems-Level Primitives:** Direct support for pointers, structs, and manual memory management.
- **Bootstrap Ready:** Designed with a modular architecture to facilitate future self-hosting.

## Documentation

Detailed information can be found in the `docs/` directory:

- [README.md](docs/README.md) - Project overview and usage instructions.
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) - Deep dive into the compiler's internal pipeline.
- [SYNTAX.md](docs/SYNTAX.md) - Language reference and grammar.
- [BOOTSTRAP.md](docs/BOOTSTRAP.md) - Roadmap for self-hosting.
- [TAC.md](docs/TAC.md) - Proposal for a Three-Address Code intermediate representation.

## Getting Started

See [docs/README.md](docs/README.md) for build instructions and basic usage examples.
