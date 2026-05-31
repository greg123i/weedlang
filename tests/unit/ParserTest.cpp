#include "compiler/Lexer.h"
#include "compiler/Parser.h"
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

static ProgramNode* parseProgram(const std::string& src) {
    auto tokens = lexAll(src);
    Parser parser(tokens);
    auto ast = parser.parse();
    auto* program = dynamic_cast<ProgramNode*>(ast.get());
    assert(program && "Expected ProgramNode");
    // Leak is fine in unit test; keeps pointers stable for asserts.
    (void)ast.release();
    return program;
}

void testParsesStructAndMemberAccessAndAssignment() {
    const std::string src =
        "struct Point { int x; int y; };"
        "Point p;"
        "p.x = 10;"
        "p.y = 20;"
        "p.x + p.y";

    std::unique_ptr<ProgramNode> program(parseProgram(src));
    const auto& stmts = program->statements();
    assert(stmts.size() == 5);

    assert(dynamic_cast<StructDefNode*>(stmts[0].get()));
    assert(dynamic_cast<VarDeclNode*>(stmts[1].get()));

    auto* a1 = dynamic_cast<AssignNode*>(stmts[2].get());
    assert(a1);
    assert(dynamic_cast<MemberAccessNode*>(a1->target()));
    assert(dynamic_cast<NumberNode*>(a1->value()));

    auto* a2 = dynamic_cast<AssignNode*>(stmts[3].get());
    assert(a2);
    assert(dynamic_cast<MemberAccessNode*>(a2->target()));
    assert(dynamic_cast<NumberNode*>(a2->value()));

    auto* sum = dynamic_cast<BinaryOpNode*>(stmts[4].get());
    assert(sum);
    assert(sum->op() == TokenType::PLUS);
    assert(dynamic_cast<MemberAccessNode*>(sum->left()));
    assert(dynamic_cast<MemberAccessNode*>(sum->right()));

    std::cout << "Parser struct/member/assignment test passed!" << std::endl;
}

void testDoesNotMisparseExpressionAsVarDecl() {
    // Historically `IDENTIFIER ...` could be treated as a struct type name, breaking expression statements.
    const std::string src = "foo(1, 2) + 3";
    std::unique_ptr<ProgramNode> program(parseProgram(src));
    const auto& stmts = program->statements();
    assert(stmts.size() == 1);

    auto* expr = dynamic_cast<BinaryOpNode*>(stmts[0].get());
    assert(expr);
    assert(dynamic_cast<CallNode*>(expr->left()));

    std::cout << "Parser decl/expr disambiguation test passed!" << std::endl;
}

void testVoidPointerDeclParses() {
    const std::string src = "void* p;";
    std::unique_ptr<ProgramNode> program(parseProgram(src));
    const auto& stmts = program->statements();
    assert(stmts.size() == 1);
    auto* decl = dynamic_cast<VarDeclNode*>(stmts[0].get());
    assert(decl);
    assert(decl->type().baseType == DataType::VOID);
    assert(decl->type().pointerLevel == 1);
    std::cout << "Parser void* declaration test passed!" << std::endl;
}

int main() {
    testParsesStructAndMemberAccessAndAssignment();
    testDoesNotMisparseExpressionAsVarDecl();
    testVoidPointerDeclParses();

    // Smoke: function + return parsing
    {
        const std::string src = "fn main() -> int { return 7; }";
        std::unique_ptr<ProgramNode> program(parseProgram(src));
        const auto& stmts = program->statements();
        assert(stmts.size() == 1);
        auto* fn = dynamic_cast<FuncDefNode*>(stmts[0].get());
        assert(fn);
        assert(fn->name() == "main");
    }
    return 0;
}
