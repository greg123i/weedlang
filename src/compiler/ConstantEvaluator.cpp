#include "compiler/ConstantEvaluator.h"
#include <optional>
#include <sstream>
#include <unordered_map>

namespace {

struct MetaValue {
    enum class Kind { Int, String };

    Kind kind = Kind::Int;
    int64_t intValue = 0;
    std::string stringValue;
    bool isUnsigned = false;

    static MetaValue fromInt(int64_t v, bool uns = false) {
        MetaValue out;
        out.kind = Kind::Int;
        out.intValue = v;
        out.isUnsigned = uns;
        return out;
    }

    static MetaValue fromString(std::string v) {
        MetaValue out;
        out.kind = Kind::String;
        out.stringValue = std::move(v);
        return out;
    }
};

using MetaEnv = std::unordered_map<std::string, MetaValue>;

static void pushDiag(std::vector<std::string>* diagnostics, const std::string& msg) {
    if (diagnostics) diagnostics->push_back(msg);
}

static std::string locPrefix(const ASTNode* node) {
    const auto& loc = node->location();
    if (!loc.file || loc.file->empty()) return "";
    return *loc.file + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column) + ": ";
}

static std::string locPrefix(const Token& token) {
    if (!token.location.file || token.location.file->empty()) return "";
    return *token.location.file + ":" + std::to_string(token.location.line) + ":" + std::to_string(token.location.column) + ": ";
}

static void setLoc(ASTNode* node, const ASTNode* src) {
    if (node && src) node->setLocation(src->location());
}

static void setLoc(ASTNode* node, const Token& tok) {
    if (node) node->setLocation(tok.location);
}

static std::optional<int64_t> parseInt64(const std::string& s) {
    try {
        size_t idx = 0;
        long long v = std::stoll(s, &idx, 10);
        if (idx != s.size()) return std::nullopt;
        return static_cast<int64_t>(v);
    } catch (...) {
        return std::nullopt;
    }
}

static std::unique_ptr<ASTNode> makeNumber(int64_t v, const ASTNode* src) {
    auto n = std::make_unique<NumberNode>(std::to_string(v));
    setLoc(n.get(), src);
    return n;
}

static std::unique_ptr<ASTNode> makeString(const std::string& v, const ASTNode* src) {
    auto n = std::make_unique<StringLiteralNode>(v);
    setLoc(n.get(), src);
    return n;
}

static std::optional<MetaValue> evalExpr(const ASTNode* node,
                                         const MetaEnv& env,
                                         std::vector<std::string>* diagnostics);

static std::unique_ptr<ASTNode> lowerExpr(const ASTNode* node,
                                          MetaEnv& env,
                                          std::vector<std::string>* diagnostics);

static std::unique_ptr<ASTNode> lowerStatement(const ASTNode* node,
                                               MetaEnv& env,
                                               std::vector<std::string>* diagnostics,
                                               bool inComptime);

static std::vector<std::unique_ptr<ASTNode>> lowerStatements(const std::vector<std::unique_ptr<ASTNode>>& statements,
                                                             MetaEnv& env,
                                                             std::vector<std::string>* diagnostics,
                                                             bool inComptime) {
    std::vector<std::unique_ptr<ASTNode>> out;
    for (const auto& stmt : statements) {
        auto lowered = lowerStatement(stmt.get(), env, diagnostics, inComptime);
        if (lowered) out.push_back(std::move(lowered));
    }
    return out;
}

static bool isTruthy(const MetaValue& v) {
    if (v.kind == MetaValue::Kind::String) return !v.stringValue.empty();
    return v.intValue != 0;
}

