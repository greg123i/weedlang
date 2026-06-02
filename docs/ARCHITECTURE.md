# WeedLang Compiler Architecture

This document describes the internal structure and pipeline of the WeedLang compiler (`weedc`).

## Compiler Pipeline

The compiler follows a linear pipeline from source code to a Windows PE executable:

1.  **Lexing (`src/compiler/Lexer.cpp`)**: Converts raw source text into a stream of tokens.
2.  **Macro Expansion (`src/compiler/Macros.cpp`)**: A pre-parsing phase that expands Rust-like `macro_rules!` definitions and invocations. This operates on the token stream.
3.  **Parsing (`src/compiler/Parser.cpp`)**: Performs recursive descent parsing on the token stream to build an Abstract Syntax Tree (AST).
4.  **Import Resolution (`src/compiler/Compiler.cpp`)**: Recursively loads and parses imported `.weed` files, merging their statements into the main program AST.
5.  **Comptime Lowering (`src/compiler/ConstantEvaluator.cpp`)**: Evaluates `comptime` blocks and replaces them with their results (e.g., constant values).
6.  **Assembly Generation (`src/compiler/Codegen.cpp`)**: A visitor-based code generator that traverses the AST and emits x86-64 NASM-compatible assembly.
7.  **Assembler (`NASM`)**: The compiler shells out to NASM to convert the generated assembly into a COFF object file (`.obj`).
8.  **Linking (`src/compiler/PELinker.cpp`)**: An internal PE linker that takes the COFF object, resolves external imports (e.g., from `kernel32.dll`), and generates a final Windows PE executable (`.exe`).

## Core Components

### AST (`include/compiler/AST.h`)
The AST is the central representation of the program. It consists of `ASTNode` subclasses for statements (e.g., `IfNode`, `WhileNode`, `FunctionNode`) and expressions (e.g., `BinaryExpressionNode`, `CallNode`).

### Constant Evaluator (`src/compiler/ConstantEvaluator.cpp`)
This component provides a limited execution environment for compile-time logic. It can evaluate constant expressions and simple blocks within `comptime` scopes.

### PELinker (`src/compiler/PELinker.cpp`)
A custom linker designed to avoid dependency on large external linkers like `link.exe` or `lld`. It:
- Parses the COFF object produced by NASM.
- Creates a standard PE header.
- Builds an Import Address Table (IAT) for DLL functions declared via `foreign`.
- Maps sections (`.text`, `.data`, etc.) into the PE structure.

## Technical Choices

- **Language**: C++17 for the compiler implementation.
- **Backend**: NASM was chosen for its simplicity and human-readable output, which helps in debugging the code generator.
- **Linker**: A custom internal linker ensures that `weedc` can produce working executables on Windows without requiring the Visual Studio build tools to be installed.