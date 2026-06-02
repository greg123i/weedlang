#include "compiler/Codegen.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>

struct StructInfo {
    std::string name;
    std::unordered_map<std::string, int> memberOffsets;
    std::unordered_map<std::string, int> memberSizes;
    std::unordered_map<std::string, Type> memberTypes;
    int size;
    int align;
};

static std::unordered_map<std::string, StructInfo> globalStructs;

static int align16(int n) {
    return (n + 15) & ~15;
}

static int sizeOfType(const Type& t) {
    return t.size([](const std::string& name) {
        auto it = globalStructs.find(name);
        return it != globalStructs.end() ? it->second.size : 0;
    });
}

static int alignOfType(const Type& t) {
    return t.alignment([](const std::string& name) {
        auto it = globalStructs.find(name);
        return it != globalStructs.end() ? it->second.align : 1;
    });
}

static int alignUp(int x, int a) {
    return (x + (a - 1)) & ~(a - 1);
}

static const char* ptrSizeQualifier(int size) {
    switch (size) {
        case 1: return "byte";
        case 2: return "word";
        case 4: return "dword";
        case 8: return "qword";
        default: return "qword";
    }
}

static void emitLoadExtended(std::stringstream& out, const std::string& addrExpr, int size, bool isUnsigned) {
    if (size == 1) out << (isUnsigned ? "    movzx rax, byte " : "    movsx rax, byte ") << addrExpr << "\n";
    else if (size == 2) out << (isUnsigned ? "    movzx rax, word " : "    movsx rax, word ") << addrExpr << "\n";
    else if (size == 4) out << (isUnsigned ? "    mov eax, dword " : "    movsxd rax, dword ") << addrExpr << "\n";
    else out << "    mov rax, qword " << addrExpr << "\n";
}

static std::string nodeLoc(const ASTNode* node) {
    const auto& loc = node->location();
    if (!loc.file || loc.file->empty()) return "";
    return *loc.file + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column) + ": ";
}

static std::streambuf* swapOut(std::stringstream& from, std::stringstream& to) {
    std::ostream& o = from;
    return o.rdbuf(to.rdbuf());
}

static void restoreOut(std::stringstream& from, std::streambuf* saved) {
    std::ostream& o = from;
    o.rdbuf(saved);
}

NASMVisitor::FuncContext& NASMVisitor::ctx() {
    if (ctxStack.empty()) {
        throw std::runtime_error("internal codegen error: no active function context");
    }
    return ctxStack.back();
}

const NASMVisitor::FuncContext& NASMVisitor::ctx() const {
    if (ctxStack.empty()) {
        throw std::runtime_error("internal codegen error: no active function context");
    }
    return ctxStack.back();
}

std::string NASMVisitor::freshLabel(const std::string& prefix) {
    return prefix + "_" + std::to_string(labelCounter++);
}

void NASMVisitor::beginFunction(const std::string& retLabel) {
    FuncContext c;
    c.returnLabel = retLabel;
    ctxStack.push_back(std::move(c));
}

void NASMVisitor::endFunction() {
    ctxStack.pop_back();
}

int NASMVisitor::pushTmp() {
    int off = ctx().currentStackOffset + 8;
    ctx().currentStackOffset += 8;
    asmCode << "    mov qword [rbp - " << (32 + off) << "], rax\n";
    return off;
}

void NASMVisitor::popTmp(int slotOff, const std::string& reg) {
    asmCode << "    mov " << reg << ", qword [rbp - " << (32 + slotOff) << "]\n";
    freeTmp();
}

void NASMVisitor::freeTmp() {
    ctx().currentStackOffset -= 8;
}

void NASMVisitor::pushType(Type t) {
    typeStack.push_back(std::move(t));
}

Type NASMVisitor::popType() {
    if (typeStack.empty()) {
        throw std::runtime_error("internal codegen error: type stack underflow");
    }
    Type t = std::move(typeStack.back());
    typeStack.pop_back();
    return t;
}

void NASMVisitor::visit(StructDefNode* node) {
    StructInfo info;
    info.name = node->name();
    int size = 0;
    int maxAlign = 1;
    for (const auto& m : node->members()) {
        info.memberOffsets[m.name] = m.offset;
        info.memberTypes[m.name] = m.type;
        int mSize = sizeOfType(m.type);
        info.memberSizes[m.name] = mSize;
        int a = alignOfType(m.type);
        if (a > maxAlign) maxAlign = a;
        size = std::max(size, m.offset + mSize);
    }
    info.align = maxAlign;
    info.size = alignUp(size, maxAlign);
    globalStructs[node->name()] = info;
}

