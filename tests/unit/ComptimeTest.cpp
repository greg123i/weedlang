#include "compiler/ConstantEvaluator.h"
#include "compiler/Lexer.h"
#include "compiler/Parser.h"
#include <cassert>
#include <iostream>
#include <vector>

static std::vector<Token> lexAll(const std::string& src) {
    Lexer lexer(src, "<test>");
    std::vector<Token> tokens;
    for (;;) {
        Token t = lexer.getNextToken();
        if (t.type == TokenType::END_OF_FILE) {
            tokens.push_back(t);
            break;
        }
        tokens.push_back(t);
    }
    return tokens;
}

int main() {
    const std::string src =
        "comptime { const N = 2 + 3; }"
        "N + 4";

    Parser parser(lexAll(src));
    auto ast = parser.parse();
    assert(parser.errors().empty());

    std::vector<std::string> diagnostics;
    ast = lowerComptime(std::move(ast), &diagnostics);
    assert(diagnostics.empty());

    auto* program = dynamic_cast<ProgramNode*>(ast.get());
    assert(program);
    assert(program->statements().size() == 1);

    auto* number = dynamic_cast<NumberNode*>(program->statements()[0].get());
    assert(number);
    assert(number->value() == "9");

    std::cout << "Comptime lowering test passed!\n";
    return 0;
}
