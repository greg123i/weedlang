#include "compiler/Parser.h"
#include "compiler/Lexer.h"
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

std::unique_ptr<ASTNode> Parser::parseExternal(const std::string& exePath, const std::vector<Token>& tokens) {
    std::string tempIn = "temp_tokens.txt";
    std::string tempOut = "temp_ast.txt";
    {
        std::ofstream out(tempIn, std::ios::binary);
        for (const auto& t : tokens) {
            out << t.location.line << ":" << t.location.column << " "
                << Lexer::tokenTypeToString(t.type) << " " << t.value << "\n";
        }
    }

    std::string cmd = exePath + " < " + tempIn + " > " + tempOut + " 2> nul";
    std::system(cmd.c_str());

    std::ifstream in(tempOut);
    std::vector<std::unique_ptr<ASTNode>> programStmts;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t indent = 0;
        while (indent < line.length() && line[indent] == ' ') indent++;
        if (indent > 0) continue;

        std::string content = line.substr(indent);
        if (content.starts_with("NUMBER: ")) {
            programStmts.push_back(std::make_unique<NumberNode>(content.substr(8)));
        } else if (content.starts_with("STRING: ")) {
            programStmts.push_back(std::make_unique<StringLiteralNode>(content.substr(8)));
        } else if (content.starts_with("VAR_REF: ")) {
            programStmts.push_back(std::make_unique<VarRefNode>(content.substr(9)));
        } else if (content.starts_with("FUNC: ")) {
             programStmts.push_back(std::make_unique<FuncDefNode>(content.substr(6), std::vector<FuncParam>{}, Type(DataType::VOID), std::make_unique<BlockNode>(std::vector<std::unique_ptr<ASTNode>>{})));
        }
    }

    in.close();
    std::filesystem::remove(tempIn);
    std::filesystem::remove(tempOut);
    return std::make_unique<ProgramNode>(std::move(programStmts));
}

static int sizeOfType(const Type& t) {
    return t.size();
}

static int alignOfType(const Type& t) {
    return t.alignment();
}

static int alignUp(int x, int a) {
    return (x + (a - 1)) & ~(a - 1);
}

Parser::Parser(const std::vector<Token>& tokens) : tokens_(tokens), pos_(0) {
    if (!tokens_.empty()) currentToken_ = tokens_[0];
}

const std::vector<std::string>& Parser::errors() const {
    return errors_;
}

void Parser::eat() {
    if (pos_ + 1 < tokens_.size()) {
        currentToken_ = tokens_[++pos_];
    } else if (!tokens_.empty()) {
        currentToken_ = tokens_.back();
        currentToken_.type = TokenType::END_OF_FILE;
    }
}

TokenType Parser::peekType(size_t ahead) const {
    size_t idx = pos_ + ahead;
    if (idx < tokens_.size()) return tokens_[idx].type;
    return TokenType::END_OF_FILE;
}

const Token& Parser::previousToken() const {
    if (pos_ == 0 || tokens_.empty()) return currentToken_;
    return tokens_[pos_ - 1];
}

std::runtime_error Parser::errorAt(const Token& token, const std::string& message) const {
    std::ostringstream out;
    out << (token.location.file ? *token.location.file : "<unknown>") << ":" << token.location.line << ":" << token.location.column
        << ": " << message;
    if (!token.value.empty()) {
        out << " near '" << token.value << "'";
    }
    return std::runtime_error(out.str());
}

void Parser::report(const Token& token, const std::string& message) {
    errors_.push_back(errorAt(token, message).what());
}

void Parser::synchronize() {
    if (currentToken_.type == TokenType::END_OF_FILE) return;
    eat();
    while (currentToken_.type != TokenType::SEMICOLON &&
           currentToken_.type != TokenType::RBRACE &&
           currentToken_.type != TokenType::FN &&
           currentToken_.type != TokenType::STRUCT &&
           currentToken_.type != TokenType::END_OF_FILE) {
        eat();
    }
    if (currentToken_.type == TokenType::SEMICOLON) eat();
}