void NASMVisitor::visit(MemberAccessNode* node) {
    if (node->expr()->kind() == NodeKind::VarRef) {
        auto var = static_cast<VarRefNode*>(node->expr());
        if (ctx().varTypes.count(var->name())) {
            Type baseType = ctx().varTypes[var->name()];
            if (baseType.baseType == DataType::STRUCT_TYPE && baseType.pointerLevel == 0) {
                std::string sTypeName = baseType.structName;
                if (!globalStructs.count(sTypeName)) throw std::runtime_error(nodeLoc(node) + "unknown struct type: " + sTypeName);
                int offset = globalStructs[sTypeName].memberOffsets[node->member()];
                int mSize = globalStructs[sTypeName].memberSizes[node->member()];
                Type mType = globalStructs[sTypeName].memberTypes[node->member()];
                std::string addr = "[rbp - " + std::to_string(32 + ctx().stackOffsets[var->name()] - offset) + "]";
                emitLoadExtended(asmCode, addr, mSize, mType.isUnsigned);
                pushType(mType);
                return;
            }
        }
    }

    node->expr()->accept(this);
    Type baseType = popType();

    if (baseType.pointerLevel > 0 && baseType.baseType == DataType::STRUCT_TYPE) {
        std::string sTypeName = baseType.structName;
        if (!globalStructs.count(sTypeName)) throw std::runtime_error(nodeLoc(node) + "unknown struct type: " + sTypeName);
        int offset = globalStructs[sTypeName].memberOffsets[node->member()];
        int mSize = globalStructs[sTypeName].memberSizes[node->member()];
        Type mType = globalStructs[sTypeName].memberTypes[node->member()];
        std::string addr = "[rax + " + std::to_string(offset) + "]";
        emitLoadExtended(asmCode, addr, mSize, mType.isUnsigned);
        pushType(mType);
        return;
    }

    throw std::runtime_error(nodeLoc(node) + "member access requires a struct variable or pointer to struct");
}

