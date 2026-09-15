cmake_minimum_required(VERSION 3.20)
project(lumen_c VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(BISON 3.8 REQUIRED)
find_package(FLEX 2.6 REQUIRED)
find_package(LLVM CONFIG QUIET)

if(LLVM_FOUND)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
    set(LUMEN_CODEGEN_SOURCE src/codegen.cpp)
else()
    message(STATUS "LLVM development package not found; using the textual LLVM IR backend")
    set(LUMEN_CODEGEN_SOURCE src/text_codegen.cpp)
endif()

BISON_TARGET(
    LumenParser
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser.y
    ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp
    DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/parser.hpp
)
FLEX_TARGET(
    LumenLexer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lexer.l
    ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp
)
ADD_FLEX_BISON_DEPENDENCY(LumenLexer LumenParser)

add_executable(lumenc
    src/main.cpp
    src/constant_fold.cpp
    src/semantic.cpp
    ${LUMEN_CODEGEN_SOURCE}
    ${BISON_LumenParser_OUTPUTS}
    ${FLEX_LumenLexer_OUTPUTS}
)

target_include_directories(lumenc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(LLVM_FOUND)
    target_include_directories(lumenc SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(lumenc PRIVATE ${LLVM_DEFINITIONS})
    llvm_map_components_to_libnames(LLVM_LIBS core support passes nativecodegen native)
    target_link_libraries(lumenc PRIVATE ${LLVM_LIBS})
endif()

if(MSVC)
    target_compile_options(lumenc PRIVATE /W4)
else()
    target_compile_options(lumenc PRIVATE -Wall -Wextra -Wpedantic)
    set_source_files_properties(
        ${FLEX_LumenLexer_OUTPUTS}
        PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare"
    )
endif()

include(CTest)
if(BUILD_TESTING)
    find_program(CLANG_EXECUTABLE NAMES clang REQUIRED)
    add_executable(type_semantic_test
        tests/type_semantic_test.cpp
        src/constant_fold.cpp
        src/semantic.cpp
    )
    target_include_directories(type_semantic_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(MSVC)
        target_compile_options(type_semantic_test PRIVATE /W4)
    else()
        target_compile_options(type_semantic_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME type_semantic COMMAND type_semantic_test)

    add_test(
        NAME compiler_e2e
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e.sh
                $<TARGET_FILE:lumenc> ${CLANG_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests
                ${CMAKE_CURRENT_BINARY_DIR}/test-output
    )
    if(LLVM_FOUND)
        add_test(
            NAME llvm_backend
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/llvm_backend.sh
                    $<TARGET_FILE:lumenc> ${CLANG_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/program.lm
                    ${CMAKE_CURRENT_BINARY_DIR}/llvm-backend-output
        )
    else()
        add_test(
            NAME fallback_backend_limits
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/fallback_backend.sh
                    $<TARGET_FILE:lumenc> ${CMAKE_CURRENT_SOURCE_DIR}/tests/program.lm
                    ${CMAKE_CURRENT_BINARY_DIR}/fallback-output
        )
    endif()
endif()
