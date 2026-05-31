#ifndef COMPILER_AST_H
#define COMPILER_AST_H

#include <string>
#include <vector>
#include <memory>
#include <functional>

enum class DataType { VOID, I8, I16, I32, I64, U8, U16, U32, U64, STRUCT_TYPE, PTR };

struct Token;

struct SourceLocation {
    const std::string* file = nullptr;
    size_t line = 1;
    size_t column = 1;
};

struct Type {
    DataType baseType;
    int pointerLevel;
    bool isUnsigned;
    int arraySize; // 0 = not an array
    std::string structName;
    Type(DataType b = DataType::VOID, int p = 0, std::string s = "", bool u = false, int a = 0)
        : baseType(b), pointerLevel(p), isUnsigned(u), arraySize(a), structName(std::move(s)) {}

    // Returns the size in bytes. resolver is used to look up struct sizes.
    int size(const std::function<int(const std::string&)>& structSizeResolver = nullptr) const;
    // Returns the alignment requirement in bytes.
    int alignment(const std::function<int(const std::string&)>& structAlignResolver = nullptr) const;
};

enum class TokenType {
    NUMBER, PLUS, MINUS, MULTIPLY, DIVIDE, LPAREN, RPAREN,
    IDENTIFIER, EQUAL, SEMICOLON,
    CHAR_TYPE, SHORT_TYPE, INT_TYPE, LONG_TYPE, VOID_TYPE,
    I8_TYPE, I16_TYPE, I32_TYPE, I64_TYPE,
    U8_TYPE, U16_TYPE, U32_TYPE, U64_TYPE,
    AS,
    LESS, GREATER, LESS_EQUAL, GREATER_EQUAL, DOUBLE_EQUAL, NOT_EQUAL,
    LOGICAL_AND, LOGICAL_OR,
    FN, RETURN, IF, ELSE, WHILE,
    MACRO_RULES,
    COMPTIME,
    CONST,
    IMPORT, ASM_BLOCK, FOREIGN, STRING_LITERAL, AMPERSAND, COMMA,
    STRUCT, DOT, LBRACE, RBRACE, LBRACKET, RBRACKET, BANG, DOLLAR, FAT_ARROW, ARROW,
    COLON,
    END_OF_FILE, UNKNOWN
};

struct Token {
    TokenType type;
    std::string value;
    SourceLocation location;
    Token(TokenType type, std::string value = "") : type(type), value(std::move(value)) {}
    Token(TokenType type, std::string value, SourceLocation location)
        : type(type), value(std::move(value)), location(std::move(location)) {}
    Token() : type(TokenType::UNKNOWN), value("") {}
};

enum class NodeKind {
    Number, BinaryOp, Assign, VarDecl, VarRef, Import, Asm, Foreign,
    AddressOf, Dereference, Cast, Call, Index, StringLiteral, Program,
    StructDef, MemberAccess, Block, If, While, Return, FuncDef, Comptime, ConstDecl
};

class ASTVisitor;
class ASTNode {
public:
    explicit ASTNode(NodeKind kind) : kind_(kind) {}
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor* visitor) = 0;

    bool isExpression() const {
        switch (kind()) {
            case NodeKind::Number:
            case NodeKind::BinaryOp:
            case NodeKind::Assign:
            case NodeKind::VarRef:
            case NodeKind::AddressOf:
            case NodeKind::Dereference:
            case NodeKind::Cast:
            case NodeKind::Call:
            case NodeKind::Index:
            case NodeKind::StringLiteral:
                return true;
            default:
                return false;
        }
    }
    void setLocation(SourceLocation location) { location_ = std::move(location); }
    const SourceLocation& location() const { return location_; }
    NodeKind kind() const { return kind_; }
private:
    SourceLocation location_;
    NodeKind kind_;
};

class NumberNode;
class BinaryOpNode;
class AssignNode;
class VarDeclNode;
class VarRefNode;
class ImportNode;
class AsmNode;
class ForeignNode;
class AddressOfNode;
class DereferenceNode;
class CastNode;
class CallNode;
class IndexNode;
class StringLiteralNode;
class ProgramNode;
class StructDefNode;
class MemberAccessNode;
class BlockNode;
class IfNode;
class WhileNode;
class ReturnNode;
class FuncDefNode;
class ComptimeNode;
class ConstDeclNode;

class ASTVisitor {
public:
    virtual void visit(NumberNode* node) = 0;
    virtual void visit(BinaryOpNode* node) = 0;
    virtual void visit(AssignNode* node) = 0;
    virtual void visit(VarDeclNode* node) = 0;
    virtual void visit(VarRefNode* node) = 0;
    virtual void visit(ImportNode* node) = 0;
    virtual void visit(AsmNode* node) = 0;
    virtual void visit(ForeignNode* node) = 0;
    virtual void visit(AddressOfNode* node) = 0;
    virtual void visit(DereferenceNode* node) = 0;
    virtual void visit(CastNode* node) = 0;
    virtual void visit(CallNode* node) = 0;
    virtual void visit(IndexNode* node) = 0;
    virtual void visit(StringLiteralNode* node) = 0;
    virtual void visit(ProgramNode* node) = 0;
    virtual void visit(StructDefNode* node) = 0;
    virtual void visit(MemberAccessNode* node) = 0;
    virtual void visit(BlockNode* node) = 0;
    virtual void visit(IfNode* node) = 0;
    virtual void visit(WhileNode* node) = 0;
    virtual void visit(ReturnNode* node) = 0;
    virtual void visit(FuncDefNode* node) = 0;
    virtual void visit(ComptimeNode* node) = 0;
    virtual void visit(ConstDeclNode* node) = 0;
    virtual ~ASTVisitor() = default;
};

