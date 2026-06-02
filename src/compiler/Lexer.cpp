#include "compiler/Lexer.h"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdio>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

Lexer::Lexer(std::string text, std::string sourceName)
    : text_(std::move(text)), pos_(0), line_(1), column_(1), sourceName_(std::make_shared<std::string>(std::move(sourceName))) {
    if (text_.length() >= 3 &&
        (unsigned char)text_[0] == 0xEF &&
        (unsigned char)text_[1] == 0xBB &&
        (unsigned char)text_[2] == 0xBF) {
        pos_ = 3;
    }
}

void Lexer::setExternalPath(std::string path) {
    if (!path.empty() && std::filesystem::exists(path)) {
        useExternal_ = true;
        runExternalTokenizer(path);
    }
}

TokenType Lexer::stringToTokenType(const std::string& typeStr, const std::string& val) {
    if (typeStr == "KEYWORD") {
        if (val == "fn") return TokenType::FN;
        if (val == "return") return TokenType::RETURN;
        if (val == "if") return TokenType::IF;
        if (val == "else") return TokenType::ELSE;
        if (val == "while") return TokenType::WHILE;
        if (val == "struct") return TokenType::STRUCT;
        if (val == "const") return TokenType::CONST;
        if (val == "import") return TokenType::IMPORT;
        if (val == "foreign") return TokenType::FOREIGN;
        if (val == "comptime") return TokenType::COMPTIME;
        if (val == "macro_rules") return TokenType::MACRO_RULES;
        if (val == "char") return TokenType::CHAR_TYPE;
        if (val == "short") return TokenType::SHORT_TYPE;
        if (val == "int") return TokenType::INT_TYPE;
        if (val == "long") return TokenType::LONG_TYPE;
        if (val == "i8") return TokenType::I8_TYPE;
        if (val == "i16") return TokenType::I16_TYPE;
        if (val == "i32") return TokenType::I32_TYPE;
        if (val == "i64") return TokenType::I64_TYPE;
        if (val == "u8") return TokenType::U8_TYPE;
        if (val == "u16") return TokenType::U16_TYPE;
        if (val == "u32") return TokenType::U32_TYPE;
        if (val == "u64") return TokenType::U64_TYPE;
        if (val == "void") return TokenType::VOID_TYPE;
        if (val == "as") return TokenType::AS;
    } else if (typeStr == "IDENT") {
        return TokenType::IDENTIFIER;
    } else if (typeStr == "NUMBER") {
        return TokenType::NUMBER;
    } else if (typeStr == "STRING") {
        return TokenType::STRING_LITERAL;
    } else if (typeStr == "PUNC") {
        if (val == "+") return TokenType::PLUS;
        if (val == "-") return TokenType::MINUS;
        if (val == "*") return TokenType::MULTIPLY;
        if (val == "/") return TokenType::DIVIDE;
        if (val == "(") return TokenType::LPAREN;
        if (val == ")") return TokenType::RPAREN;
        if (val == "{") return TokenType::LBRACE;
        if (val == "}") return TokenType::RBRACE;
        if (val == "[") return TokenType::LBRACKET;
        if (val == "]") return TokenType::RBRACKET;
        if (val == ",") return TokenType::COMMA;
        if (val == ";") return TokenType::SEMICOLON;
        if (val == ":") return TokenType::COLON;
        if (val == ".") return TokenType::DOT;
        if (val == "=") return TokenType::EQUAL;
        if (val == "!") return TokenType::BANG;
        if (val == "$") return TokenType::DOLLAR;
        if (val == "->") return TokenType::ARROW;
        if (val == "=>") return TokenType::FAT_ARROW;
        if (val == "==") return TokenType::DOUBLE_EQUAL;
        if (val == "!=") return TokenType::NOT_EQUAL;
        if (val == "<") return TokenType::LESS;
        if (val == ">") return TokenType::GREATER;
        if (val == "<=") return TokenType::LESS_EQUAL;
        if (val == ">=") return TokenType::GREATER_EQUAL;
        if (val == "&&") return TokenType::LOGICAL_AND;
        if (val == "||") return TokenType::LOGICAL_OR;
        if (val == "&") return TokenType::AMPERSAND;
    } else if (typeStr == "EOF") {
        return TokenType::END_OF_FILE;
    }
    return TokenType::UNKNOWN;
}

