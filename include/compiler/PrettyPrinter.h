#ifndef COMPILER_PRETTY_PRINTER_H
#define COMPILER_PRETTY_PRINTER_H

#include "compiler/AST.h"
#include <string>

std::string prettyPrintAst(const ASTNode* node);

#endif // COMPILER_PRETTY_PRINTER_H
