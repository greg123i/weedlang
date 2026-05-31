#include "compiler/Compiler.h"
#include <iostream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

static void printUsage(const char* exe) {
    std::cerr
        << "Usage: " << exe << " [options] [input.weed | -]\n"
        << "Options:\n"
        << "  -o <file>         Set output executable path (default: output.exe)\n"
        << "  --nasm <path>     Set NASM executable path\n"
        << "  --dump-tokens     Print tokens and exit\n"
        << "  --dump-ast        Print pretty AST and exit\n"
        << "  --format          Alias for --dump-ast\n"
        << "  --no-link         Emit assembly only, skip NASM/link\n"
        << "  --emit-obj        Assemble to .obj and stop before PE linking\n"
        << "  --run             Run the output executable after build\n"
        << "  --help            Show this help\n";
}

int main(int argc, char* argv[]) {
    Compiler::Options opt;

    // Default NASM path
    if (const char* env = std::getenv("NASM_PATH")) {
        opt.nasmPath = env;
    } else {
        const fs::path legacy = R"(C:\Users\QWE\AppData\Local\bin\NASM\nasm.exe)";
        if (fs::exists(legacy)) {
            opt.nasmPath = legacy.string();
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-o") {
            if (i + 1 >= argc) { std::cerr << "missing value for -o\n"; return 1; }
            opt.outputExe = argv[++i];
        } else if (arg == "--nasm") {
            if (i + 1 >= argc) { std::cerr << "missing value for --nasm\n"; return 1; }
            opt.nasmPath = argv[++i];
        } else if (arg == "--dump-tokens") {
            opt.dumpTokens = true;
        } else if (arg == "--dump-ast" || arg == "--format") {
            opt.dumpAst = true;
        } else if (arg == "--no-link") {
            opt.noLink = true;
        } else if (arg == "--emit-obj") {
            opt.emitObjOnly = true;
        } else if (arg == "--run") {
            opt.runAfterBuild = true;
        } else if (arg == "-") {
            opt.readStdin = true;
            opt.inputPath = "-";
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            return 1;
        } else {
            opt.inputPath = arg;
            opt.readStdin = false;
        }
    }

    if (opt.inputPath.empty() && !opt.readStdin) {
        printUsage(argv[0]);
        return 1;
    }

    try {
        Compiler compiler(opt);
        return compiler.run();
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
