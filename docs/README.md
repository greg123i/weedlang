# WeedLang (.weed) Compiler

WeedLang is a small experimental language/compiler targeting Windows x64. It lexes/parses `.weed` source into an AST and emits NASM-compatible x86-64 assembly, then shells out to NASM and uses an internal PE linker to produce a Windows PE executable.

## Components

- **Lexer** (`src/compiler/Lexer.cpp`): Tokenizes `.weed` source.
- **Parser** (`src/compiler/Parser.cpp`): Builds an AST (recursive descent).
- **Codegen** (`src/compiler/Codegen.cpp`): AST visitor that emits NASM assembly.
- **Linker** (`src/compiler/PELinker.cpp`): Direct COFF-to-PE linker that generates Windows executables and import tables.
- **Driver** (`src/main.cpp`): Reads a `.weed` file, runs lex/parse/codegen, writes `output.asm`, then invokes NASM and the internal linker to produce `output.exe`.

## Usage

### Build
```powershell
cmake -S . -B build
cmake --build build -j 4
```

### Run the compiler
```powershell
build\weedc.exe examples\main.weed
```

The build target is currently called `untitled2`, but the emitted executable name is `weedc.exe`.

The compiler writes `output.asm`, `output.obj`, and `output.exe` to the working directory where you run it, unless you pass `-o`.

### CLI
```powershell
build\weedc.exe [options] [input.weed | -]
```

Useful options:

- `-o <file>` set the output executable path
- `--nasm <path>` set the NASM executable path
- `--dump-tokens` print tokens and stop
- `--dump-ast` or `--format` print the pretty AST and stop
- `--no-link` emit assembly only
- `--emit-obj` assemble to `.obj` and stop before PE linking
- `--run` run the built executable after linking
- `-` read source from stdin

## Tests

Unit tests are built and registered with CTest.

```powershell
cmake -S . -B build
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

## Language Notes (Current)

- The current language reference is [docs/SYNTAX.md](SYNTAX.md).
- The bootstrap path is described in [docs/BOOTSTRAP.md](BOOTSTRAP.md).

## Current Limits

The spec and compiler are still evolving. Current limitations worth knowing:

- by-value struct members are not fully supported yet
- `import` syntax exists, but module loading is still minimal
- `macro_rules` is still experimental
- metaprogramming support is improving, but not everything is lowered or reflected yet
- the compiler still targets Windows x64 only
- NASM is required for the current build pipeline
- the emitted output currently goes through `output.asm`, `output.obj`, and `output.exe`

## Note on Assembly
The generated assembly targets Windows x64 and exits via `ExitProcess` from `kernel32.dll`.

## Tooling Requirements

- CMake (>= 3.10)D
- A C++20 toolchain (to build the compiler itself)
- NASM (the path is currently hardcoded in `src/main.cpp`)
- No external linker is required: the compiler includes a minimal internal PE linker.
