#ifndef COMPILER_PELINKER_H
#define COMPILER_PELINKER_H

#include <string>
#include <unordered_map>

// Minimal PE32+ linker for a single x64 COFF object file (NASM -f win64).
// Resolves external symbols via a generated import table (.idata) and emits a runnable PE exe.
//
// imports: map from function symbol name -> DLL name (e.g. "ExitProcess" -> "kernel32.dll")
bool linkCoffObjectToPeExe(const std::string& objPath,
                           const std::string& exePath,
                           const std::string& entrySymbol,
                           const std::unordered_map<std::string, std::string>& imports,
                           std::string* errorOut);

#endif // COMPILER_PELINKER_H