Type Parser::parseType() {
    DataType base = DataType::STRUCT_TYPE;
    bool isUnsigned = false;
    std::string sName;

    if (currentToken_.type == TokenType::I8_TYPE) { base = DataType::I8; eat(); }
    else if (currentToken_.type == TokenType::I16_TYPE) { base = DataType::I16; eat(); }
    else if (currentToken_.type == TokenType::I32_TYPE) { base = DataType::I32; eat(); }
    else if (currentToken_.type == TokenType::I64_TYPE) { base = DataType::I64; eat(); }
    else if (currentToken_.type == TokenType::U8_TYPE) { base = DataType::U8; isUnsigned = true; eat(); }
    else if (currentToken_.type == TokenType::U16_TYPE) { base = DataType::U16; isUnsigned = true; eat(); }
    else if (currentToken_.type == TokenType::U32_TYPE) { base = DataType::U32; isUnsigned = true; eat(); }
    else if (currentToken_.type == TokenType::U64_TYPE) { base = DataType::U64; isUnsigned = true; eat(); }
    else if (currentToken_.type == TokenType::CHAR_TYPE) { base = DataType::I8; eat(); }
    else if (currentToken_.type == TokenType::SHORT_TYPE) { base = DataType::I16; eat(); }
    else if (currentToken_.type == TokenType::INT_TYPE) { base = DataType::I32; eat(); }
    else if (currentToken_.type == TokenType::LONG_TYPE) { base = DataType::I64; eat(); }
    else if (currentToken_.type == TokenType::VOID_TYPE) { base = DataType::VOID; eat(); }
    else if (currentToken_.type == TokenType::IDENTIFIER) { base = DataType::STRUCT_TYPE; sName = currentToken_.value; eat(); }
    else {
        throw errorAt(currentToken_, "expected a type");
    }

    int ptrLevel = 0;
    while (currentToken_.type == TokenType::MULTIPLY) { ptrLevel++; eat(); }

    int arraySize = 0;
    if (currentToken_.type == TokenType::LBRACKET) {
        eat(); // [
        if (currentToken_.type != TokenType::NUMBER) throw errorAt(currentToken_, "expected array size");
        arraySize = std::stoi(currentToken_.value);
        eat(); // number
        if (currentToken_.type != TokenType::RBRACKET) throw errorAt(currentToken_, "expected ']'");
        eat(); // ]
    }

    Type t(base, ptrLevel, sName, isUnsigned, arraySize);
    if (t.baseType == DataType::VOID && t.pointerLevel == 0) {
        throw errorAt(previousToken(), "void is only valid as a pointer type (void*)");
    }
    return t;
}

std::unique_ptr<ASTNode> Parser::parse() {
    Token start = currentToken_;
    std::vector<std::unique_ptr<ASTNode>> statements;
    while (currentToken_.type != TokenType::END_OF_FILE) {
        try {
            statements.push_back(parseStatement());
            if (currentToken_.type == TokenType::SEMICOLON) eat();
        } catch (const std::exception& ex) {
            errors_.push_back(ex.what());
            synchronize();
        }
    }
    return makeNode<ProgramNode>(start, std::move(statements));
}

std::unique_ptr<BlockNode> Parser::parseBlock() {
    Token start = currentToken_;
    if (currentToken_.type != TokenType::LBRACE) {
        throw errorAt(currentToken_, "expected '{'");
    }
    eat(); // {
    std::vector<std::unique_ptr<ASTNode>> stmts;
    while (currentToken_.type != TokenType::RBRACE && currentToken_.type != TokenType::END_OF_FILE) {
        try {
            stmts.push_back(parseStatement());
            if (currentToken_.type == TokenType::SEMICOLON) eat();
        } catch (const std::exception& ex) {
            errors_.push_back(ex.what());
            synchronize();
            if (currentToken_.type == TokenType::RBRACE || currentToken_.type == TokenType::END_OF_FILE) break;
        }
    }
    if (currentToken_.type == TokenType::END_OF_FILE) {
        throw errorAt(currentToken_, "unexpected end of file (unclosed '{')");
    }
    eat(); // }
    return makeNode<BlockNode>(start, std::move(stmts));
}