void NASMVisitor::visit(AssignNode* node) {
    node->value()->accept(this);
    Type rhsType = popType();

    if (node->target()->kind() == NodeKind::VarRef) {
        auto var = static_cast<VarRefNode*>(node->target());
        if (!ctx().stackOffsets.count(var->name())) {
            throw std::runtime_error(nodeLoc(node) + "unknown variable in assignment target: " + var->name());
        }
        Type targetType = ctx().varTypes[var->name()];
        int sz = sizeOfType(targetType);
        if (targetType.arraySize > 0) throw std::runtime_error(nodeLoc(node) + "cannot assign to an array variable directly");

        asmCode << "    mov " << ptrSizeQualifier(sz) << " [rbp - " << (32 + ctx().stackOffsets[var->name()]) << "], ";
        if (sz == 1) asmCode << "al\n";
        else if (sz == 2) asmCode << "ax\n";
        else if (sz == 4) asmCode << "eax\n";
        else asmCode << "rax\n";
        pushType(targetType);
        return;
    }

    if (node->target()->kind() == NodeKind::MemberAccess) {
        auto mem = static_cast<MemberAccessNode*>(node->target());
        int rhsOff = pushTmp();
        
        // Try VarRef optimization first (direct struct variable)
        if (mem->expr()->kind() == NodeKind::VarRef) {
            auto baseVar = static_cast<VarRefNode*>(mem->expr());
            if (ctx().varTypes.count(baseVar->name())) {
                Type baseType = ctx().varTypes[baseVar->name()];
                if (baseType.baseType == DataType::STRUCT_TYPE && baseType.pointerLevel == 0) {
                    std::string sTypeName = baseType.structName;
                    int offset = globalStructs[sTypeName].memberOffsets[mem->member()];
                    int mSize = globalStructs[sTypeName].memberSizes[mem->member()];
                    popTmp(rhsOff, "rbx");
                    asmCode << "    mov " << ptrSizeQualifier(mSize) << " [rbp - " << (32 + ctx().stackOffsets[baseVar->name()] - offset) << "], ";
                    if (mSize == 1) asmCode << "bl\n";
                    else if (mSize == 2) asmCode << "bx\n";
                    else if (mSize == 4) asmCode << "ebx\n";
                    else asmCode << "rbx\n";
                    pushType(Type(DataType::VOID));
                    return;
                }
            }
        }

        // General case: evaluate expression and hope it's a pointer to struct
        mem->expr()->accept(this);
        Type baseType = popType();
        popTmp(rhsOff, "rbx");

        if (baseType.pointerLevel > 0 && baseType.baseType == DataType::STRUCT_TYPE) {
            std::string sTypeName = baseType.structName;
            int offset = globalStructs[sTypeName].memberOffsets[mem->member()];
            int mSize = globalStructs[sTypeName].memberSizes[mem->member()];
            asmCode << "    mov " << ptrSizeQualifier(mSize) << " [rax + " << offset << "], ";
            if (mSize == 1) asmCode << "bl\n";
            else if (mSize == 2) asmCode << "bx\n";
            else if (mSize == 4) asmCode << "ebx\n";
            else asmCode << "rbx\n";
            pushType(Type(DataType::VOID));
            return;
        }
        throw std::runtime_error(nodeLoc(node) + "assignment to a member expression requires a struct variable or pointer to struct");
    }

    if (node->target()->kind() == NodeKind::Index) {
        auto idx = static_cast<IndexNode*>(node->target());
        int rhsOff = pushTmp();
        idx->expr()->accept(this);
        Type baseType = popType();
        int baseOff = pushTmp();
        idx->index()->accept(this);
        popType();
        asmCode << "    mov rbx, rax\n";
        popTmp(baseOff, "rax");

        int storeSize = 8;
        if (baseType.pointerLevel > 0) {
            Type pointed = baseType;
            pointed.pointerLevel--;
            storeSize = sizeOfType(pointed);
            if (storeSize > 1) asmCode << "    imul rbx, " << storeSize << "\n";
            asmCode << "    add rax, rbx\n";
        }
        popTmp(rhsOff, "rbx");
        asmCode << "    mov " << ptrSizeQualifier(storeSize) << " [rax], ";
        if (storeSize == 1) asmCode << "bl\n";
        else if (storeSize == 2) asmCode << "bx\n";
        else if (storeSize == 4) asmCode << "ebx\n";
        else asmCode << "rbx\n";
        pushType(baseType); // simplified
        return;
    }

    if (node->target()->kind() == NodeKind::Dereference) {
        auto deref = static_cast<DereferenceNode*>(node->target());
        int rhsOff = pushTmp();
        deref->expr()->accept(this);
        Type ptrType = popType();
        popTmp(rhsOff, "rbx");
        int storeSize = 8;
        if (ptrType.pointerLevel > 0) {
            Type pointed = ptrType;
            pointed.pointerLevel--;
            storeSize = sizeOfType(pointed);
        }
        asmCode << "    mov " << ptrSizeQualifier(storeSize) << " [rax], ";
        if (storeSize == 1) asmCode << "bl\n";
        else if (storeSize == 2) asmCode << "bx\n";
        else if (storeSize == 4) asmCode << "ebx\n";
        else asmCode << "rbx\n";
        Type resT = ptrType; if (resT.pointerLevel > 0) resT.pointerLevel--;
        pushType(resT);
        return;
    }
    throw std::runtime_error(nodeLoc(node) + "unsupported assignment target");
}

