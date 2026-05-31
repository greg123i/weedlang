cmake_minimum_required(VERSION 3.10)

project(WeedLang)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)
enable_testing()

add_executable(untitled2
    src/main.cpp
    src/compiler/AST.cpp
    src/compiler/Compiler.cpp
    src/compiler/Lexer.cpp
    src/compiler/ConstantEvaluator.cpp
    src/compiler/Macros.cpp
    src/compiler/Parser.cpp
    src/compiler/Codegen.cpp
    src/compiler/PrettyPrinter.cpp
    src/compiler/PELinker.cpp
)
set_target_properties(untitled2 PROPERTIES OUTPUT_NAME weedc)

add_executable(lexer_test
    tests/unit/LexerTest.cpp
    src/compiler/Lexer.cpp
)
add_test(NAME lexer_test COMMAND lexer_test)

add_executable(parser_test
    tests/unit/ParserTest.cpp
    src/compiler/Lexer.cpp
    src/compiler/Parser.cpp
    src/compiler/AST.cpp
)
add_test(NAME parser_test COMMAND parser_test)

add_executable(codegen_test
    tests/unit/CodegenTest.cpp
    src/compiler/Lexer.cpp
    src/compiler/Parser.cpp
    src/compiler/Codegen.cpp
    src/compiler/AST.cpp
)
add_test(NAME codegen_test COMMAND codegen_test)

add_executable(macro_test
    tests/unit/MacroTest.cpp
    src/compiler/Lexer.cpp
    src/compiler/Macros.cpp
)
add_test(NAME macro_test COMMAND macro_test)

add_executable(comptime_test
    tests/unit/ComptimeTest.cpp
    src/compiler/Lexer.cpp
    src/compiler/Parser.cpp
    src/compiler/ConstantEvaluator.cpp
    src/compiler/AST.cpp
)
add_test(NAME comptime_test COMMAND comptime_test)

set(WEED_INTEROP_DIR "${CMAKE_CURRENT_BINARY_DIR}/integration")
set(WEED_INTEROP_OBJ "${WEED_INTEROP_DIR}/weed_add.obj")

add_custom_command(
    OUTPUT "${WEED_INTEROP_OBJ}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEED_INTEROP_DIR}"
    COMMAND $<TARGET_FILE:untitled2> --emit-obj -o "${WEED_INTEROP_DIR}/weed_add.exe" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/weed_add.weed"
    DEPENDS untitled2 "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/weed_add.weed"
    VERBATIM
)

add_executable(weed_obj_interop_test
    tests/integration/WeedObjInteropTest.cpp
    "${WEED_INTEROP_OBJ}"
)
add_test(NAME weed_obj_interop_test COMMAND weed_obj_interop_test)