std::unique_ptr<ASTNode> Parser::parseStatement() {
    Token start = currentToken_;
    if (currentToken_.type == TokenType::FN) {
        eat(); // fn
        std::string name = currentToken_.value;
        eat(); // ident

        if (currentToken_.type != TokenType::LPAREN) throw errorAt(currentToken_, "expected '(' after function name");
        eat(); // (
        std::vector<FuncParam> params;
        if (currentToken_.type != TokenType::RPAREN) {
            while (true) {
                Type pt = parseType();
                std::string pn = currentToken_.value;
                eat(); // ident
                params.push_back({pn, pt});
                if (currentToken_.type == TokenType::COMMA) { eat(); continue; }
                break;
            }
        }
        if (currentToken_.type != TokenType::RPAREN) throw errorAt(currentToken_, "expected ')' after parameter list");
        eat(); // )

        Type ret = Type(DataType::VOID, 0, "");
        if (currentToken_.type == TokenType::ARROW) {
            eat();
            ret = parseType();
        }
        auto body = parseBlock();
        return makeNode<FuncDefNode>(start, name, std::move(params), ret, std::move(body));
    }

    if (currentToken_.type == TokenType::RETURN) {
        eat();
        std::unique_ptr<ASTNode> val = nullptr;
        if (currentToken_.type != TokenType::SEMICOLON && currentToken_.type != TokenType::RBRACE) {
            val = parseExpression();
        }
        return makeNode<ReturnNode>(start, std::move(val));
    }

    if (currentToken_.type == TokenType::IF) {
        eat();
        auto cond = parseExpression();
        auto thenB = parseBlock();
        std::unique_ptr<ASTNode> elseB = nullptr;
        if (currentToken_.type == TokenType::ELSE) {
            eat();
            elseB = parseBlock();
        }
        return makeNode<IfNode>(start, std::move(cond), std::move(thenB), std::move(elseB));
    }

    if (currentToken_.type == TokenType::WHILE) {
        eat();
        auto cond = parseExpression();
        auto body = parseBlock();
        return makeNode<WhileNode>(start, std::move(cond), std::move(body));
    }

    if (currentToken_.type == TokenType::CONST) {
        eat();
        Type t;
            if (currentToken_.type == TokenType::IDENTIFIER && peekType() == TokenType::EQUAL) {
             // Type inference placeholder: const x = ...
             t = Type(DataType::I32); // default to i32 for now if no type
        } else {
             t = parseType();
        }
        std::string name = currentToken_.value;
        eat(); // name
        eat(); // =
        auto init = parseExpression();
        eat(); // ;
        return makeNode<ConstDeclNode>(start, name, t, std::move(init));
    }

    if (currentToken_.type == TokenType::COMPTIME) {
        eat();
        return makeNode<ComptimeNode>(start, parseStatement());
    }

    if (currentToken_.type == TokenType::LBRACE) {
        return parseBlock();
    }

    if (currentToken_.type == TokenType::STRUCT) {
        eat();
        std::string name = currentToken_.value;
        eat(); // name
        eat(); // {
        std::vector<StructMember> members;
        int currentOffset = 0;
        while (currentToken_.type != TokenType::RBRACE) {
            Type t = parseType();
            std::string mName = currentToken_.value;
            eat(); // name
            eat(); // ;

            if (t.baseType == DataType::STRUCT_TYPE && t.pointerLevel == 0) {
                throw errorAt(previousToken(), "struct members by value are not supported yet; use a pointer field");
            }

            int a = alignOfType(t);
            currentOffset = alignUp(currentOffset, a);
            members.push_back({mName, t, currentOffset});
            currentOffset += sizeOfType(t);
        }
        eat(); eat(); // }, ;
        return makeNode<StructDefNode>(start, name, std::move(members));
    }
    if (currentToken_.type == TokenType::CHAR_TYPE ||
        currentToken_.type == TokenType::SHORT_TYPE ||
        currentToken_.type == TokenType::INT_TYPE ||
        currentToken_.type == TokenType::LONG_TYPE ||
        currentToken_.type == TokenType::I8_TYPE ||
        currentToken_.type == TokenType::I16_TYPE ||
        currentToken_.type == TokenType::I32_TYPE ||
        currentToken_.type == TokenType::I64_TYPE ||
        currentToken_.type == TokenType::U8_TYPE ||
        currentToken_.type == TokenType::U16_TYPE ||
        currentToken_.type == TokenType::U32_TYPE ||
        currentToken_.type == TokenType::U64_TYPE ||
        currentToken_.type == TokenType::VOID_TYPE ||
        (currentToken_.type == TokenType::IDENTIFIER &&
         (peekType() == TokenType::IDENTIFIER || peekType() == TokenType::MULTIPLY))) {
        Type t = parseType();
        std::string name = currentToken_.value;
        eat();
        std::unique_ptr<ASTNode> init = nullptr;
        if (currentToken_.type == TokenType::EQUAL) {
            eat();
            init = parseExpression();
        }
        eat(); // ;
        if (t.baseType == DataType::STRUCT_TYPE && t.pointerLevel == 0) {
            // By-value structs are fine (we allocate storage and allow member access).
            // But struct members by value are not supported yet.
        }
        return makeNode<VarDeclNode>(start, name, t, std::move(init));
    }
    if (currentToken_.type == TokenType::IMPORT) {
        eat(); std::string path = currentToken_.value;
        eat();
        return makeNode<ImportNode>(start, path);
    }
    if (currentToken_.type == TokenType::FOREIGN) {
        eat(); std::string lib = currentToken_.value;
        eat(); std::string func = currentToken_.value;
        eat();
        return makeNode<ForeignNode>(start, lib, func);
    }
    return parseExpression();
}