void NASMVisitor::visit(VarDeclNode* node) {
    ctx().varTypes[node->name()] = node->type();
    if (node->type().baseType == DataType::STRUCT_TYPE && node->type().pointerLevel == 0) {
        ctx().varToStruct[node->name()] = node->type().structName;
        int size = globalStructs[node->type().structName].size;
        int a = globalStructs[node->type().structName].align;
        ctx().currentStackOffset = alignUp(ctx().currentStackOffset, a);
        ctx().currentStackOffset += size;
        ctx().stackOffsets[node->name()] = ctx().currentStackOffset;

        asmCode << "    xor rax, rax\n";
        for (int off = 0; off < size; off += 8) {
            asmCode << "    mov qword [rbp - " << (32 + ctx().stackOffsets[node->name()] - off) << "], rax\n";
        }
    } else {
        int sz = sizeOfType(node->type());
        int a = alignOfType(node->type());
        ctx().currentStackOffset = alignUp(ctx().currentStackOffset, a);
        ctx().currentStackOffset += sz;
        ctx().stackOffsets[node->name()] = ctx().currentStackOffset;

        if (node->init()) {
            node->init()->accept(this);
            popType();
        } else {
            asmCode << "    xor rax, rax\n";
            if (node->type().arraySize > 0) {
                for (int off = 0; off < sz; off += 1) {
                   asmCode << "    mov byte [rbp - " << (32 + ctx().stackOffsets[node->name()] - off) << "], 0\n";
                }
            }
        }

        if (node->type().arraySize == 0) {
            asmCode << "    mov " << ptrSizeQualifier(sz) << " [rbp - " << (32 + ctx().stackOffsets[node->name()]) << "], ";
            if (sz == 1) asmCode << "al\n";
            else if (sz == 2) asmCode << "ax\n";
            else if (sz == 4) asmCode << "eax\n";
            else asmCode << "rax\n";
        }
    }
}

void NASMVisitor::visit(VarRefNode* node) {
    if (ctx().stackOffsets.count(node->name())) {
        Type t = ctx().varTypes[node->name()];
        int sz = sizeOfType(t);
        if (t.arraySize > 0) {
            asmCode << "    lea rax, [rbp - " << (32 + ctx().stackOffsets[node->name()]) << "]\n";
            Type ptrT = t; ptrT.arraySize = 0; ptrT.pointerLevel++;
            pushType(ptrT);
        } else {
            std::string addr = "[rbp - " + std::to_string(32 + ctx().stackOffsets[node->name()]) + "]";
            emitLoadExtended(asmCode, addr, sz, t.isUnsigned);
            pushType(t);
        }
        return;
    }
    throw std::runtime_error(nodeLoc(node) + "unknown variable: " + node->name());
}

void NASMVisitor::visit(AddressOfNode* node) {
    if (node->expr()->kind() == NodeKind::VarRef) {
        auto var = static_cast<VarRefNode*>(node->expr());
        asmCode << "    lea rax, [rbp - " << (32 + ctx().stackOffsets[var->name()]) << "]\n";
        Type t = ctx().varTypes[var->name()]; t.pointerLevel++;
        pushType(t);
        return;
    }
    throw std::runtime_error(nodeLoc(node) + "address-of currently only supports variables");
}

void NASMVisitor::visit(DereferenceNode* node) {
    node->expr()->accept(this);
    Type ptrType = popType();
    int loadSize = 8; bool isUnsigned = false; Type resType;
    if (ptrType.pointerLevel > 0) {
        resType = ptrType; resType.pointerLevel--;
        loadSize = sizeOfType(resType); isUnsigned = resType.isUnsigned;
    } else {
        resType = Type(DataType::I64);
    }
    std::string addr = "[rax]";
    emitLoadExtended(asmCode, addr, loadSize, isUnsigned);
    pushType(resType);
}

void NASMVisitor::visit(CastNode* node) {
    node->expr()->accept(this);
    popType();
    Type targetT = node->targetType();
    if (targetT.pointerLevel > 0 || targetT.baseType == DataType::STRUCT_TYPE) {
        pushType(targetT);
        return;
    }
    int sz = sizeOfType(targetT);
    if (sz == 1) asmCode << (targetT.isUnsigned ? "    movzx rax, al\n" : "    movsx rax, al\n");
    else if (sz == 2) asmCode << (targetT.isUnsigned ? "    movzx rax, ax\n" : "    movsx rax, ax\n");
    else if (sz == 4) asmCode << (targetT.isUnsigned ? "    mov eax, eax\n" : "    movsxd rax, eax\n");
    pushType(targetT);
}

void NASMVisitor::visit(NumberNode* node) {
    asmCode << "    mov rax, " << node->value() << "\n";
    pushType(Type(DataType::I64));
}

