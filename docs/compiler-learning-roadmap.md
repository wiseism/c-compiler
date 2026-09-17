# 编译器新手学习路线

这份路线以本项目为实践主线，目标是先读懂现有代码，再逐步具备独立扩展语言和后端的能力。

## 一、从本项目可以学到什么

### 1. 编译器整体流程

项目展示了一条完整的编译链：

```text
.lm 源码 → 词法分析 → 语法分析 → AST → 语义分析 → 常量折叠 → LLVM IR → clang → 可执行程序
```

入口位于 `src/main.cpp`。它负责解析命令行参数、调用前端和后端，并将错误传递给用户。

### 2. 词法分析

`src/lexer.l` 使用 Flex 将字符流切分为 token，例如关键字、标识符、整数、运算符和注释。
这里可以学习正则表达式、最长匹配规则、行列号跟踪以及词法错误处理。

### 3. 语法分析

`src/parser.y` 使用 Bison 根据文法构造 AST。重点包括表达式优先级、函数定义、声明、语句、错误恢复和语法位置记录。

### 4. AST 与 C++ 所有权

`include/ast.hpp` 展示了如何用 C++ 类表达程序结构，以及如何使用 `std::unique_ptr` 管理 AST 节点生命周期。
阅读时应关注节点继承、移动语义、容器和源码位置保存。

### 5. 类型系统与语义分析

`include/type.hpp`、`include/semantic.hpp` 和 `src/semantic.cpp` 负责检查：

- 变量和函数是否声明；
- 作用域和遮蔽关系；
- 函数参数数量与类型；
- `int`、`void`、指针和数组是否正确使用；
- 非 `void` 函数是否所有路径都返回；
- 全局初始化器是否为常量表达式。

这部分体现了“解析成功不等于程序合法”：语法树还必须通过独立的语义检查。

### 6. 常量折叠和中间表示

`src/constant_fold.cpp` 在 typed AST 上计算纯常量表达式，例如 `(2 + 3) * 4`。
它还需要保持短路求值、除零和有符号溢出等语义，适合学习编译器优化的边界条件。

### 7. LLVM IR 后端

`src/codegen.cpp` 使用 LLVM C++ API 生成模块、函数、基本块、指令和调试信息；`src/text_codegen.cpp` 提供文本 IR 后端。
通过比较两套后端，可以理解目标无关的中间表示、控制流图、SSA 风格寄存器和运行时 trap。

### 8. 构建系统和兼容性

`CMakeLists.txt` 展示了 Flex、Bison、LLVM、Clang、CTest 和可选 fuzzing 目标如何组合。
代码还兼容不同 LLVM 版本，这适合学习第三方 API 变化、条件编译和可移植构建。

### 9. 测试与调试方法

`tests/` 包含端到端测试、错误诊断测试、后端差分测试和 fuzz smoke test。
`docs/test-single-lm.md` 说明了如何手工编译和运行单个源文件，`docs/test-all-lm.sh` 可批量检查所有 fixture。

## 二、推荐学习顺序

### 阶段 0：准备 C++ 和工具基础（1 周）

- 掌握 C++17：类、继承、模板、RAII、智能指针、移动语义；
- 熟悉命令行、Git、CMake、编译器参数和调试器；
- 能独立运行 `cmake --build`、`ctest` 和项目脚本。

练习：修改一个诊断文本，重新构建并运行相关测试。

### 阶段 1：读懂前端（1–2 周）

阅读顺序：`src/main.cpp` → `src/lexer.l` → `src/parser.y` → `include/ast.hpp`。

- 画出 token、文法规则和 AST 节点的对应关系；
- 添加一个简单语法，例如一元 `-` 或新的关键字；
- 为新语法补充一个合法和一个非法 fixture。

推荐资料：

- LLVM Kaleidoscope Tutorial：词法、递归下降解析和 AST；
- Bison Manual：文法、优先级和错误恢复；
- Flex Manual：模式、动作和状态机。

### 阶段 2：掌握类型和语义（1–2 周）

阅读 `include/type.hpp`、`src/semantic.cpp` 和相关 `.lm` 错误样例。

- 实现一个新的类型检查规则；
- 理解符号表、作用域栈和函数签名；
- 为每种错误验证行号、列号和错误消息。

推荐资料：

- *Crafting Interpreters*：作用域、解析和静态分析章节；
- *Engineering a Compiler*：类型检查和符号表章节。

### 阶段 3：理解 IR 和代码生成（2–3 周）

阅读 `include/typed_ir.hpp`、`src/codegen.cpp`、`src/text_codegen.cpp`。

- 手工写出简单函数对应的 LLVM IR；
- 观察 `if`、`while`、数组访问和指针解引用生成的基本块；
- 使用 `opt -verify -S` 检查 IR；
- 比较 LLVM API 后端和文本后端的输出与行为。

推荐资料：

- LLVM Language Reference Manual；
- LLVM Programmer’s Manual；
- *SSA-based Compiler Design* 的 SSA 和控制流章节。

### 阶段 4：优化、测试和工具链（1–2 周）

- 研究 `src/constant_fold.cpp` 的优化前后差异；
- 阅读 `tests/e2e.sh`、`tests/differential.sh` 和 CTest 配置；
- 使用 `lldb` 调试编译器；
- 使用 sanitizer 和 fuzzing 寻找崩溃或未定义行为；
- 学习 GitHub Actions 中的跨平台构建矩阵。

推荐资料：

- LLVM Passes 文档；
- Clang Sanitizers 文档；
- libFuzzer 文档；
- *Fuzzing for Software Security Testing and Quality Assurance*。

## 三、建议的项目练习

按难度逐步完成：

1. 增加一个整数运算符，并补齐 lexer、parser、语义检查、两个后端和测试。
2. 增加 `break` 或 `continue`，观察控制流和基本块如何变化。
3. 增加字符串字面量，并调用一个外部 C 函数输出字符串。
4. 增加局部 `const` 变量和更严格的常量表达式检查。
5. 实现一个简单的 IR 优化 pass，并用差分测试验证行为不变。
6. 为一种新的 LLVM 版本增加兼容层，并在 CI 中验证。

每次练习都应包含：实现、正例、反例、端到端运行验证、文档更新和 Git commit。

## 四、每次修改的验证清单

```bash
cmake --build cmake-build-debug --target lumenc -j 6
ctest --test-dir cmake-build-debug --output-on-failure
./docs/test-all-lm.sh ./cmake-build-debug/lumenc
git diff --check
```

如果修改了 IR 生成，还应单独运行一个合法 `.lm` 文件，使用 `clang` 链接并检查退出码或标准输出。

## 五、学习目标

完成以上路线后，应能够：

- 从零设计一个小语言的 token、文法和 AST；
- 实现作用域、类型检查和错误恢复；
- 阅读并修改 LLVM IR 生成代码；
- 为新语言特性编写完整测试；
- 定位编译器崩溃、诊断错误和后端兼容性问题。