std::unique_ptr<ASTNode> Parser::parseFactor() {
    Token start = currentToken_;
    std::unique_ptr<ASTNode> node;

    if (start.type == TokenType::NUMBER) {
        eat();
        node = makeNode<NumberNode>(start, start.value);
    } else if (start.type == TokenType::COMPTIME) {
        eat();
        node = makeNode<ComptimeNode>(start, parseFactor());
    } else if (start.type == TokenType::STRING_LITERAL) {
        eat();
        node = makeNode<StringLiteralNode>(start, start.value);
    } else if (start.type == TokenType::MINUS && peekType() == TokenType::NUMBER) {
        eat(); // -
        std::string val = "-" + currentToken_.value;
        eat(); // number
        node = makeNode<NumberNode>(start, val);
    } else if (start.type == TokenType::IDENTIFIER) {
        std::string name = start.value;
        eat();
        if (currentToken_.type == TokenType::LPAREN) {
            eat();
            std::vector<std::unique_ptr<ASTNode>> args;
            if (currentToken_.type != TokenType::RPAREN) {
                args.push_back(parseExpression());
                while (currentToken_.type == TokenType::COMMA) {
                    eat();
                    args.push_back(parseExpression());
                }
            }
            if (currentToken_.type != TokenType::RPAREN) throw errorAt(currentToken_, "expected ')' to close call");
            eat(); // )
            node = makeNode<CallNode>(start, name, std::move(args));
        } else {
            node = makeNode<VarRefNode>(start, name);
        }
    } else if (start.type == TokenType::LPAREN) {
        eat();
        node = parseExpression();
        if (currentToken_.type != TokenType::RPAREN) throw errorAt(currentToken_, "expected ')'");
        eat(); // )
    } else if (start.type == TokenType::ASM_BLOCK) {
        eat();
        node = makeNode<AsmNode>(start, start.value);
    } else if (start.type == TokenType::AMPERSAND) {
        eat();
        node = makeNode<AddressOfNode>(start, parseFactor());
    } else if (start.type == TokenType::MULTIPLY) {
        eat();
        node = makeNode<DereferenceNode>(start, parseFactor());
    } else {
        throw errorAt(start, "unexpected expression");
    }

    while (true) {
        Token opToken = currentToken_;
        if (currentToken_.type == TokenType::DOT) {
            eat();
            std::string mName = currentToken_.value;
            eat();
            node = makeNode<MemberAccessNode>(opToken, std::move(node), mName);
        } else if (currentToken_.type == TokenType::LBRACKET) {
            eat();
            auto index = parseExpression();
            if (currentToken_.type != TokenType::RBRACKET) throw errorAt(currentToken_, "expected ']'");
            eat();
            node = makeNode<IndexNode>(opToken, std::move(node), std::move(index));
        } else if (currentToken_.type == TokenType::AS) {
            eat();
            Type t = parseType();
            node = makeNode<CastNode>(opToken, std::move(node), t);
        } else {
            break;
        }
    }

    return node;
}

