#include "compiler/Lexer.h"
#include <iostream>
#include <cassert>

void testLexer() {
    Lexer lexer("123 + 456");
    Token t1 = lexer.getNextToken();
    assert(t1.type == TokenType::NUMBER && t1.value == "123");
    
    Token t2 = lexer.getNextToken();
    assert(t2.type == TokenType::PLUS);
    
    Token t3 = lexer.getNextToken();
    assert(t3.type == TokenType::NUMBER && t3.value == "456");
    
    std::cout << "Lexer unit test passed!" << std::endl;
}

void testStructLexing() {
    Lexer lexer("struct Point { int x; int y; };");
    assert(lexer.getNextToken().type == TokenType::STRUCT);
    assert(lexer.getNextToken().type == TokenType::IDENTIFIER); // Point
    assert(lexer.getNextToken().type == TokenType::LBRACE);
    assert(lexer.getNextToken().type == TokenType::INT_TYPE);
    assert(lexer.getNextToken().type == TokenType::IDENTIFIER); // x
    assert(lexer.getNextToken().type == TokenType::SEMICOLON);
    assert(lexer.getNextToken().type == TokenType::INT_TYPE);
    assert(lexer.getNextToken().type == TokenType::IDENTIFIER); // y
    assert(lexer.getNextToken().type == TokenType::SEMICOLON);
    assert(lexer.getNextToken().type == TokenType::RBRACE);
    assert(lexer.getNextToken().type == TokenType::SEMICOLON);

    std::cout << "Struct lexing test passed!" << std::endl;
}

void testAsmLexing() {
    Lexer lexer("asm { mov rax, 123 }");
    Token t1 = lexer.getNextToken();
    assert(t1.type == TokenType::ASM_BLOCK);
    assert(t1.value.find("mov rax") != std::string::npos);

    std::cout << "ASM block lexing test passed!" << std::endl;
}

void testTypeKeywordLexing() {
    Lexer lexer("char short int long void");
    assert(lexer.getNextToken().type == TokenType::CHAR_TYPE);
    assert(lexer.getNextToken().type == TokenType::SHORT_TYPE);
    assert(lexer.getNextToken().type == TokenType::INT_TYPE);
    assert(lexer.getNextToken().type == TokenType::LONG_TYPE);
    assert(lexer.getNextToken().type == TokenType::VOID_TYPE);

    std::cout << "Type keyword lexing test passed!" << std::endl;
}

void testMacroLexing() {
    Lexer lexer("macro_rules! m { ($x) => { $x }; } m!(1)");
    assert(lexer.getNextToken().type == TokenType::MACRO_RULES);
    assert(lexer.getNextToken().type == TokenType::BANG);
    assert(lexer.getNextToken().type == TokenType::IDENTIFIER);
    assert(lexer.getNextToken().type == TokenType::LBRACE);
    // ... not exhaustive; just smoke that tokens exist and lexer doesn't choke.
    std::cout << "Macro lexing smoke test passed!" << std::endl;
}

void testControlKeywordLexing() {
    Lexer lexer("fn return if else while");
    assert(lexer.getNextToken().type == TokenType::FN);
    assert(lexer.getNextToken().type == TokenType::RETURN);
    assert(lexer.getNextToken().type == TokenType::IF);
    assert(lexer.getNextToken().type == TokenType::ELSE);
    assert(lexer.getNextToken().type == TokenType::WHILE);
    std::cout << "Control keyword lexing test passed!" << std::endl;
}

int main() {
    testLexer();
    testStructLexing();
    testAsmLexing();
    testTypeKeywordLexing();
    testMacroLexing();
    testControlKeywordLexing();
    return 0;
}
