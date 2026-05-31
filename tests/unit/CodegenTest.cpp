#include "compiler/Lexer.h"
#include "compiler/Parser.h"
#include "compiler/Codegen.h"
#include <cassert>
#include <iostream>
#include <vector>

static std::vector<Token> lexAll(const std::string& src) {
    Lexer lexer(src);
    std::vector<Token> tokens;
    for (;;) {
        Token t = lexer.getNextToken();
        if (t.type == TokenType::END_OF_FILE) break;
        tokens.push_back(t);
    }
    tokens.push_back({TokenType::END_OF_FILE});
    return tokens;
}

static std::unique_ptr<ASTNode> parse(const std::string& src) {
    auto tokens = lexAll(src);
    Parser parser(tokens);
    return parser.parse();
}

void testStructMemberLoadStoreUsesRbpFrame() {
    const std::string src =
        "struct Point { int x; int y; };"
        "Point p;"
        "p.x = 10;"
        "p.y = 20;"
        "p.x + p.y";

    auto ast = parse(src);
    NASMVisitor v;
    ast->accept(&v);
    std::string asmText = v.asmCode.str();

    // Struct locals and member accesses should use frame-relative negative offsets.
    assert(asmText.find("[rbp - ") != std::string::npos);

    // Load patterns
    bool hasLoad =
        (asmText.find("mov rax, qword [rbp - ") != std::string::npos) ||
        (asmText.find("movsx rax, ") != std::string::npos) ||
        (asmText.find("movsxd rax, ") != std::string::npos);
    assert(hasLoad);

    std::cout << "Codegen struct member load/store (rbp frame) test passed!" << std::endl;
}

void testCallUsesShadowSpaceFrameNotPushArgs() {
    const std::string src =
        "foreign \"kernel32.dll\" SetLastError;"
        "SetLastError(420)";

    auto ast = parse(src);
    NASMVisitor v;
    ast->accept(&v);
    std::string asmText = v.asmCode.str();

    // We should not be using PUSH for passing args anymore.
    assert(asmText.find("push rax") == std::string::npos);
    // Should allocate call frame at least once.
    assert(asmText.find("sub rsp, ") != std::string::npos);
    assert(asmText.find("call SetLastError") != std::string::npos);

    std::cout << "Codegen call-frame/shadow-space test passed!" << std::endl;
}

void testDerefStoreTypedViaCast() {
    const std::string src =
        "char x;"
        "char* p;"
        "p = &x;"
        "*(p as char*) = 65;"
        "*p";

    auto ast = parse(src);
    NASMVisitor v;
    ast->accept(&v);
    std::string asmText = v.asmCode.str();

    // Expect a byte store through [rax] at least once.
    assert(asmText.find("mov byte [rax], bl") != std::string::npos);

    std::cout << "Codegen deref store typed via cast test passed!" << std::endl;
}

int main() {
    testStructMemberLoadStoreUsesRbpFrame();
    testCallUsesShadowSpaceFrameNotPushArgs();
    testDerefStoreTypedViaCast();
    return 0;
}