std::unique_ptr<ASTNode> Parser::parseTerm() {
    auto node = parseFactor();
    while (currentToken_.type == TokenType::MULTIPLY || currentToken_.type == TokenType::DIVIDE) {
        Token opToken = currentToken_;
        TokenType op = currentToken_.type; eat();
        node = makeNode<BinaryOpNode>(opToken, std::move(node), op, parseFactor());
    }
    return node;
}

std::unique_ptr<ASTNode> Parser::parseAdditive() {
    auto node = parseTerm();
    while (currentToken_.type == TokenType::PLUS || currentToken_.type == TokenType::MINUS) {
        Token opToken = currentToken_;
        TokenType op = currentToken_.type; eat();
        node = makeNode<BinaryOpNode>(opToken, std::move(node), op, parseTerm());
    }
    return node;
}

std::unique_ptr<ASTNode> Parser::parseRelational() {
    auto node = parseAdditive();
    while (currentToken_.type == TokenType::LESS || currentToken_.type == TokenType::GREATER ||
           currentToken_.type == TokenType::LESS_EQUAL || currentToken_.type == TokenType::GREATER_EQUAL) {
        Token opToken = currentToken_;
        TokenType op = currentToken_.type; eat();
        node = makeNode<BinaryOpNode>(opToken, std::move(node), op, parseAdditive());
    }
    return node;
}

std::unique_ptr<ASTNode> Parser::parseEquality() {
    auto node = parseRelational();
    while (currentToken_.type == TokenType::DOUBLE_EQUAL || currentToken_.type == TokenType::NOT_EQUAL) {
        Token opToken = currentToken_;
        TokenType op = currentToken_.type; eat();
        node = makeNode<BinaryOpNode>(opToken, std::move(node), op, parseRelational());
    }
    return node;
}

std::unique_ptr<ASTNode> Parser::parseLogicalAnd() {
    auto node = parseEquality();
    while (currentToken_.type == TokenType::LOGICAL_AND) {
        Token opToken = currentToken_;
        TokenType op = currentToken_.type; eat();
        node = makeNode<BinaryOpNode>(opToken, std::move(node), op, parseEquality());
    }
    return node;
}

std::unique_ptr<ASTNode> Parser::parseLogicalOr() {
    auto node = parseLogicalAnd();
    while (currentToken_.type == TokenType::LOGICAL_OR) {
        Token opToken = currentToken_;
        TokenType op = currentToken_.type; eat();
        node = makeNode<BinaryOpNode>(opToken, std::move(node), op, parseLogicalAnd());
    }
    return node;
}

std::unique_ptr<ASTNode> Parser::parseExpression() {
    return parseAssignment();
}

std::unique_ptr<ASTNode> Parser::parseAssignment() {
    auto node = parseLogicalOr();
    if (currentToken_.type == TokenType::EQUAL) {
        Token opToken = currentToken_;
        eat();
        auto rhs = parseAssignment();
        return makeNode<AssignNode>(opToken, std::move(node), std::move(rhs));
    }

    return node;
}
