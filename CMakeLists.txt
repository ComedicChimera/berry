cmake_minimum_required(
    VERSION 3.20.0
)

project(berry)

set(CMAKE_CXX_STANDARD 20)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/")
set(SRCS 
    "main.cpp" 
    "report.cpp" 
    "arena.cpp" 
       
    "syntax/token.cpp"
    "syntax/lexer.cpp" 
    "syntax/parser.cpp" 
    "syntax/parse_def.cpp"
    "syntax/parse_type.cpp"
    "syntax/parse_stmt.cpp"
    "syntax/parse_block.cpp"
    "syntax/parse_expr.cpp"

    "types/type_to_str.cpp"
    "types/type_cmp.cpp" 
    "types/untyped.cpp"
    "types/prim_type.cpp"

    "check/checker.cpp"
    "check/check_def.cpp"
    "check/check_block.cpp"
    "check/check_stmt.cpp"
    "check/check_expr.cpp"

    "test/arena_test.cpp"
    "test/ast_print.cpp"
)
list(TRANSFORM SRCS PREPEND ${SRC_DIR})

add_executable(${PROJECT_NAME} ${SRCS})

target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR})

set(INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/")
target_include_directories(${PROJECT_NAME} PRIVATE ${INC_DIR})