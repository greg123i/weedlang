#include "compiler/Compiler.h"
#include "compiler/Lexer.h"
#include "compiler/Parser.h"
#include "compiler/Codegen.h"
#include "compiler/ConstantEvaluator.h"
#include "compiler/Macros.h"
#include "compiler/PELinker.h"
#include "compiler/PrettyPrinter.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

Compiler::Compiler(Options opt) : opt_(std::move(opt)) {}

std::string Compiler::readAll(std::istream& in) {
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void Compiler::printErrors(const std::vector<std::string>& errors) {
    for (const auto& err : errors) {
        std::cerr << err << "\n";
    }
}

int Compiler::runProcess(const std::string& commandLine) {
    return std::system(commandLine.c_str());
}

std::vector<std::unique_ptr<ASTNode>> Compiler::loadAndParseRecursive(const std::string& path, std::set<std::string>& visited) {
    std::string absolutePath = fs::absolute(path).string();
    if (visited.count(absolutePath)) return {};
    visited.insert(absolutePath);

    std::ifstream inFile(path, std::ios::binary);
    if (!inFile) {
        throw std::runtime_error("could not open file: " + path);
    }
    std::string input = readAll(inFile);

    Lexer lexer(input, path);
    std::vector<Token> tokens;
    while (true) {
        Token t = lexer.getNextToken();
        tokens.push_back(t);
        if (t.type == TokenType::END_OF_FILE) break;
    }

    tokens = expandMacros(tokens);

    Parser parser(tokens);
    auto ast = parser.parse();
    if (!parser.errors().empty()) {
        printErrors(parser.errors());
        throw std::runtime_error("parse errors in " + path);
    }

    auto program = static_cast<ProgramNode*>(ast.get());
    std::vector<std::unique_ptr<ASTNode>> allStmts;

    auto& originalStmts = program->statements();
    for (size_t i = 0; i < originalStmts.size(); ++i) {
        if (originalStmts[i]->kind() == NodeKind::Import) {
            auto imp = static_cast<ImportNode*>(originalStmts[i].get());
            auto importedStmts = loadAndParseRecursive(imp->path(), visited);
            for (size_t j = 0; j < importedStmts.size(); ++j) {
                allStmts.push_back(std::move(importedStmts[j]));
            }
        }
    }

    for (size_t i = 0; i < originalStmts.size(); ++i) {
        if (originalStmts[i]->kind() != NodeKind::Import) {
            allStmts.push_back(std::move(const_cast<std::unique_ptr<ASTNode>&>(originalStmts[i])));
        }
    }

    return allStmts;
}

int Compiler::run() {
    std::set<std::string> visited;
    std::vector<std::unique_ptr<ASTNode>> allStatements;

    if (opt_.readStdin) {
        std::string input = readAll(std::cin);
        Lexer lexer(input, "<stdin>");
        std::vector<Token> tokens;
        while (true) {
            Token t = lexer.getNextToken();
            tokens.push_back(t);
            if (t.type == TokenType::END_OF_FILE) break;
        }
        tokens = expandMacros(tokens);
        Parser parser(tokens);
        auto ast = parser.parse();
        if (!parser.errors().empty()) {
            printErrors(parser.errors());
            return 1;
        }
        auto program = static_cast<ProgramNode*>(ast.get());
        auto& stmts = program->statements();
        for (size_t i = 0; i < stmts.size(); ++i) {
            allStatements.push_back(std::move(const_cast<std::unique_ptr<ASTNode>&>(stmts[i])));
        }
    } else {
        allStatements = loadAndParseRecursive(opt_.inputPath, visited);
    }

    auto finalProgram = std::make_unique<ProgramNode>(std::move(allStatements));

    std::vector<std::string> comptimeErrors;
    auto ast = lowerComptime(std::move(finalProgram), &comptimeErrors);
    if (!comptimeErrors.empty()) {
        printErrors(comptimeErrors);
        return 1;
    }

    if (opt_.dumpAst) {
        std::cout << prettyPrintAst(ast.get());
        return 0;
    }

    NASMVisitor visitor;
    visitor.emitLibraryMode = opt_.emitObjOnly;
    visitor.imports["ExitProcess"] = "kernel32.dll";
    ast->accept(&visitor);

    fs::path exePath(opt_.outputExe);
    fs::path asmPath = exePath;
    asmPath.replace_extension(".asm");
    fs::path objPath = exePath;
    objPath.replace_extension(".obj");

    {
        std::ofstream asmFile(asmPath, std::ios::binary);
        if (!asmFile) {
            std::cerr << "could not write assembly file: " << asmPath.string() << "\n";
            return 1;
        }
        for (const auto& [func, lib] : visitor.imports) {
            asmFile << "extern " << func << "\n";
        }
        asmFile << "section .text\n";
        if (!opt_.emitObjOnly) {
            asmFile << "global main\n";
        }
        asmFile << visitor.asmCode.str();
    }

    if (!opt_.noLink) {
        std::cout << "Assembling with NASM...\n";
        std::string nasmCmd = opt_.nasmPath + " -f win64 " + fs::absolute(asmPath).string() + " -o " + fs::absolute(objPath).string();
        int nasmRc = runProcess(nasmCmd);
        if (nasmRc != 0) {
            std::cerr << "NASM failed with exit code " << nasmRc << "\n";
            return 1;
        }
    }

    if (!opt_.noLink && !opt_.emitObjOnly) {
        std::cout << "Linking (internal PE linker)...\n";
        std::string linkErr;
        if (!linkCoffObjectToPeExe(objPath.string(), exePath.string(), "main", visitor.imports, &linkErr)) {
            std::cerr << "link failed: " << linkErr << "\n";
            return 1;
        }
    }

    if (opt_.runAfterBuild) {
        int runRc = runProcess(fs::absolute(exePath).string());
        if (runRc != 0) {
            std::cerr << "program exited with status " << runRc << "\n";
            return 1;
        }
    }

    if (opt_.emitObjOnly) {
        std::cout << "Object emitted to '" << objPath.string() << "'.\n";
    } else if (opt_.noLink) {
        std::cout << "Assembly emitted to '" << asmPath.string() << "'.\n";
    } else {
        std::cout << "Compilation complete. Generated '" << exePath.string() << "'.\n";
    }
    return 0;
}