static std::optional<MetaValue> evalExpr(const ASTNode* node,
                                         const MetaEnv& env,
                                         std::vector<std::string>* diagnostics) {
    if (!node) return std::nullopt;

    switch (node->kind()) {
        case NodeKind::Number: {
            auto n = static_cast<const NumberNode*>(node);
            auto parsed = parseInt64(n->value());
            if (!parsed) {
                pushDiag(diagnostics, locPrefix(node) + "invalid integer literal");
                return std::nullopt;
            }
            return MetaValue::fromInt(*parsed);
        }
        case NodeKind::StringLiteral: {
            auto s = static_cast<const StringLiteralNode*>(node);
            return MetaValue::fromString(s->value());
        }
        case NodeKind::VarRef: {
            auto v = static_cast<const VarRefNode*>(node);
            auto it = env.find(v->name());
            if (it != env.end()) return it->second;
            return std::nullopt;
        }
        case NodeKind::Cast: {
            auto c = static_cast<const CastNode*>(node);
            auto inner = evalExpr(c->expr(), env, diagnostics);
            if (!inner) return std::nullopt;
            if (inner->kind == MetaValue::Kind::String) {
                pushDiag(diagnostics, locPrefix(node) + "cannot cast a string literal at compile time");
                return std::nullopt;
            }
            return MetaValue::fromInt(inner->intValue, c->targetType().isUnsigned);
        }
        case NodeKind::BinaryOp: {
            auto b = static_cast<const BinaryOpNode*>(node);
            auto lhs = evalExpr(b->left(), env, diagnostics);
            auto rhs = evalExpr(b->right(), env, diagnostics);
            if (!lhs || !rhs) return std::nullopt;

            if (lhs->kind == MetaValue::Kind::String || rhs->kind == MetaValue::Kind::String) {
                if (b->op() == TokenType::PLUS &&
                    lhs->kind == MetaValue::Kind::String &&
                    rhs->kind == MetaValue::Kind::String) {
                    return MetaValue::fromString(lhs->stringValue + rhs->stringValue);
                }
                if (b->op() == TokenType::DOUBLE_EQUAL || b->op() == TokenType::NOT_EQUAL) {
                    bool eq = lhs->kind == rhs->kind &&
                              lhs->stringValue == rhs->stringValue &&
                              lhs->intValue == rhs->intValue;
                    return MetaValue::fromInt((b->op() == TokenType::DOUBLE_EQUAL) ? (eq ? 1 : 0) : (eq ? 0 : 1));
                }
                pushDiag(diagnostics, locPrefix(node) + "unsupported compile-time operation on strings");
                return std::nullopt;
            }

            int64_t l = lhs->intValue;
            int64_t r = rhs->intValue;
            switch (b->op()) {
                case TokenType::PLUS: return MetaValue::fromInt(l + r);
                case TokenType::MINUS: return MetaValue::fromInt(l - r);
                case TokenType::MULTIPLY: return MetaValue::fromInt(l * r);
                case TokenType::DIVIDE:
                    if (r == 0) {
                        pushDiag(diagnostics, locPrefix(node) + "division by zero in comptime expression");
                        return std::nullopt;
                    }
                    return MetaValue::fromInt(l / r);
                case TokenType::LESS: return MetaValue::fromInt(l < r);
                case TokenType::GREATER: return MetaValue::fromInt(l > r);
                case TokenType::LESS_EQUAL: return MetaValue::fromInt(l <= r);
                case TokenType::GREATER_EQUAL: return MetaValue::fromInt(l >= r);
                case TokenType::DOUBLE_EQUAL: return MetaValue::fromInt(l == r);
                case TokenType::NOT_EQUAL: return MetaValue::fromInt(l != r);
                case TokenType::LOGICAL_AND: return MetaValue::fromInt(isTruthy(*lhs) && isTruthy(*rhs));
                case TokenType::LOGICAL_OR: return MetaValue::fromInt(isTruthy(*lhs) || isTruthy(*rhs));
                default:
                    pushDiag(diagnostics, locPrefix(node) + "unsupported comptime operator");
                    return std::nullopt;
            }
        }
        case NodeKind::Comptime: {
            auto u = static_cast<const ComptimeNode*>(node);
            return evalExpr(u->expr(), env, diagnostics);
        }
        default:
            return std::nullopt;
    }
}

