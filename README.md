# Lumen C

一个刻意保持小而完整的编译器：前端由 **Flex + Bison** 实现，后端使用
**LLVM IR**。它不是表达式 demo；源码会经过词法分析、语法分析、AST、作用域与
语义检查、LLVM 验证，最终生成可交给 `clang` 链接的 `.ll` 文件。

## 已支持的语言

- 唯一数据类型 `int`（32 位有符号整数）
- 函数定义、递归、前向调用和 `extern` 函数声明
- 局部变量、初始化、赋值及词法作用域
- `+ - * / %`、比较、`!`，以及真正短路求值的 `&&` / `||`
- `if` / `else`、`while`、代码块和 `return`
- `//` 与 `/* ... */` 注释
- 带源码行号的语法和语义诊断

每个函数都必须显式 `return`。目前没有全局变量、指针、数组、浮点数和用户自定义
类型；这些边界是语言设计的一部分，而不是偷偷按 C 语义处理。

## 构建

依赖 CMake 3.20+、C++17 编译器、Flex 2.6+、Bison 3.8+ 和能够处理 LLVM IR
的 `clang`。如果能找到 `LLVMConfig.cmake`，构建会自动使用官方 LLVM C++ API；
否则使用内置的文本 IR 生成器，输出仍交给 LLVM/Clang 验证和链接。

macOS（Homebrew）：

```bash
brew install cmake flex bison llvm
cmake -S . -B build -DLLVM_DIR="$(brew --prefix llvm)/lib/cmake/llvm"
cmake --build build -j
ctest --test-dir build --output-on-failure
```

本机已有的 `/usr/local/llvm` 会被 CMake 自动发现；其他非标准安装位置可通过
`-DLLVM_DIR=/path/to/lib/cmake/llvm` 指定。也可以用
`-DCMAKE_DISABLE_FIND_PACKAGE_LLVM=TRUE` 显式测试文本 IR 后端。

## 使用

```bash
./build/lumenc examples/fibonacci.lm -o fibonacci.ll
clang fibonacci.ll -o fibonacci
./fibonacci
```

省略 `-o` 时 LLVM IR 写到标准输出：

```bash
./build/lumenc examples/fibonacci.lm | less
```

示例程序：

```c
extern int putchar(int c);

int square(int x) {
    return x * x;
}

int main() {
    int n = 6;
    if (n > 0) {
        return square(n);
    }
    return 0;
}
```

## 结构

```text
src/lexer.l       Flex 词法规则
src/parser.y      Bison 文法与 AST 构造
include/ast.hpp   所有权明确的 AST
src/codegen.cpp   语义检查与 LLVM IR 生成
src/main.cpp      命令行驱动
tests/e2e.sh      源码 → IR → 原生程序的端到端测试
```

进一步阅读：[架构设计](docs/architecture.md) ·
[详细设计](docs/detailed-design.md)

排查 LLVM pass 引入的问题时，可以对后续 `opt`/`clang` 流程使用
`-mllvm -print-changed`；本项目也会在输出前运行 LLVM verifier，尽量把坏 IR
截在编译器内部。