void Lexer::runExternalTokenizer(const std::string& exePath) {
    // Write text to a temporary input file
    std::string tempInPath = "temp_in.weed";
    std::string tempOutPath = "temp_out.txt";
    {
        std::ofstream out(tempInPath, std::ios::binary);
        out << text_;
    }

    std::string cmd = exePath + " " + tempInPath + " > " + tempOutPath + " 2> nul";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        useExternal_ = false;
        std::filesystem::remove(tempInPath);
        std::filesystem::remove(tempOutPath);
        return;
    }

    std::ifstream in(tempOutPath);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        // Parse: LINE:COL TYPE VALUE
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        size_t firstSpace = line.find(' ', colon);
        if (firstSpace == std::string::npos) continue;
        size_t secondSpace = line.find(' ', firstSpace + 1);
        
        int l = std::stoi(line.substr(0, colon));
        int c = std::stoi(line.substr(colon + 1, firstSpace - colon - 1));
        std::string typeStr = line.substr(firstSpace + 1, (secondSpace == std::string::npos ? std::string::npos : secondSpace - firstSpace - 1));
        std::string val = (secondSpace == std::string::npos ? "" : line.substr(secondSpace + 1));

        // Trim trailing spaces from val
        while (!val.empty() && std::isspace((unsigned char)val.back())) val.pop_back();

        TokenType type = stringToTokenType(typeStr, val);
        externalTokens_.push_back(makeToken(type, val, l, c));
    }

    in.close();
    std::filesystem::remove(tempInPath);
    std::filesystem::remove(tempOutPath);
}

char Lexer::peek(size_t ahead) const {
    size_t idx = pos_ + ahead;
    if (idx >= text_.size()) return '\0';
    return text_[idx];
}

void Lexer::advance() {
    if (pos_ >= text_.size()) return;
    if (text_[pos_] == '\n') {
        pos_++;
        line_++;
        column_ = 1;
        return;
    }
    pos_++;
    column_++;
}

std::string Lexer::tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::IDENTIFIER: return "IDENT";
        case TokenType::STRING_LITERAL: return "STRING";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::MULTIPLY: return "MULTIPLY";
        case TokenType::DIVIDE: return "DIVIDE";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::LBRACKET: return "LBRACKET";
        case TokenType::RBRACKET: return "RBRACKET";
        case TokenType::COMMA: return "COMMA";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::COLON: return "COLON";
        case TokenType::DOT: return "DOT";
        case TokenType::EQUAL: return "EQUAL";
        case TokenType::BANG: return "BANG";
        case TokenType::DOLLAR: return "DOLLAR";
        case TokenType::ARROW: return "ARROW";
        case TokenType::FAT_ARROW: return "FAT_ARROW";
        case TokenType::DOUBLE_EQUAL: return "DOUBLE_EQUAL";
        case TokenType::NOT_EQUAL: return "NOT_EQUAL";
        case TokenType::LESS: return "LESS";
        case TokenType::GREATER: return "GREATER";
        case TokenType::LESS_EQUAL: return "LESS_EQUAL";
        case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";
        case TokenType::LOGICAL_AND: return "LOGICAL_AND";
        case TokenType::LOGICAL_OR: return "LOGICAL_OR";
        case TokenType::AMPERSAND: return "AMPERSAND";
        case TokenType::FN: return "FN";
        case TokenType::RETURN: return "RETURN";
        case TokenType::IF: return "IF";
        case TokenType::ELSE: return "ELSE";
        case TokenType::WHILE: return "WHILE";
        case TokenType::STRUCT: return "STRUCT";
        case TokenType::CONST: return "CONST";
        case TokenType::IMPORT: return "IMPORT";
        case TokenType::FOREIGN: return "FOREIGN";
        case TokenType::COMPTIME: return "COMPTIME";
        case TokenType::MACRO_RULES: return "MACRO_RULES";
        case TokenType::AS: return "AS";
        case TokenType::END_OF_FILE: return "EOF";
        default: return "UNKNOWN";
    }
}

