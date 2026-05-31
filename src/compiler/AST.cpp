#include "compiler/AST.h"
#include <algorithm>

int Type::size(const std::function<int(const std::string&)>& resolver) const {
    if (pointerLevel > 0) return 8;
    int baseSize = 8;
    switch (baseType) {
        case DataType::I8:
        case DataType::U8: baseSize = 1; break;
        case DataType::I16:
        case DataType::U16: baseSize = 2; break;
        case DataType::I32:
        case DataType::U32: baseSize = 4; break;
        case DataType::I64:
        case DataType::U64: baseSize = 8; break;
        case DataType::VOID: baseSize = 0; break;
        case DataType::STRUCT_TYPE:
            if (resolver) baseSize = resolver(structName);
            else baseSize = 0;
            break;
        case DataType::PTR: baseSize = 8; break;
        default: baseSize = 8; break;
    }
    if (arraySize > 0) return baseSize * arraySize;
    return baseSize;
}

int Type::alignment(const std::function<int(const std::string&)>& resolver) const {
    if (pointerLevel > 0) return 8;
    int sz = size(resolver);
    if (sz <= 0) return 1;
    if (sz >= 8) return 8;
    return sz;
}