void NASMVisitor::visit(BinaryOpNode* node) {
    if (node->op() == TokenType::LOGICAL_AND || node->op() == TokenType::LOGICAL_OR) {
        std::string skipL = freshLabel(node->op() == TokenType::LOGICAL_AND ? "and_skip" : "or_skip");
        node->left()->accept(this); popType();
        asmCode << "    cmp rax, 0\n";
        asmCode << (node->op() == TokenType::LOGICAL_AND ? "    je " : "    jne ") << skipL << "\n";
        node->right()->accept(this); popType();
        asmCode << "    cmp rax, 0\n";
        asmCode << "    setne al\n";
        asmCode << "    movzx rax, al\n";
        asmCode << skipL << ":\n";
        pushType(Type(DataType::I32));
        return;
    }

    node->left()->accept(this); Type leftType = popType(); int leftOff = pushTmp();
    node->right()->accept(this); Type rightType = popType();
    asmCode << "    mov rbx, rax\n";
    popTmp(leftOff, "rax");

    if (node->op() == TokenType::PLUS || node->op() == TokenType::MINUS) {
        Type resT = leftType;
        if (leftType.pointerLevel > 0) {
            Type pointed = leftType; pointed.pointerLevel--;
            int scale = sizeOfType(pointed);
            if (scale > 1) asmCode << "    imul rbx, " << scale << "\n";
        } else if (node->op() == TokenType::PLUS && rightType.pointerLevel > 0) {
            Type pointed = rightType; pointed.pointerLevel--;
            int scale = sizeOfType(pointed);
            if (scale > 1) asmCode << "    imul rax, " << scale << "\n";
            resT = rightType;
        }
        if (node->op() == TokenType::PLUS) asmCode << "    add rax, rbx\n";
        else asmCode << "    sub rax, rbx\n";
        pushType(resT);
    } else if (node->op() == TokenType::MULTIPLY) {
        asmCode << "    imul rax, rbx\n"; pushType(leftType);
    } else if (node->op() == TokenType::DIVIDE) {
        asmCode << "    cqo\n    idiv rbx\n"; pushType(leftType);
    } else {
        asmCode << "    cmp rax, rbx\n";
        if (node->op() == TokenType::DOUBLE_EQUAL) asmCode << "    sete al\n";
        else if (node->op() == TokenType::NOT_EQUAL) asmCode << "    setne al\n";
        else if (node->op() == TokenType::LESS) asmCode << "    setl al\n";
        else if (node->op() == TokenType::GREATER) asmCode << "    setg al\n";
        else if (node->op() == TokenType::LESS_EQUAL) asmCode << "    setle al\n";
        else if (node->op() == TokenType::GREATER_EQUAL) asmCode << "    setge al\n";
        asmCode << "    movzx rax, al\n";
        pushType(Type(DataType::I32));
    }
}

void NASMVisitor::visit(CallNode* node) {
    static const char* argRegs[] = {"rcx", "rdx", "r8", "r9"};
    const auto& args = node->args();
    int stackArgCount = (int)args.size() > 4 ? (int)args.size() - 4 : 0;
    int callFrame = 32 + stackArgCount * 8;
    if ((callFrame & 15) != 0) callFrame += 8;

    std::vector<int> argSlots;
    for (int i = 0; i < (int)args.size(); ++i) {
        args[i]->accept(this); popType();
        argSlots.push_back(pushTmp());
    }
    asmCode << "    sub rsp, " << callFrame << "\n";
    for (int i = 0; i < (int)args.size(); ++i) {
        asmCode << "    mov rax, [rbp - " << (32 + argSlots[i]) << "]\n";
        if (i < 4) asmCode << "    mov " << argRegs[i] << ", rax\n";
        else asmCode << "    mov [rsp + " << (32 + (i - 4) * 8) << "], rax\n";
    }
    for (int i = 0; i < (int)args.size(); ++i) freeTmp();
    std::string target = node->name();
    Type resType(DataType::I64);
    if (userFunctions.count(target)) {
        resType = userFunctions[target].retType;
        target = "wl_" + target;
    }
    asmCode << "    call " << target << "\n";
    asmCode << "    add rsp, " << callFrame << "\n";
    pushType(resType);
}

void NASMVisitor::visit(ForeignNode* node) {
    imports[node->func()] = node->lib();
}

