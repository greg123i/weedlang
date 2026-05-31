#ifndef COMPILER_CONSTANT_EVALUATOR_H
#define COMPILER_CONSTANT_EVALUATOR_H

#include "compiler/AST.h"
#include <memory>
#include <string>
#include <vector>

// Lowers compile-time constructs before codegen.
// - Folds pure comptime expressions into literals.
// - Carries comptime bindings forward so later code can reuse them.
// - Erases comptime-only blocks/statements from the runtime AST.
//
// Diagnostics are returned as formatted strings.
std::unique_ptr<ASTNode> lowerComptime(std::unique_ptr<ASTNode> root,
                                        std::vector<std::string>* diagnostics = nullptr);

#endif // COMPILER_CONSTANT_EVALUATOR_H
