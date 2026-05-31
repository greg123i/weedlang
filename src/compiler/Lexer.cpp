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

    if (std::filesystem::exists("tokenizer.exe")) {
        useExternal_ = true;
        runExternalTokenizer();
    } else {
        auto exePath = std::filesystem::path(sourceName).parent_path() / "tokenizer.exe";
        if (std::filesystem::exists(exePath)) {
            useExternal_ = true;
            runExternalTokenizer();
        }
    }
}

void Lexer::runExternalTokenizer() {
    // Write text to a temporary input file
    std::string tempInPath = "temp_in.weed";
    std::string tempOutPath = "temp_out.txt";
    {
        std::ofstream out(tempInPath, std::ios::binary);
        out << text_;
    }

    std::string cmd = "tokenizer.exe " + tempInPath + " > " + tempOutPath + " 2> nul";
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

        TokenType type = TokenType::UNKNOWN;
        if (typeStr == "KEYWORD") {
            if (val == "fn") type = TokenType::FN;
            else if (val == "return") type = TokenType::RETURN;
            else if (val == "if") type = TokenType::IF;
            else if (val == "else") type = TokenType::ELSE;
            else if (val == "while") type = TokenType::WHILE;
            else if (val == "struct") type = TokenType::STRUCT;
            else if (val == "const") type = TokenType::CONST;
            else if (val == "import") type = TokenType::IMPORT;
            else if (val == "foreign") type = TokenType::FOREIGN;
            else if (val == "comptime") type = TokenType::COMPTIME;
            else if (val == "macro_rules") type = TokenType::MACRO_RULES;
            else if (val == "char") type = TokenType::CHAR_TYPE;
            else if (val == "short") type = TokenType::SHORT_TYPE;
            else if (val == "int") type = TokenType::INT_TYPE;
            else if (val == "long") type = TokenType::LONG_TYPE;
            else if (val == "i8") type = TokenType::I8_TYPE;
            else if (val == "i16") type = TokenType::I16_TYPE;
            else if (val == "i32") type = TokenType::I32_TYPE;
            else if (val == "i64") type = TokenType::I64_TYPE;
            else if (val == "u8") type = TokenType::U8_TYPE;
            else if (val == "u16") type = TokenType::U16_TYPE;
            else if (val == "u32") type = TokenType::U32_TYPE;
            else if (val == "u64") type = TokenType::U64_TYPE;
            else if (val == "void") type = TokenType::VOID_TYPE;
            else if (val == "as") type = TokenType::AS;
            else if (val == "asm") type = TokenType::UNKNOWN; // wait, asm is handled below
        } else if (typeStr == "IDENT") {
            type = TokenType::IDENTIFIER;
        } else if (typeStr == "NUMBER") {
            type = TokenType::NUMBER;
        } else if (typeStr == "STRING") {
            type = TokenType::STRING_LITERAL;
        } else if (typeStr == "PUNC") {
            if (val == "+") type = TokenType::PLUS;
            else if (val == "-") type = TokenType::MINUS;
            else if (val == "*") type = TokenType::MULTIPLY;
            else if (val == "/") type = TokenType::DIVIDE;
            else if (val == "(") type = TokenType::LPAREN;
            else if (val == ")") type = TokenType::RPAREN;
            else if (val == "{") type = TokenType::LBRACE;
            else if (val == "}") type = TokenType::RBRACE;
            else if (val == "[") type = TokenType::LBRACKET;
            else if (val == "]") type = TokenType::RBRACKET;
            else if (val == ",") type = TokenType::COMMA;
            else if (val == ";") type = TokenType::SEMICOLON;
            else if (val == ":") type = TokenType::COLON;
            else if (val == ".") type = TokenType::DOT;
            else if (val == "=") type = TokenType::EQUAL;
            else if (val == "!") type = TokenType::BANG;
            else if (val == "$") type = TokenType::DOLLAR;
            else if (val == "->") type = TokenType::ARROW;
            else if (val == "=>") type = TokenType::FAT_ARROW;
            else if (val == "==") type = TokenType::DOUBLE_EQUAL;
            else if (val == "!=") type = TokenType::NOT_EQUAL;
            else if (val == "<") type = TokenType::LESS;
            else if (val == ">") type = TokenType::GREATER;
            else if (val == "<=") type = TokenType::LESS_EQUAL;
            else if (val == ">=") type = TokenType::GREATER_EQUAL;
            else if (val == "&&") type = TokenType::LOGICAL_AND;
            else if (val == "||") type = TokenType::LOGICAL_OR;
            else if (val == "&") type = TokenType::AMPERSAND;
        } else if (typeStr == "EOF") {
            type = TokenType::END_OF_FILE;
        }

        externalTokens_.push_back(makeToken(type, val, l, c));
    }

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

Token Lexer::makeToken(TokenType type, std::string value, size_t line, size_t column) const {
    Token token(type, std::move(value));
    token.location.file = sourceName_.get();
    token.location.line = line;
    token.location.column = column;
    return token;
}

void Lexer::skipWhitespaceAndComments() {
    while (pos_ < text_.size()) {
        char c = peek();
        if (std::isspace((unsigned char)c) || c == '\0') {
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
