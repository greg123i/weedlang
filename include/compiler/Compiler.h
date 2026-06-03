#ifndef COMPILER_COMPILER_H
#define COMPILER_COMPILER_H

#include "compiler/AST.h"
#include <string>
#include <vector>
#include <set>
#include <memory>

class Compiler {
public:
    struct Options {
        std::string inputPath;
        std::string outputExe = "output.exe";
        std::string nasmPath = "nasm.exe";
        bool readStdin = false;
        bool dumpTokens = false;
        bool dumpAst = false;
        bool noLink = false;
        bool emitObjOnly = false;
        bool runAfterBuild = false;
        bool useBootstrap = false;
        std::string bootstrapTokenizerPath = "bootstrap_tokenizer.cmd";
        std::string bootstrapParserPath = "bootstrap_parser.exe";
        std::string bootstrapComptimePath;
        std::string bootstrapCodegenPath;
        std::string bootstrapMacroExpanderPath;
        std::string bootstrapLinkerPath;
        std::string bootstrapCompilerPath;
    };

    explicit Compiler(Options opt);
    int run();

private:
    std::vector<std::unique_ptr<ASTNode>> loadAndParseRecursive(const std::string& path, std::set<std::string>& visited);
    std::string readAll(std::istream& in);
    void printErrors(const std::vector<std::string>& errors);
    int runProcess(const std::string& commandLine);

    Options opt_;
};

#endif // COMPILER_COMPILER_H
