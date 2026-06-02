#ifndef COMPILER_LEXER_H
#define COMPILER_LEXER_H

#include "compiler/AST.h"
#include <string>
#include <vector>
#include <memory>

class Lexer {
public:
    explicit Lexer(std::string text, std::string sourceName = "<input>");
    Token getNextToken();
    void setExternalPath(std::string path);
private:
    std::string text_;
    size_t pos_;
    size_t line_;
    size_t column_;
    std::shared_ptr<std::string> sourceName_;

    bool useExternal_ = false;
    std::vector<Token> externalTokens_;
    size_t externalPos_ = 0;

    void runExternalTokenizer(const std::string& exePath);
    char peek(size_t ahead = 0) const;
    void advance();
    Token makeToken(TokenType type, std::string value, size_t line, size_t column) const;
    void skipWhitespaceAndComments();

public:
    static std::string tokenTypeToString(TokenType type);
    static TokenType stringToTokenType(const std::string& typeStr, const std::string& val);
};

#endif // COMPILER_LEXER_H
