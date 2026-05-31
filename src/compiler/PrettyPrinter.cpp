#include "compiler/PrettyPrinter.h"
#include <sstream>

namespace {

std::string indent(int depth) {
    return std::string(depth * 2, ' ');
}

std::string tokenName(TokenType t) {
    switch (t) {
        case TokenType::PLUS: return "+";
        case TokenType::MINUS: return "-";
        case TokenType::MULTIPLY: return "*";
        case TokenType::DIVIDE: return "/";
        case TokenType::LESS: return "<";
        case TokenType::GREATER: return ">";
        case TokenType::LESS_EQUAL: return "<=";
        case TokenType::GREATER_EQUAL: return ">=";
        case TokenType::DOUBLE_EQUAL: return "==";
        case TokenType::NOT_EQUAL: return "!=";
        case TokenType::LOGICAL_AND: return "&&";
        case TokenType::LOGICAL_OR: return "||";
        default: return "?";
    }
}

void printNode(std::ostringstream& out, const ASTNode* node, int depth);

void printChildren(std::ostringstream& out, const std::vector<std::unique_ptr<ASTNode>>& nodes, int depth) {
    for (const auto& child : nodes) {
        printNode(out, child.get(), depth);
    }
}

void printNode(std::ostringstream& out, const ASTNode* node, int depth) {
    if (!node) {
        out << indent(depth) << "<null>\n";
        return;
    }

    switch (node->kind()) {
        case NodeKind::Program: {
            auto n = static_cast<const ProgramNode*>(node);
            out << indent(depth) << "Program\n";
            printChildren(out, n->statements(), depth + 1);
            break;
        }
        case NodeKind::Block: {
            auto n = static_cast<const BlockNode*>(node);
            out << indent(depth) << "Block\n";
            printChildren(out, n->statements(), depth + 1);
            break;
        }
        case NodeKind::Number: {
            auto n = static_cast<const NumberNode*>(node);
            out << indent(depth) << "Number " << n->value() << "\n";
            break;
        }
        case NodeKind::StringLiteral: {
            auto n = static_cast<const StringLiteralNode*>(node);
            out << indent(depth) << "String \"" << n->value() << "\"\n";
            break;
        }
        case NodeKind::VarRef: {
            auto n = static_cast<const VarRefNode*>(node);
            out << indent(depth) << "VarRef " << n->name() << "\n";
            break;
        }
        case NodeKind::BinaryOp: {
            auto n = static_cast<const BinaryOpNode*>(node);
            out << indent(depth) << "BinaryOp " << tokenName(n->op()) << "\n";
            printNode(out, n->left(), depth + 1);
            printNode(out, n->right(), depth + 1);
            break;
        }
        case NodeKind::Assign: {
            auto n = static_cast<const AssignNode*>(node);
            out << indent(depth) << "Assign\n";
            printNode(out, n->target(), depth + 1);
            printNode(out, n->value(), depth + 1);
            break;
        }
        case NodeKind::VarDecl: {
            auto n = static_cast<const VarDeclNode*>(node);
            out << indent(depth) << "VarDecl " << n->name() << "\n";
            printNode(out, n->init(), depth + 1);
            break;
        }
        case NodeKind::Call: {
            auto n = static_cast<const CallNode*>(node);
            out << indent(depth) << "Call " << n->name() << "\n";
            for (const auto& arg : n->args()) {
                printNode(out, arg.get(), depth + 1);
            }
            break;
        }
        case NodeKind::MemberAccess: {
            auto n = static_cast<const MemberAccessNode*>(node);
            out << indent(depth) << "MemberAccess ." << n->member() << "\n";
            printNode(out, n->expr(), depth + 1);
            break;
        }
        case NodeKind::Index: {
            auto n = static_cast<const IndexNode*>(node);
            out << indent(depth) << "Index\n";
            printNode(out, n->expr(), depth + 1);
            printNode(out, n->index(), depth + 1);
            break;
        }
        case NodeKind::Cast: {
            auto n = static_cast<const CastNode*>(node);
            out << indent(depth) << "Cast\n";
            printNode(out, n->expr(), depth + 1);
            break;
        }
        case NodeKind::AddressOf: {
            auto n = static_cast<const AddressOfNode*>(node);
            out << indent(depth) << "AddressOf\n";
            printNode(out, n->expr(), depth + 1);
            break;
        }
        case NodeKind::Dereference: {
            auto n = static_cast<const DereferenceNode*>(node);
            out << indent(depth) << "Dereference\n";
            printNode(out, n->expr(), depth + 1);
            break;
        }
        case NodeKind::Return: {
            auto n = static_cast<const ReturnNode*>(node);
            out << indent(depth) << "Return\n";
            printNode(out, n->value(), depth + 1);
            break;
        }
        case NodeKind::If: {
            auto n = static_cast<const IfNode*>(node);
            out << indent(depth) << "If\n";
            printNode(out, n->cond(), depth + 1);
            printNode(out, n->thenBranch(), depth + 1);
            printNode(out, n->elseBranch(), depth + 1);
            break;
        }
        case NodeKind::While: {
            auto n = static_cast<const WhileNode*>(node);
            out << indent(depth) << "While\n";
            printNode(out, n->cond(), depth + 1);
            printNode(out, n->body(), depth + 1);
            break;
        }
        case NodeKind::FuncDef: {
            auto n = static_cast<const FuncDefNode*>(node);
            out << indent(depth) << "Func " << n->name() << "\n";
            printNode(out, n->body(), depth + 1);
            break;
        }
        case NodeKind::StructDef: {
            auto n = static_cast<const StructDefNode*>(node);
            out << indent(depth) << "Struct " << n->name() << "\n";
            for (const auto& m : n->members()) {
                out << indent(depth + 1) << "Member " << m.name << "\n";
            }
            break;
        }
        case NodeKind::Import: {
            auto n = static_cast<const ImportNode*>(node);
            out << indent(depth) << "Import " << n->path() << "\n";
            break;
        }
        case NodeKind::Foreign: {
            auto n = static_cast<const ForeignNode*>(node);
            out << indent(depth) << "Foreign " << n->lib() << " " << n->func() << "\n";
            break;
        }
        case NodeKind::Asm: {
            auto n = static_cast<const AsmNode*>(node);
            out << indent(depth) << "Asm {" << n->code() << "}\n";
            break;
        }
        case NodeKind::ConstDecl: {
            auto n = static_cast<const ConstDeclNode*>(node);
            out << indent(depth) << "ConstDecl " << n->name() << "\n";
            printNode(out, n->init(), depth + 1);
            break;
        }
        case NodeKind::Comptime: {
            auto n = static_cast<const ComptimeNode*>(node);
            out << indent(depth) << "Comptime\n";
            printNode(out, n->expr(), depth + 1);
            break;
        }
        default:
            out << indent(depth) << "<unknown-node>\n";
            break;
    }
}

} // namespace

std::string prettyPrintAst(const ASTNode* node) {
    std::ostringstream out;
    printNode(out, node, 0);
    return out.str();
}
