# WeedLang (.weed) Compiler

WeedLang is a small experimental language and compiler targeting Windows x64. It lexes and parses `.weed` source into an AST, evaluates compile-time blocks, and emits NASM-compatible x86-64 assembly. It then invokes NASM and uses its internal PE linker to produce a native Windows executable.

## Components

- **Lexer** (`src/compiler/Lexer.cpp`): Tokenizes `.weed` source. Supports external tokenizers for bootstrapping.
- **Macro Expander** (`src/compiler/Macros.cpp`): Expands `macro_rules!` before parsing.
- **Parser** (`src/compiler/Parser.cpp`): Recursive descent parser that builds an AST. Supports external parsers for bootstrapping.
- **Constant Evaluator** (`src/compiler/ConstantEvaluator.cpp`): Lowers `comptime` blocks at compile-time.
- **Codegen** (`src/compiler/Codegen.cpp`): AST visitor that emits x64 NASM assembly.
- **Linker** (`src/compiler/PELinker.cpp`): Internal COFF-to-PE linker that generates Windows executables and import tables.
- **Driver** (`src/main.cpp`): Orchestrates the entire pipeline.

## Usage

### Build
```powershell
cmake -S . -B build
cmake --build build -j 4
```
*Note: In IDE environments like CLion, the output is typically in `cmake-build-debug/` or `cmake-build-release/`.*

### Run the compiler
```powershell
.\cmake-build-debug\weedc.exe examples\hello_world.weed --run
```

The compiler produces `output.asm`, `output.obj`, and `output.exe` by default.

### CLI Options
```powershell
weedc.exe [options] [input.weed | -]
```

- `-o <file>`: Set output executable path (default: `output.exe`).
- `--nasm <path>`: Set NASM executable path (default: `nasm.exe`).
- `--dump-tokens`: Print tokens and exit.
- `--dump-ast`: Print the AST and exit.
- `--no-link`: Emit assembly only, skip NASM and linking.
- `--emit-obj`: Assemble to `.obj` and stop before PE linking.
- `--run`: Run the output executable after a successful build.
- `--bootstrap`: Use default bootstrap tools (`bootstrap_tokenizer.exe`, etc.) for all stages.
- `-`: Read source from stdin.

## Tests

Unit tests cover the core components and can be run via CTest.

```powershell
ctest --test-dir cmake-build-debug --output-on-failure
```

## Language Features

- **FFI**: Direct calls to Windows DLLs via `foreign` declarations.
- **Metaprogramming**: `comptime` and `macro_rules!`.
- **Low-level Control**: Pointers, casts, and inline `asm` blocks.
- **Modular**: `import` support for splitting code across files.

## Dependencies

- **CMake** (>= 3.10)
- **C++20** compatible compiler (MSVC, Clang, or GCC)
- **NASM** (Required for assembly and linking; should be on `PATH` or specified via `--nasm`)

## Note on Linking
`weedc` includes its own PE linker. It automatically resolves imports from `kernel32.dll` and other system libraries when declared with `foreign`. No external linker (like `link.exe`) is required.
