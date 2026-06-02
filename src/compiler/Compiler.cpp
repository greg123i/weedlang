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
    if (opt_.useBootstrap || !opt_.bootstrapTokenizerPath.empty()) {
        lexer.setExternalPath(opt_.bootstrapTokenizerPath);
    }
    std::vector<Token> tokens;
    while (true) {
        Token t = lexer.getNextToken();
        tokens.push_back(t);
        if (t.type == TokenType::END_OF_FILE) break;
    }

    if (!opt_.bootstrapMacroExpanderPath.empty()) {
        std::cout << "Using bootstrap macro expander: " << opt_.bootstrapMacroExpanderPath << "\n";
        std::string tempIn = "temp_macro_in.txt";
        std::string tempOut = "temp_macro_out.txt";
        {
            std::ofstream out(tempIn, std::ios::binary);
            for (const auto& t : tokens) {
                out << t.location.line << ":" << t.location.column << " "
                    << Lexer::tokenTypeToString(t.type) << " " << t.value << "\n";
            }
        }
        std::string cmd = opt_.bootstrapMacroExpanderPath + " < " + tempIn + " > " + tempOut;
        runProcess(cmd);
        
        // Parse expanded tokens back
        std::ifstream in(tempOut);
        std::vector<Token> expanded;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            size_t firstSpace = line.find(' ', colon);
            if (firstSpace == std::string::npos) continue;
            size_t secondSpace = line.find(' ', firstSpace + 1);
            int l = std::stoi(line.substr(0, colon));
            int c = std::stoi(line.substr(colon + 1, firstSpace - colon - 1));
            std::string typeStr = line.substr(firstSpace + 1, (secondSpace == std::string::npos ? std::string::npos : secondSpace - firstSpace - 1));
            std::string val = (secondSpace == std::string::npos ? "" : line.substr(secondSpace + 1));
            
            while (!val.empty() && std::isspace((unsigned char)val.back())) val.pop_back();

            TokenType type = Lexer::stringToTokenType(typeStr, val);
            Token tok(type, val);
            tok.location.line = l;
            tok.location.column = c;
            // Note: file info might be lost or needs to be preserved
            expanded.push_back(tok);
        }
        in.close();
        fs::remove(tempIn);
        fs::remove(tempOut);
        if (!expanded.empty()) tokens = std::move(expanded);
    } else {
        tokens = expandMacros(tokens);
    }

    std::unique_ptr<ASTNode> ast;
    if (opt_.useBootstrap || !opt_.bootstrapParserPath.empty()) {
        ast = Parser::parseExternal(opt_.bootstrapParserPath, tokens);
    } else {
        Parser parser(tokens);
        ast = parser.parse();
        if (!parser.errors().empty()) {
            printErrors(parser.errors());
            throw std::runtime_error("parse errors in " + path);
        }
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
    if (!opt_.bootstrapCompilerPath.empty()) {
        std::cout << "Bypassing to bootstrap compiler: " << opt_.bootstrapCompilerPath << "\n";
        std::string cmd = opt_.bootstrapCompilerPath + " " + opt_.inputPath + " -o " + opt_.outputExe;
        if (opt_.runAfterBuild) cmd += " --run";
        if (opt_.noLink) cmd += " --no-link";
        return runProcess(cmd);
    }

    std::set<std::string> visited;
    std::vector<std::unique_ptr<ASTNode>> allStatements;

    if (opt_.readStdin) {
        std::string input = readAll(std::cin);
        Lexer lexer(input, "<stdin>");
        if (opt_.useBootstrap || !opt_.bootstrapTokenizerPath.empty()) {
            lexer.setExternalPath(opt_.bootstrapTokenizerPath);
        }
        std::vector<Token> tokens;
        while (true) {
            Token t = lexer.getNextToken();
            tokens.push_back(t);
            if (t.type == TokenType::END_OF_FILE) break;
        }
        tokens = expandMacros(tokens);

        std::unique_ptr<ASTNode> ast;
        if (opt_.useBootstrap || !opt_.bootstrapParserPath.empty()) {
            ast = Parser::parseExternal(opt_.bootstrapParserPath, tokens);
        } else {
            Parser parser(tokens);
            ast = parser.parse();
            if (!parser.errors().empty()) {
                printErrors(parser.errors());
                return 1;
            }
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

    if (opt_.dumpTokens) {
        std::ifstream inFile(opt_.inputPath, std::ios::binary);
        std::string input = opt_.readStdin ? readAll(std::cin) : readAll(inFile);
        Lexer lexer(input, opt_.inputPath);
        std::vector<Token> tokens;
        while (true) {
            Token t = lexer.getNextToken();
            tokens.push_back(t);
            if (t.type == TokenType::END_OF_FILE) break;
        }
        tokens = expandMacros(tokens);
        for (const auto& t : tokens) {
            std::cout << t.location.line << ":" << t.location.column << " "
                      << Lexer::tokenTypeToString(t.type) << " " << t.value << "\n";
        }
        return 0;
    }

    std::unique_ptr<ASTNode> ast;
    if (!opt_.bootstrapComptimePath.empty()) {
        std::cout << "Using bootstrap comptime: " << opt_.bootstrapComptimePath << "\n";
        std::string astInput = prettyPrintAst(finalProgram.get());
        std::string tempIn = "temp_comptime_in.txt";
        std::string tempOut = "temp_comptime_out.txt";
        {
            std::ofstream out(tempIn, std::ios::binary);
            out << astInput;
        }
        std::string cmd = opt_.bootstrapComptimePath + " < " + tempIn + " > " + tempOut;
        runProcess(cmd);
        ast = std::move(finalProgram);
        fs::remove(tempIn);
        fs::remove(tempOut);
    } else {
        std::vector<std::string> comptimeErrors;
        ast = lowerComptime(std::move(finalProgram), &comptimeErrors);
        if (!comptimeErrors.empty()) {
            printErrors(comptimeErrors);
            return 1;
        }
    }

    if (opt_.dumpAst) {
        std::cout << prettyPrintAst(ast.get());
        return 0;
    }

    fs::path exePath(opt_.outputExe);
    fs::path asmPath = exePath;
    asmPath.replace_extension(".asm");
    fs::path objPath = exePath;
    objPath.replace_extension(".obj");

    if (!opt_.bootstrapCodegenPath.empty()) {
        std::cout << "Using bootstrap codegen: " << opt_.bootstrapCodegenPath << "\n";
        std::string astInput = prettyPrintAst(ast.get());
        std::string tempIn = "temp_codegen_in.txt";
        {
            std::ofstream out(tempIn, std::ios::binary);
            out << astInput;
        }
        std::string cmd = opt_.bootstrapCodegenPath + " < " + tempIn + " > " + fs::absolute(asmPath).string();
        runProcess(cmd);
        fs::remove(tempIn);

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
            if (!opt_.bootstrapLinkerPath.empty()) {
                std::cout << "Using bootstrap linker: " << opt_.bootstrapLinkerPath << "\n";
                std::string cmd = opt_.bootstrapLinkerPath + " " + fs::absolute(objPath).string() + " -o " + fs::absolute(exePath).string();
                runProcess(cmd);
            } else {
                std::cout << "Linking (internal PE linker)...\n";
                std::string linkErr;
                std::unordered_map<std::string, std::string> imports;
                imports["ExitProcess"] = "kernel32.dll"; 
                if (!linkCoffObjectToPeExe(objPath.string(), exePath.string(), "main", imports, &linkErr)) {
                    std::cerr << "link failed: " << linkErr << "\n";
                    return 1;
                }
            }
        }
    } else {
        NASMVisitor visitor;
        visitor.emitLibraryMode = opt_.emitObjOnly;
        visitor.imports["ExitProcess"] = "kernel32.dll";
        ast->accept(&visitor);

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
            if (!opt_.bootstrapLinkerPath.empty()) {
                std::cout << "Using bootstrap linker: " << opt_.bootstrapLinkerPath << "\n";
                std::string cmd = opt_.bootstrapLinkerPath + " " + fs::absolute(objPath).string() + " -o " + fs::absolute(exePath).string();
                runProcess(cmd);
            } else {
                std::cout << "Linking (internal PE linker)...\n";
                std::string linkErr;
                if (!linkCoffObjectToPeExe(objPath.string(), exePath.string(), "main", visitor.imports, &linkErr)) {
                    std::cerr << "link failed: " << linkErr << "\n";
                    return 1;
                }
            }
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