void NASMVisitor::visit(IndexNode* node) {
    node->expr()->accept(this); Type baseType = popType(); int baseOff = pushTmp();
    node->index()->accept(this); popType();
    asmCode << "    mov rbx, rax\n";
    popTmp(baseOff, "rax");
    if (baseType.pointerLevel > 0) {
        Type pointed = baseType; pointed.pointerLevel--;
        int scale = sizeOfType(pointed);
        if (scale > 1) asmCode << "    imul rbx, " << scale << "\n";
        asmCode << "    add rax, rbx\n";
        emitLoadExtended(asmCode, "[rax]", sizeOfType(pointed), pointed.isUnsigned);
        pushType(pointed);
    } else throw std::runtime_error(nodeLoc(node) + "cannot index a non-pointer expression");
}

void NASMVisitor::visit(StringLiteralNode* node) {
    std::string label = freshLabel("str");
    strings[node->value()].push_back(label);
    asmCode << "    lea rax, [" << label << "]\n";
    pushType(Type(DataType::I8, 1));
}

void NASMVisitor::visit(ProgramNode* node) {
    for (const auto& stmt : node->statements()) {
        if (stmt->kind() == NodeKind::FuncDef) {
            auto fn = static_cast<FuncDefNode*>(stmt.get());
            userFunctions[fn->name()] = {fn->name(), fn->retType()};
        }
    }
    if (emitLibraryMode) {
        asmCode << "bits 64\ndefault rel\n";
        for (const auto& stmt : node->statements()) {
            if (stmt->kind() == NodeKind::StructDef || stmt->kind() == NodeKind::Import || stmt->kind() == NodeKind::Foreign || stmt->kind() == NodeKind::FuncDef)
                stmt->accept(this);
        }
        return;
    }
    asmCode << "bits 64\ndefault rel\nmain:\n    sub rsp, 40\n";
    if (userFunctions.count("main")) asmCode << "    call wl_main\n";
    else asmCode << "    call wl__toplevel\n";
    asmCode << "    mov rcx, rax\n    call ExitProcess\n    add rsp, 40\n    ret\n";
    asmCode << "wl__toplevel:\n";
    {
        beginFunction("wl__toplevel_ret");
        std::stringstream body; auto* saved = swapOut(asmCode, body);
        for (const auto& stmt : node->statements()) {
            if (stmt->kind() == NodeKind::FuncDef) continue;
            stmt->accept(this);
            if (stmt->isExpression()) popType();
        }
        asmCode << ctx().returnLabel << ":\n"; restoreOut(asmCode, saved);
        int locals = align16(ctx().currentStackOffset);
        asmCode << "    push rbp\n    mov rbp, rsp\n    sub rsp, " << (32 + locals) << "\n";
        asmCode << body.str();
        asmCode << "    add rsp, " << (32 + locals) << "\n    pop rbp\n    ret\n";
        endFunction();
    }
    for (const auto& stmt : node->statements()) if (stmt->kind() == NodeKind::FuncDef) stmt->accept(this);
    if (!strings.empty()) {
        asmCode << "section .rdata\n";
        for (const auto& [str, labels] : strings) {
            for (const auto& label : labels) {
                asmCode << "global " << label << "\n" << label << ": db ";
                for (size_t i = 0; i < str.length(); ++i) {
                    if (str[i] == '\\' && i + 1 < str.length()) {
                       if (str[i+1] == 'n') { asmCode << "10, "; i++; continue; }
                       if (str[i+1] == 'r') { asmCode << "13, "; i++; continue; }
                       if (str[i+1] == 't') { asmCode << "9, "; i++; continue; }
                    }
                    asmCode << (int)(unsigned char)str[i] << ", ";
                }
                asmCode << "0\n";
            }
        }
    }
}

void NASMVisitor::visit(ImportNode* node) {}
void NASMVisitor::visit(AsmNode* node) { asmCode << node->code() << "\n"; }
void NASMVisitor::visit(BlockNode* node) { for (const auto& stmt : node->statements()) { stmt->accept(this); if (stmt->isExpression()) popType(); } }

void NASMVisitor::visit(IfNode* node) {
    std::string elseL = freshLabel("else"), endL = freshLabel("endif");
    node->cond()->accept(this); popType();
    asmCode << "    cmp rax, 0\n    je " << elseL << "\n";
    node->thenBranch()->accept(this);
    if (node->thenBranch()->isExpression()) popType();
    asmCode << "    jmp " << endL << "\n" << elseL << ":\n";
    if (node->elseBranch()) {
        node->elseBranch()->accept(this);
        if (node->elseBranch()->isExpression()) popType();
    }
    asmCode << endL << ":\n";
}

