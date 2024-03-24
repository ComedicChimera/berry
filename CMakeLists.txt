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
    "ast.cpp"
    "driver.cpp"
    "loader.cpp"
    "linker.cpp"
       
    "syntax/token.cpp"
    "syntax/lexer.cpp" 
    "syntax/parser.cpp" 
    "syntax/enum_alloc.cpp"
    "syntax/parse_def.cpp"
    "syntax/parse_type.cpp"
    "syntax/parse_stmt.cpp"
    "syntax/parse_expr.cpp"
    "syntax/parse_import.cpp"
    "syntax/parse_pattern.cpp"

    "types/type_to_str.cpp"
    "types/type_cmp.cpp" 
    "types/untyped.cpp"
    "types/prim_type.cpp"
    "types/type_alloc.cpp"

    "check/checker.cpp"
    "check/check_def.cpp"
    "check/check_stmt.cpp"
    "check/check_expr.cpp"
    "check/check_op.cpp"

    "codegen/codegen.cpp"
    "codegen/gen_def.cpp"
    "codegen/gen_expr.cpp"
    "codegen/gen_stmt.cpp"
    "codegen/gen_debug.cpp"
    "codegen/gen_import.cpp"
    "codegen/gen_atom.cpp"
    "codegen/gen_main.cpp"
    "codegen/gen_comptime.cpp"

    "test/arena_test.cpp"
    "test/ast_print.cpp"
)
list(TRANSFORM SRCS PREPEND ${SRC_DIR})

add_executable(${PROJECT_NAME} ${SRCS})

target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR})

set(INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/")
target_include_directories(${PROJECT_NAME} PRIVATE ${INC_DIR})

# LLVM Configuration
if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4624")
endif()

if (NOT DEFINED LLVM_CONFIG_PATH)
    set(LLVM_CONFIG_PATH "D:/DevTools/llvm-17.0.6/llvm/out/Debug/bin/llvm-config.exe")
endif()

message(LLVM_CONFIG_PATH="${LLVM_CONFIG_PATH}")

execute_process(COMMAND ${LLVM_CONFIG_PATH} "--includedir" OUTPUT_VARIABLE LLVM_INC_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG_PATH} "--libdir" OUTPUT_VARIABLE LLVM_LIB_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND ${LLVM_CONFIG_PATH} "--libs" "core" "native" "--system-libs" OUTPUT_VARIABLE LLVM_LIBS_RAW OUTPUT_STRIP_TRAILING_WHITESPACE)
separate_arguments(LLVM_LIBS NATIVE_COMMAND ${LLVM_LIBS_RAW})

execute_process(COMMAND ${LLVM_CONFIG_PATH} "--obj-root" OUTPUT_VARIABLE LLVM_INC_PATH2 OUTPUT_STRIP_TRAILING_WHITESPACE)
cmake_path(APPEND LLVM_INC_PATH2 "include")

if (NOT DEFINED LIBXML2_PATH)
    set(LIBXML2_PATH "D:/DevTools/libxml2/out/Release")
endif()

if (NOT DEFINED ZLIB_PATH)
    set(ZLIB_PATH "D:/DevTools/zlib/out/Release")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${LLVM_INC_PATH} ${LLVM_INC_PATH2})
target_link_directories(${PROJECT_NAME} PRIVATE ${LLVM_LIB_PATH} ${LIBXML2_PATH} ${ZLIB_PATH})
target_link_libraries(${PROJECT_NAME} PRIVATE ${LLVM_LIBS})
