#ifndef COMPILER_CODEGEN_H
#define COMPILER_CODEGEN_H

#include "AST.h"
#include <sstream>
#include <unordered_map>
#include <set>
#include <vector>

class NASMVisitor : public ASTVisitor {
public:
    std::stringstream asmCode;
    std::unordered_map<std::string, std::string> imports;
    struct FunctionInfo {
        std::string name;
        Type retType;
    };
    std::unordered_map<std::string, FunctionInfo> userFunctions;
    bool emitLibraryMode = false;

    // Type stack for expression results (replaces fragile lastType)
    void pushType(Type t);
    Type popType();
    std::vector<Type> typeStack;

    // String literal table: literal -> labels
    std::unordered_map<std::string, std::vector<std::string>> strings;

    struct FuncContext {
        std::unordered_map<std::string, int> stackOffsets;
        std::unordered_map<std::string, std::string> varToStruct;
        std::unordered_map<std::string, Type> varTypes;
        int currentStackOffset = 0;
        std::string returnLabel;
    };

    std::vector<FuncContext> ctxStack;
    int labelCounter = 0;

    void visit(NumberNode* node) override;
    void visit(BinaryOpNode* node) override;
    void visit(AssignNode* node) override;
    void visit(VarDeclNode* node) override;
    void visit(VarRefNode* node) override;
    void visit(ImportNode* node) override;
    void visit(AsmNode* node) override;
    void visit(ForeignNode* node) override;
    void visit(AddressOfNode* node) override;
    void visit(DereferenceNode* node) override;
    void visit(CastNode* node) override;
    void visit(CallNode* node) override;
    void visit(IndexNode* node) override;
    void visit(StringLiteralNode* node) override;
    void visit(ProgramNode* node) override;
    void visit(StructDefNode* node) override;
    void visit(MemberAccessNode* node) override;
    void visit(BlockNode* node) override;
    void visit(IfNode* node) override;
    void visit(WhileNode* node) override;
    void visit(ReturnNode* node) override;
    void visit(FuncDefNode* node) override;
    void visit(ComptimeNode* node) override;
    void visit(ConstDeclNode* node) override;

    // Helper to allocate a temporary stack slot and store RAX into it.
    // Returns the offset from RBP (excluding the 32-byte shadow space).
    int pushTmp();
    // Helper to load from a temporary slot into a register.
    void popTmp(int slotOff, const std::string& reg = "rax");
    // Helper to reclaim a slot (just decrements the counter for now).
    void freeTmp();

private:
    FuncContext& ctx();
    const FuncContext& ctx() const;
    std::string freshLabel(const std::string& prefix);
    void beginFunction(const std::string& retLabel);
    void endFunction();
};

#endif // COMPILER_CODEGEN_H