Token Lexer::makeToken(TokenType type, std::string value, size_t line, size_t column) const {
    Token token(type, std::move(value));
    token.location.file = sourceName_;
    token.location.line = line;
    token.location.column = column;
    return token;
}

void Lexer::skipWhitespaceAndComments() {
    while (pos_ < text_.size()) {
        char c = peek();
        if (std::isspace((unsigned char)c)) {
            advance();
            continue;
        }
        if (c == '/' && peek(1) == '/') {
            while (pos_ < text_.size() && peek() != '\n') {
                advance();
            }
            continue;
        }
        break;
    }
}

Token Lexer::getNextToken() {
    if (useExternal_) {
        if (externalPos_ < externalTokens_.size()) {
            return externalTokens_[externalPos_++];
        }
        return makeToken(TokenType::END_OF_FILE, "", line_, column_);
    }

    skipWhitespaceAndComments();
    if (pos_ >= text_.length()) {
        return makeToken(TokenType::END_OF_FILE, "", line_, column_);
    }

    size_t startLine = line_;
    size_t startCol = column_;
    char c = peek();

    if (c == '"') {
        std::string s;
        advance();
        while (pos_ < text_.length() && peek() != '"') {
            if (peek() == '\\' && peek(1) != '\0') {
                s += peek();
                advance();
            }
            s += peek();
            advance();
        }
        if (peek() == '"') advance();
        return makeToken(TokenType::STRING_LITERAL, s, startLine, startCol);
    }

    if (std::isdigit((unsigned char)c)) {
        std::string n;
        while (pos_ < text_.length() && std::isdigit((unsigned char)peek())) {
            n += peek();
            advance();
        }
        return makeToken(TokenType::NUMBER, n, startLine, startCol);
    }

    if (std::isalpha((unsigned char)c) || c == '_') {
        std::string id;
        while (pos_ < text_.length() && (std::isalnum((unsigned char)peek()) || peek() == '_')) {
            id += peek();
            advance();
        }
        auto tok = [&](TokenType type) { return makeToken(type, id, startLine, startCol); };
        if (id == "char") return tok(TokenType::CHAR_TYPE);
        if (id == "short") return tok(TokenType::SHORT_TYPE);
        if (id == "int") return tok(TokenType::INT_TYPE);
        if (id == "long") return tok(TokenType::LONG_TYPE);
        if (id == "i8") return tok(TokenType::I8_TYPE);
        if (id == "i16") return tok(TokenType::I16_TYPE);
        if (id == "i32") return tok(TokenType::I32_TYPE);
        if (id == "i64") return tok(TokenType::I64_TYPE);
        if (id == "u8") return tok(TokenType::U8_TYPE);
        if (id == "u16") return tok(TokenType::U16_TYPE);
        if (id == "u32") return tok(TokenType::U32_TYPE);
        if (id == "u64") return tok(TokenType::U64_TYPE);
        if (id == "void") return tok(TokenType::VOID_TYPE);
        if (id == "struct") return tok(TokenType::STRUCT);
        if (id == "import") return tok(TokenType::IMPORT);
        if (id == "as") return tok(TokenType::AS);
        if (id == "macro_rules") return tok(TokenType::MACRO_RULES);
        if (id == "comptime") return tok(TokenType::COMPTIME);
        if (id == "const") return tok(TokenType::CONST);
        if (id == "fn") return tok(TokenType::FN);
        if (id == "return") return tok(TokenType::RETURN);
        if (id == "if") return tok(TokenType::IF);
        if (id == "else") return tok(TokenType::ELSE);
        if (id == "while") return tok(TokenType::WHILE);
        if (id == "asm") {
            size_t savePos = pos_;
            size_t saveLine = line_;
            size_t saveCol = column_;
            skipWhitespaceAndComments();
            if (peek() == '{') {
                std::string s;
                advance(); // {
                int depth = 1;
                while (pos_ < text_.size() && depth > 0) {
                    if (peek() == '{') depth++;
                    else if (peek() == '}') {
                        depth--;
                        if (depth == 0) {
                            advance();
                            break;
                        }
                    }
                    if (depth > 0) {
                        s += peek();
                        advance();
                    }
                }
                return makeToken(TokenType::ASM_BLOCK, s, startLine, startCol);
            }
            pos_ = savePos;
            line_ = saveLine;
            column_ = saveCol;
            return makeToken(TokenType::UNKNOWN, id, startLine, startCol);
        }
        if (id == "foreign") return tok(TokenType::FOREIGN);
        return tok(TokenType::IDENTIFIER);
    }

    auto single = [&](TokenType type, const std::string& value) {
        advance();
        return makeToken(type, value, startLine, startCol);
    };

    switch (c) {
        case '<':
            if (peek(1) == '=') {
                advance();
                advance();
                return makeToken(TokenType::LESS_EQUAL, "<=", startLine, startCol);
            }
            return single(TokenType::LESS, "<");
        case '>':
            if (peek(1) == '=') {
                advance();
                advance();
                return makeToken(TokenType::GREATER_EQUAL, ">=", startLine, startCol);
            }
            return single(TokenType::GREATER, ">");
        case '+': return single(TokenType::PLUS, "+");
        case '-':
            if (peek(1) == '>') {
                advance();
                advance();
                return makeToken(TokenType::ARROW, "->", startLine, startCol);
            }
            return single(TokenType::MINUS, "-");
        case '*': return single(TokenType::MULTIPLY, "*");
        case '/': return single(TokenType::DIVIDE, "/");
        case '(': return single(TokenType::LPAREN, "(");
        case ')': return single(TokenType::RPAREN, ")");
        case '.': return single(TokenType::DOT, ".");
        case '!':
            if (peek(1) == '=') {
                advance();
                advance();
                return makeToken(TokenType::NOT_EQUAL, "!=", startLine, startCol);
            }
            return single(TokenType::BANG, "!");
        case '$': return single(TokenType::DOLLAR, "$");
        case '[': return single(TokenType::LBRACKET, "[");
        case ']': return single(TokenType::RBRACKET, "]");
        case '{': return single(TokenType::LBRACE, "{");
        case '}': return single(TokenType::RBRACE, "}");
        case ',': return single(TokenType::COMMA, ",");
        case '&':
            if (peek(1) == '&') {
                advance();
                advance();
                return makeToken(TokenType::LOGICAL_AND, "&&", startLine, startCol);
            }
            return single(TokenType::AMPERSAND, "&");
        case '|':
            if (peek(1) == '|') {
                advance();
                advance();
                return makeToken(TokenType::LOGICAL_OR, "||", startLine, startCol);
            }
            return single(TokenType::UNKNOWN, "|");
        case ':': return single(TokenType::COLON, ":");
        case '=':
            if (peek(1) == '=') {
                advance();
                advance();
                return makeToken(TokenType::DOUBLE_EQUAL, "==", startLine, startCol);
            }
            if (peek(1) == '>') {
                advance();
                advance();
                return makeToken(TokenType::FAT_ARROW, "=>", startLine, startCol);
            }
            return single(TokenType::EQUAL, "=");
        case ';': return single(TokenType::SEMICOLON, ";");
        default:
            advance();
            return makeToken(TokenType::UNKNOWN, std::string(1, c), startLine, startCol);
    }
}
