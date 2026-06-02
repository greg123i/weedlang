#ifndef COMPILER_PARSER_H
#define COMPILER_PARSER_H

#include "compiler/AST.h"
#include <vector>
#include <memory>

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);
    std::unique_ptr<ASTNode> parse();
    static std::unique_ptr<ASTNode> parseExternal(const std::string& exePath, const std::vector<Token>& tokens);
    const std::vector<std::string>& errors() const;
private:
    std::vector<Token> tokens_;
    size_t pos_;
    Token currentToken_;
    std::vector<std::string> errors_;
    void eat();
    TokenType peekType(size_t ahead = 1) const;
    const Token& previousToken() const;
    std::runtime_error errorAt(const Token& token, const std::string& message) const;
    void report(const Token& token, const std::string& message);
    void synchronize();

    template<typename T, typename... Args>
    std::unique_ptr<T> makeNode(const Token& startToken, Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        node->setLocation(startToken.location);
        return node;
    }

    Type parseType();
    std::unique_ptr<ASTNode> parseArrayType(Type base);
    std::unique_ptr<ASTNode> parseStatement();
    std::unique_ptr<BlockNode> parseBlock();
    std::unique_ptr<ASTNode> parseFactor();
    std::unique_ptr<ASTNode> parseTerm(); // multiplicative: * /
    std::unique_ptr<ASTNode> parseAdditive(); // + -
    std::unique_ptr<ASTNode> parseRelational(); // < > <= >=
    std::unique_ptr<ASTNode> parseEquality(); // == !=
    std::unique_ptr<ASTNode> parseLogicalAnd(); // &&
    std::unique_ptr<ASTNode> parseLogicalOr(); // ||
    std::unique_ptr<ASTNode> parseExpression();
    std::unique_ptr<ASTNode> parseAssignment();
};

#endif // COMPILER_PARSER_H