void NASMVisitor::visit(WhileNode* node) {
    std::string startL = freshLabel("while_start"), endL = freshLabel("while_end");
    asmCode << startL << ":\n";
    node->cond()->accept(this); popType();
    asmCode << "    cmp rax, 0\n    je " << endL << "\n";
    node->body()->accept(this);
    if (node->body()->isExpression()) popType();
    asmCode << "    jmp " << startL << "\n" << endL << ":\n";
}

void NASMVisitor::visit(ReturnNode* node) {
    if (node->value()) { node->value()->accept(this); popType(); }
    else asmCode << "    xor rax, rax\n";
    asmCode << "    jmp " << ctx().returnLabel << "\n";
}

void NASMVisitor::visit(ComptimeNode* node) { node->expr()->accept(this); }
void NASMVisitor::visit(ConstDeclNode* node) {
    int sz = sizeOfType(node->type()), a = alignOfType(node->type());
    ctx().currentStackOffset = alignUp(ctx().currentStackOffset, a);
    ctx().currentStackOffset += sz;
    ctx().stackOffsets[node->name()] = ctx().currentStackOffset;
    ctx().varTypes[node->name()] = node->type();
    if (node->init()) {
        node->init()->accept(this); popType();
        asmCode << "    mov " << ptrSizeQualifier(sz) << " [rbp - " << (32 + ctx().stackOffsets[node->name()]) << "], ";
        if (sz == 1) asmCode << "al\n"; else if (sz == 2) asmCode << "ax\n"; else if (sz == 4) asmCode << "eax\n"; else asmCode << "rax\n";
    }
}

void NASMVisitor::visit(FuncDefNode* node) {
    std::string label = "wl_" + node->name(), retLabel = freshLabel(label + "_ret");
    asmCode << "global " << label << "\n" << label << ":\n";
    beginFunction(retLabel);
    std::stringstream body; auto* saved = swapOut(asmCode, body);
    static const char *r8[]={"rcx","rdx","r8","r9"}, *r4[]={"ecx","edx","r8d","r9d"}, *r2[]={"cx","dx","r8w","r9w"}, *r1[]={"cl","dl","r8b","r9b"};
    struct ParamInfo { std::string name; Type type; int sz; };
    std::vector<ParamInfo> pinfos;
    for (const auto& p : node->params()) {
        int sz = sizeOfType(p.type), a = alignOfType(p.type);
        ctx().currentStackOffset = alignUp(ctx().currentStackOffset, a);
        ctx().currentStackOffset += sz;
        ctx().stackOffsets[p.name] = ctx().currentStackOffset;
        ctx().varTypes[p.name] = p.type;
        pinfos.push_back({p.name, p.type, sz});
    }
    node->body()->accept(this);
    body << retLabel << ":\n"; restoreOut(asmCode, saved);
    int locals = align16(ctx().currentStackOffset);
    asmCode << "    push rbp\n    mov rbp, rsp\n    sub rsp, " << (32 + locals) << "\n";
    for (size_t i = 0; i < pinfos.size(); ++i) {
        int destOff = 32 + ctx().stackOffsets[pinfos[i].name];
        if (i < 4) {
            asmCode << "    mov " << ptrSizeQualifier(pinfos[i].sz) << " [rbp - " << destOff << "], ";
            if (pinfos[i].sz == 1) asmCode << r1[i] << "\n"; else if (pinfos[i].sz == 2) asmCode << r2[i] << "\n"; else if (pinfos[i].sz == 4) asmCode << r4[i] << "\n"; else asmCode << r8[i] << "\n";
        } else {
            asmCode << "    mov rax, qword [rbp + " << (16 + i * 8) << "]\n";
            asmCode << "    mov " << ptrSizeQualifier(pinfos[i].sz) << " [rbp - " << destOff << "], ";
            if (pinfos[i].sz == 1) asmCode << "al\n"; else if (pinfos[i].sz == 2) asmCode << "ax\n"; else if (pinfos[i].sz == 4) asmCode << "eax\n"; else asmCode << "rax\n";
        }
    }
    asmCode << body.str();
    asmCode << "    add rsp, " << (32 + locals) << "\n    pop rbp\n    ret\n";
    endFunction();
}