static std::unique_ptr<ASTNode> lowerExpr(const ASTNode* node,
                                          MetaEnv& env,
                                          std::vector<std::string>* diagnostics) {
    if (!node) return nullptr;

    switch (node->kind()) {
        case NodeKind::Number: {
            auto n = static_cast<const NumberNode*>(node);
            return std::make_unique<NumberNode>(n->value());
        }
        case NodeKind::StringLiteral: {
            auto s = static_cast<const StringLiteralNode*>(node);
            return std::make_unique<StringLiteralNode>(s->value());
        }
        case NodeKind::VarRef: {
            auto v = static_cast<const VarRefNode*>(node);
            auto it = env.find(v->name());
            if (it != env.end()) {
                if (it->second.kind == MetaValue::Kind::String) return makeString(it->second.stringValue, node);
                return makeNumber(it->second.intValue, node);
            }
            return std::make_unique<VarRefNode>(v->name());
        }
        case NodeKind::BinaryOp: {
            auto b = static_cast<const BinaryOpNode*>(node);
            auto left = lowerExpr(b->left(), env, diagnostics);
            auto right = lowerExpr(b->right(), env, diagnostics);

            // Bottom-up evaluation
            if (auto val = evalExpr(node, env, nullptr)) {
                 if (val->kind == MetaValue::Kind::String) return makeString(val->stringValue, node);
                 return makeNumber(val->intValue, node);
            }

            auto clone = std::make_unique<BinaryOpNode>(std::move(left), b->op(), std::move(right));
            setLoc(clone.get(), node);
            return clone;
        }
        case NodeKind::Cast: {
            auto c = static_cast<const CastNode*>(node);
            auto inner = lowerExpr(c->expr(), env, diagnostics);
            if (auto val = evalExpr(node, env, nullptr)) {
                return makeNumber(val->intValue, node);
            }
            auto clone = std::make_unique<CastNode>(std::move(inner), c->targetType());
            setLoc(clone.get(), node);
            return clone;
        }
        case NodeKind::Comptime: {
            auto c = static_cast<const ComptimeNode*>(node);
            if (auto val = evalExpr(c->expr(), env, diagnostics)) {
                if (val->kind == MetaValue::Kind::String) return makeString(val->stringValue, node);
                return makeNumber(val->intValue, node);
            }
            return lowerExpr(c->expr(), env, diagnostics);
        }
        case NodeKind::Assign: {
            auto a = static_cast<const AssignNode*>(node);
            auto target = lowerExpr(a->target(), env, diagnostics);
            auto value = lowerExpr(a->value(), env, diagnostics);
            auto clone = std::make_unique<AssignNode>(std::move(target), std::move(value));
            setLoc(clone.get(), node);
            return clone;
        }
        case NodeKind::Call: {
            auto call = static_cast<const CallNode*>(node);
            std::vector<std::unique_ptr<ASTNode>> args;
            for (const auto& arg : call->args()) {
                args.push_back(lowerExpr(arg.get(), env, diagnostics));
            }
            auto clone = std::make_unique<CallNode>(call->name(), std::move(args));
            setLoc(clone.get(), node);
            return clone;
        }
        case NodeKind::Index: {
            auto idx = static_cast<const IndexNode*>(node);
            auto expr = lowerExpr(idx->expr(), env, diagnostics);
            auto index = lowerExpr(idx->index(), env, diagnostics);
            auto clone = std::make_unique<IndexNode>(std::move(expr), std::move(index));
            setLoc(clone.get(), node);
            return clone;
        }
        case NodeKind::MemberAccess: {
            auto mem = static_cast<const MemberAccessNode*>(node);
            auto expr = lowerExpr(mem->expr(), env, diagnostics);
            auto clone = std::make_unique<MemberAccessNode>(std::move(expr), mem->member());
            setLoc(clone.get(), node);
            return clone;
        }
        case NodeKind::Dereference: {
            auto d = static_cast<const DereferenceNode*>(node);
            auto expr = lowerExpr(d->expr(), env, diagnostics);
            auto clone = std::make_unique<DereferenceNode>(std::move(expr));
            setLoc(clone.get(), node);
            return clone;
        }
        case NodeKind::AddressOf: {
            auto a = static_cast<const AddressOfNode*>(node);
            auto expr = lowerExpr(a->expr(), env, diagnostics);
            auto clone = std::make_unique<AddressOfNode>(std::move(expr));
            setLoc(clone.get(), node);
            return clone;
        }
        default:
            return nullptr;
    }
}