class NumberNode : public ASTNode {
public:
    explicit NumberNode(std::string value) : ASTNode(NodeKind::Number), value_(std::move(value)) {}
    std::string value() const { return value_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::string value_;
};

class BinaryOpNode : public ASTNode {
public:
    BinaryOpNode(std::unique_ptr<ASTNode> left, TokenType op, std::unique_ptr<ASTNode> right)
        : ASTNode(NodeKind::BinaryOp), left_(std::move(left)), op_(op), right_(std::move(right)) {}
    ASTNode* left() const { return left_.get(); }
    ASTNode* right() const { return right_.get(); }
    TokenType op() const { return op_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::unique_ptr<ASTNode> left_, right_;
    TokenType op_;
};

class AssignNode : public ASTNode {
public:
    AssignNode(std::unique_ptr<ASTNode> target, std::unique_ptr<ASTNode> value)
        : ASTNode(NodeKind::Assign), target_(std::move(target)), value_(std::move(value)) {}
    ASTNode* target() const { return target_.get(); }
    ASTNode* value() const { return value_.get(); }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::unique_ptr<ASTNode> target_;
    std::unique_ptr<ASTNode> value_;
};

class VarDeclNode : public ASTNode {
public:
    VarDeclNode(std::string name, Type type, std::unique_ptr<ASTNode> init)
        : ASTNode(NodeKind::VarDecl), name_(std::move(name)), type_(std::move(type)), init_(std::move(init)) {}
    std::string name() const { return name_; }
    const Type& type() const { return type_; }
    ASTNode* init() const { return init_.get(); }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::string name_;
    Type type_;
    std::unique_ptr<ASTNode> init_;
};

class VarRefNode : public ASTNode {
public:
    explicit VarRefNode(std::string name) : ASTNode(NodeKind::VarRef), name_(std::move(name)) {}
    std::string name() const { return name_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::string name_;
};

class ImportNode : public ASTNode {
public:
    explicit ImportNode(std::string path) : ASTNode(NodeKind::Import), path_(std::move(path)) {}
    std::string path() const { return path_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::string path_;
};

class AsmNode : public ASTNode {
public:
    explicit AsmNode(std::string code) : ASTNode(NodeKind::Asm), code_(std::move(code)) {}
    std::string code() const { return code_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::string code_;
};

class ForeignNode : public ASTNode {
public:
    ForeignNode(std::string lib, std::string func) : ASTNode(NodeKind::Foreign), lib_(std::move(lib)), func_(std::move(func)) {}
    std::string lib() const { return lib_; }
    std::string func() const { return func_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::string lib_, func_;
};

class AddressOfNode : public ASTNode {
public:
    explicit AddressOfNode(std::unique_ptr<ASTNode> expr) : ASTNode(NodeKind::AddressOf), expr_(std::move(expr)) {}
    ASTNode* expr() const { return expr_.get(); }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::unique_ptr<ASTNode> expr_;
};

class DereferenceNode : public ASTNode {
public:
    explicit DereferenceNode(std::unique_ptr<ASTNode> expr) : ASTNode(NodeKind::Dereference), expr_(std::move(expr)) {}
    ASTNode* expr() const { return expr_.get(); }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::unique_ptr<ASTNode> expr_;
};

class CastNode : public ASTNode {
public:
    CastNode(std::unique_ptr<ASTNode> expr, Type targetType)
        : ASTNode(NodeKind::Cast), expr_(std::move(expr)), targetType_(std::move(targetType)) {}
    ASTNode* expr() const { return expr_.get(); }
    const Type& targetType() const { return targetType_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::unique_ptr<ASTNode> expr_;
    Type targetType_;
};

class CallNode : public ASTNode {
public:
    CallNode(std::string name, std::vector<std::unique_ptr<ASTNode>> args)
        : ASTNode(NodeKind::Call), name_(std::move(name)), args_(std::move(args)) {}
    std::string name() const { return name_; }
    const std::vector<std::unique_ptr<ASTNode>>& args() const { return args_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::string name_;
    std::vector<std::unique_ptr<ASTNode>> args_;
};

class IndexNode : public ASTNode {
public:
    IndexNode(std::unique_ptr<ASTNode> expr, std::unique_ptr<ASTNode> index)
        : ASTNode(NodeKind::Index), expr_(std::move(expr)), index_(std::move(index)) {}
    ASTNode* expr() const { return expr_.get(); }
    ASTNode* index() const { return index_.get(); }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::unique_ptr<ASTNode> expr_;
    std::unique_ptr<ASTNode> index_;
};

class StringLiteralNode : public ASTNode {
public:
    explicit StringLiteralNode(std::string value) : ASTNode(NodeKind::StringLiteral), value_(std::move(value)) {}
    std::string value() const { return value_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::string value_;
};

class ProgramNode : public ASTNode {
public:
    explicit ProgramNode(std::vector<std::unique_ptr<ASTNode>> statements)
        : ASTNode(NodeKind::Program), statements_(std::move(statements)) {}
    const std::vector<std::unique_ptr<ASTNode>>& statements() const { return statements_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::vector<std::unique_ptr<ASTNode>> statements_;
};

class BlockNode : public ASTNode {
public:
    explicit BlockNode(std::vector<std::unique_ptr<ASTNode>> statements)
        : ASTNode(NodeKind::Block), statements_(std::move(statements)) {}
    const std::vector<std::unique_ptr<ASTNode>>& statements() const { return statements_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::vector<std::unique_ptr<ASTNode>> statements_;
};

class IfNode : public ASTNode {
public:
    IfNode(std::unique_ptr<ASTNode> cond, std::unique_ptr<ASTNode> thenBranch, std::unique_ptr<ASTNode> elseBranch)
        : ASTNode(NodeKind::If), cond_(std::move(cond)), then_(std::move(thenBranch)), else_(std::move(elseBranch)) {}
    ASTNode* cond() const { return cond_.get(); }
    ASTNode* thenBranch() const { return then_.get(); }
    ASTNode* elseBranch() const { return else_.get(); }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::unique_ptr<ASTNode> cond_;
    std::unique_ptr<ASTNode> then_;
    std::unique_ptr<ASTNode> else_;
};

class WhileNode : public ASTNode {
public:
    WhileNode(std::unique_ptr<ASTNode> cond, std::unique_ptr<ASTNode> body)
        : ASTNode(NodeKind::While), cond_(std::move(cond)), body_(std::move(body)) {}
    ASTNode* cond() const { return cond_.get(); }
    ASTNode* body() const { return body_.get(); }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::unique_ptr<ASTNode> cond_;
    std::unique_ptr<ASTNode> body_;
};

class ReturnNode : public ASTNode {
public:
    explicit ReturnNode(std::unique_ptr<ASTNode> value) : ASTNode(NodeKind::Return), value_(std::move(value)) {}
    ASTNode* value() const { return value_.get(); }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::unique_ptr<ASTNode> value_;
};

struct FuncParam {
    std::string name;
    Type type;
};

class FuncDefNode : public ASTNode {
public:
    FuncDefNode(std::string name, std::vector<FuncParam> params, Type retType, std::unique_ptr<BlockNode> body)
        : ASTNode(NodeKind::FuncDef), name_(std::move(name)), params_(std::move(params)), retType_(std::move(retType)), body_(std::move(body)) {}
    const std::string& name() const { return name_; }
    const std::vector<FuncParam>& params() const { return params_; }
    const Type& retType() const { return retType_; }
    BlockNode* body() const { return body_.get(); }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::string name_;
    std::vector<FuncParam> params_;
    Type retType_;
    std::unique_ptr<BlockNode> body_;
};

class MemberAccessNode : public ASTNode {
public:
    MemberAccessNode(std::unique_ptr<ASTNode> expr, std::string member)
        : ASTNode(NodeKind::MemberAccess), expr_(std::move(expr)), member_(std::move(member)) {}
    ASTNode* expr() const { return expr_.get(); }
    std::string member() const { return member_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::unique_ptr<ASTNode> expr_;
    std::string member_;
};

struct StructMember {
    std::string name;
    Type type;
    int offset;
};

class StructDefNode : public ASTNode {
public:
    StructDefNode(std::string name, std::vector<StructMember> members)
        : ASTNode(NodeKind::StructDef), name_(std::move(name)), members_(std::move(members)) {}
    std::string name() const { return name_; }
    const std::vector<StructMember>& members() const { return members_; }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::string name_;
    std::vector<StructMember> members_;
};

class ComptimeNode : public ASTNode {
public:
    explicit ComptimeNode(std::unique_ptr<ASTNode> expr) : ASTNode(NodeKind::Comptime), expr_(std::move(expr)) {}
    ASTNode* expr() const { return expr_.get(); }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::unique_ptr<ASTNode> expr_;
};

class ConstDeclNode : public ASTNode {
public:
    ConstDeclNode(std::string name, Type type, std::unique_ptr<ASTNode> init)
        : ASTNode(NodeKind::ConstDecl), name_(std::move(name)), type_(std::move(type)), init_(std::move(init)) {}
    std::string name() const { return name_; }
    const Type& type() const { return type_; }
    ASTNode* init() const { return init_.get(); }
    void accept(ASTVisitor* visitor) override { visitor->visit(this); }
private:
    std::string name_;
    Type type_;
    std::unique_ptr<ASTNode> init_;
};

#endif // COMPILER_AST_H
