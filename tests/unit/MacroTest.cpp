#include "compiler/Lexer.h"
#include "compiler/Macros.h"
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

void testMacroRulesSimple() {
    const std::string src =
        "macro_rules! add1 { ($x:expr) => { $x + 1 }; }"
        "add1!(2)";

    auto toks = lexAll(src);
    toks = expandMacros(toks);

    // Expect: 2 + 1 EOF
    size_t i = 0;
    assert(toks[i++].type == TokenType::NUMBER && toks[i - 1].value == "2");
    assert(toks[i++].type == TokenType::PLUS);
    assert(toks[i++].type == TokenType::NUMBER && toks[i - 1].value == "1");
    assert(toks[i++].type == TokenType::END_OF_FILE);

    std::cout << "Macro simple expansion test passed!" << std::endl;
}

void testMacroRepeatCommaSeparated() {
    const std::string src =
        "macro_rules! passthru { ( $( $x:tt),* ) => { $x }; }"
        "passthru!(1, 2, 3)";

    auto toks = lexAll(src);
    toks = expandMacros(toks);

    // Expect: 1 , 2 , 3 EOF
    size_t i = 0;
    assert(toks[i++].type == TokenType::NUMBER && toks[i - 1].value == "1");
    assert(toks[i++].type == TokenType::COMMA);
    assert(toks[i++].type == TokenType::NUMBER && toks[i - 1].value == "2");
    assert(toks[i++].type == TokenType::COMMA);
    assert(toks[i++].type == TokenType::NUMBER && toks[i - 1].value == "3");
    assert(toks[i++].type == TokenType::END_OF_FILE);

    std::cout << "Macro repetition test passed!" << std::endl;
}

void testMacroIdentFragment() {
    const std::string src =
        "macro_rules! id { ($x:ident) => { $x }; }"
        "id!(hello)";

    auto toks = expandMacros(lexAll(src));
    assert(toks.size() >= 2);
    assert(toks[0].type == TokenType::IDENTIFIER && toks[0].value == "hello");
    std::cout << "Macro ident fragment test passed!" << std::endl;
}

void testMacroHygieneGensym() {
    const std::string src =
        "macro_rules! mk { () => { __tmp + 1 }; }"
        "mk!()";

    auto toks = expandMacros(lexAll(src));
    assert(toks.size() >= 3);
    assert(toks[0].type == TokenType::IDENTIFIER);
    assert(toks[0].value.rfind("__tmp_", 0) == 0);
    std::cout << "Macro hygiene gensym test passed!" << std::endl;
}

int main() {
    testMacroRulesSimple();
    testMacroRepeatCommaSeparated();
    testMacroIdentFragment();
    testMacroHygieneGensym();
    return 0;
}
