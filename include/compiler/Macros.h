#ifndef COMPILER_MACROS_H
#define COMPILER_MACROS_H

#include "compiler/AST.h"
#include <vector>

// A minimal Rust-like token macro expander.
// Supports:
//   macro_rules! name { ( $x ) => { ... $x ... }; ... }
// and invocation:
//   name!( ... )
//
// Expansion runs on the token stream before parsing.
std::vector<Token> expandMacros(const std::vector<Token>& tokens);

#endif // COMPILER_MACROS_H