static std::unique_ptr<ASTNode> lowerStatement(const ASTNode* node,
                                               MetaEnv& env,
                                               std::vector<std::string>* diagnostics,
                                               bool inComptime) {
    if (!node) return nullptr;

    switch (node->kind()) {
        case NodeKind::Program: {
            auto program = static_cast<const ProgramNode*>(node);
            auto lowered = lowerStatements(program->statements(), env, diagnostics, inComptime);
            auto out = std::make_unique<ProgramNode>(std::move(lowered));
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::Block: {
            auto block = static_cast<const BlockNode*>(node);
            if (inComptime) {
                auto scoped = env;
                (void)lowerStatements(block->statements(), scoped, diagnostics, true);
                env = std::move(scoped);
                return nullptr;
            }
            auto lowered = lowerStatements(block->statements(), env, diagnostics, false);
            auto out = std::make_unique<BlockNode>(std::move(lowered));
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::Comptime: {
            auto comptime = static_cast<const ComptimeNode*>(node);
            auto inner = comptime->expr();
            switch (inner->kind()) {
                case NodeKind::Block: {
                    auto block = static_cast<const BlockNode*>(inner);
                    auto scoped = env;
                    (void)lowerStatements(block->statements(), scoped, diagnostics, true);
                    env = std::move(scoped);
                    return nullptr;
                }
                case NodeKind::ConstDecl: {
                    auto decl = static_cast<const ConstDeclNode*>(inner);
                    auto init = decl->init() ? lowerExpr(decl->init(), env, diagnostics) : nullptr;
                    auto value = decl->init() ? evalExpr(init.get(), env, diagnostics) : std::nullopt;
                    if (value) env[decl->name()] = *value;
                    return nullptr;
                }
                case NodeKind::VarDecl: {
                    auto decl = static_cast<const VarDeclNode*>(inner);
                    auto init = decl->init() ? lowerExpr(decl->init(), env, diagnostics) : nullptr;
                    auto value = decl->init() ? evalExpr(init.get(), env, diagnostics) : std::nullopt;
                    if (value) env[decl->name()] = *value;
                    return nullptr;
                }
                case NodeKind::Assign: {
                    auto asgn = static_cast<const AssignNode*>(inner);
                    auto target = (asgn->target()->kind() == NodeKind::VarRef) ? static_cast<const VarRefNode*>(asgn->target()) : nullptr;
                    auto value = evalExpr(asgn->value(), env, diagnostics);
                    if (target && value) {
                        env[target->name()] = *value;
                        return nullptr;
                    }
                    pushDiag(diagnostics, locPrefix(node) + "comptime assignment requires a simple variable target and constant value");
                    return nullptr;
                }
                case NodeKind::If: {
                    auto ifNode = static_cast<const IfNode*>(inner);
                    auto cond = evalExpr(ifNode->cond(), env, diagnostics);
                    if (!cond) {
                        pushDiag(diagnostics, locPrefix(node) + "comptime if requires a constant condition");
                        return nullptr;
                    }
                    if (isTruthy(*cond)) {
                        auto lowered = lowerStatement(ifNode->thenBranch(), env, diagnostics, true);
                        if (lowered) (void)lowered;
                    } else if (ifNode->elseBranch()) {
                        auto lowered = lowerStatement(ifNode->elseBranch(), env, diagnostics, true);
                        if (lowered) (void)lowered;
                    }
                    return nullptr;
                }
                case NodeKind::While: {
                    auto whileNode = static_cast<const WhileNode*>(inner);
                    for (int i = 0; i < 1024; ++i) {
                        auto cond = evalExpr(whileNode->cond(), env, diagnostics);
                        if (!cond) {
                            pushDiag(diagnostics, locPrefix(node) + "comptime while requires a constant condition");
                            return nullptr;
                        }
                        if (!isTruthy(*cond)) break;
                        auto lowered = lowerStatement(whileNode->body(), env, diagnostics, true);
                        if (lowered) (void)lowered;
                    }
                    return nullptr;
                }
                default: {
                    auto lowered = lowerStatement(inner, env, diagnostics, true);
                    return lowered;
                }
            }
        }
        case NodeKind::ConstDecl: {
            auto decl = static_cast<const ConstDeclNode*>(node);
            auto init = decl->init() ? lowerExpr(decl->init(), env, diagnostics) : nullptr;
            if (init) {
                auto value = evalExpr(init.get(), env, diagnostics);
                if (value) env[decl->name()] = *value;
            }
            auto out = std::make_unique<ConstDeclNode>(decl->name(), decl->type(), std::move(init));
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::VarDecl: {
            auto decl = static_cast<const VarDeclNode*>(node);
            auto init = decl->init() ? lowerExpr(decl->init(), env, diagnostics) : nullptr;
            if (inComptime && init) {
                if (auto value = evalExpr(init.get(), env, diagnostics)) {
                    env[decl->name()] = *value;
                    return nullptr;
                }
            }
            auto out = std::make_unique<VarDeclNode>(decl->name(), decl->type(), std::move(init));
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::Assign: {
            auto asgn = static_cast<const AssignNode*>(node);
            auto target = lowerExpr(asgn->target(), env, diagnostics);
            auto value = lowerExpr(asgn->value(), env, diagnostics);

            if (inComptime) {
                if (target->kind() == NodeKind::VarRef) {
                    auto var = static_cast<VarRefNode*>(target.get());
                    auto constant = evalExpr(value.get(), env, diagnostics);
                    if (constant) {
                        env[var->name()] = *constant;
                        return nullptr;
                    }
                }
                pushDiag(diagnostics, locPrefix(node) + "comptime assignment requires a simple variable target and constant value");
                return nullptr;
            }

            auto out = std::make_unique<AssignNode>(std::move(target), std::move(value));
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::Return: {
            auto ret = static_cast<const ReturnNode*>(node);
            auto value = ret->value() ? lowerExpr(ret->value(), env, diagnostics) : nullptr;
            auto out = std::make_unique<ReturnNode>(std::move(value));
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::If: {
            auto ifNode = static_cast<const IfNode*>(node);
            auto cond = lowerExpr(ifNode->cond(), env, diagnostics);
            auto thenBranch = lowerStatement(ifNode->thenBranch(), env, diagnostics, inComptime);
            auto elseBranch = ifNode->elseBranch() ? lowerStatement(ifNode->elseBranch(), env, diagnostics, inComptime) : nullptr;
            auto out = std::make_unique<IfNode>(std::move(cond), std::move(thenBranch), std::move(elseBranch));
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::While: {
            auto whileNode = static_cast<const WhileNode*>(node);
            auto cond = lowerExpr(whileNode->cond(), env, diagnostics);
            auto body = lowerStatement(whileNode->body(), env, diagnostics, inComptime);
            auto out = std::make_unique<WhileNode>(std::move(cond), std::move(body));
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::FuncDef: {
            auto fn = static_cast<const FuncDefNode*>(node);
            MetaEnv fnEnv = env;
            auto body = lowerStatement(fn->body(), fnEnv, diagnostics, false);
            auto out = std::make_unique<FuncDefNode>(fn->name(), fn->params(), fn->retType(),
                                                     std::unique_ptr<BlockNode>(static_cast<BlockNode*>(body.release())));
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::StructDef: {
            auto structDef = static_cast<const StructDefNode*>(node);
            auto out = std::make_unique<StructDefNode>(structDef->name(), structDef->members());
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::Import: {
            auto imp = static_cast<const ImportNode*>(node);
            auto out = std::make_unique<ImportNode>(imp->path());
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::Foreign: {
            auto foreign = static_cast<const ForeignNode*>(node);
            auto out = std::make_unique<ForeignNode>(foreign->lib(), foreign->func());
            setLoc(out.get(), node);
            return out;
        }
        case NodeKind::Asm: {
            auto asmNode = static_cast<const AsmNode*>(node);
            auto out = std::make_unique<AsmNode>(asmNode->code());
            setLoc(out.get(), node);
            return out;
        }
        default: {
            if (auto expr = lowerExpr(node, env, diagnostics)) {
                return expr;
            }
            return nullptr;
        }
    }
}

} // namespace

std::unique_ptr<ASTNode> lowerComptime(std::unique_ptr<ASTNode> root,
                                       std::vector<std::string>* diagnostics) {
    MetaEnv env;
    return lowerStatement(root.get(), env, diagnostics, false);
}
