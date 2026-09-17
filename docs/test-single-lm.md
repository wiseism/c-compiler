# 使用可执行程序测试单个 `.lm` 文件

## 编译为 LLVM IR

```bash
./cmake-build-debug/lumenc tests/program.lm -o /tmp/program.ll
```

## 使用 Clang 生成可执行程序

```bash
clang /tmp/program.ll -o /tmp/program
```

## 运行程序并查看退出码

```bash
/tmp/program
echo $?
```

例如测试 `examples/fibonacci.lm`：

```bash
./cmake-build-debug/lumenc examples/fibonacci.lm -o /tmp/fibonacci.ll
clang /tmp/fibonacci.ll -o /tmp/fibonacci
/tmp/fibonacci
echo $?
```

## 只检查 `.lm` 是否能编译

```bash
./cmake-build-debug/lumenc tests/program.lm -o /tmp/program.ll
```

## 测试预期会失败的文件

错误样例应该返回非零状态，并输出诊断信息：

```bash
./cmake-build-debug/lumenc tests/syntax_error.lm -o /tmp/error.ll
echo $?
```

可以。这个 `.ll` 文件是合法的 LLVM IR，用 `clang` 可以直接编译成可执行文件。

## 基本命令

```bash
clang program.ll -o program
./program
```

## 说明

- `clang` 会自动识别 `.ll` 后缀并走 LLVM IR 路径，不需要额外参数。
- 它内部会调用 LLVM 把 IR 降级、优化、生成目标代码，再链接成可执行文件。
- 文件里只 `declare` 了 `putchar`，这是标准 C 库函数，链接时由系统 libc 提供，**不需要额外链接任何库**。

## 如果你想看到中间步骤

```bash
# 1. 只生成目标文件
clang -c program.ll -o program.o

# 2. 再链接
clang program.o -o program

# 或者一步到位
clang program.ll -o program
```

## 可能遇到的问题

1. **`putchar` 声明不完整**
   LLVM IR 里写的是 `declare i32 @putchar(i32)`，而 C 标准库的 `putchar` 签名是 `int putchar(int)`，类型是一致的，没问题。

2. **如果报 `undefined symbol: putchar`**
   说明链接阶段没找到 libc，一般不会发生。若真遇到，可以显式指定：
   ```bash
   clang program.ll -o program -lc
   ```

3. **优化（可选）**
   ```bash
   clang -O2 program.ll -o program
   ```

## 预期运行结果

从 `main` 的逻辑看：

- `factorial(4)` = 24
- `operators(5)` = 13（可以自己推一下），`+10` = 23
- `choose(1, 23, 99)` = 23
- `answer = 24 + 23 = 47`

`answer != 42`，所以会先执行 `putchar(88)` 输出 `X`，然后因为 `logic.end` 的 `phi` 取了 `true`（注意这里 `logic.rhs` 里 `putchar` 返回的是写入的字符 `X` 的 ASCII 值 88，非 0），所以 `truth6 = true`，最终进入 `if.then7`，输出 `OK\n`。

**最终输出：`XOK`**
# clang 把 .c / .cc 编译成 .o 和可执行文件

## 一、最简命令

```bash
# .c 文件
clang program.c -o program          # 直接生成可执行文件
clang -c program.c -o program.o     # 只生成目标文件 .o

# .cc / .cpp 文件（C++）
clang++ program.cc -o program       # 直接生成可执行文件
clang++ -c program.cc -o program.o  # 只生成目标文件 .o
```

⚠️ **关键点**：`.cc` / `.cpp` 是 C++ 源文件，必须用 **`clang++`**（而不是 `clang`），否则会因为 C++ 标准库、名字修饰（name mangling）等问题链接失败。

## 二、clang 与 clang++ 的区别

| 命令 | 用途 | 链接的库 |
|------|------|----------|
| `clang` | 编译 C | libc |
| `clang++` | 编译 C++ | libc + libstdc++（或 libc++） |

如果非要用 `clang` 编 C++，也不是不行，但要手动加库，比较麻烦：
```bash
clang -x c++ program.cc -o program -lstdc++
```
一般不推荐，直接用 `clang++` 就好。

## 三、编译流程拆解（四步）

clang 编译一个源文件到可执行文件，实际经历四个阶段：

```
源文件 .c/.cc
   │  ① 预处理
   ▼
预处理文件 .i
   │  ② 编译
   ▼
汇编文件 .s
   │  ③ 汇编
   ▼
目标文件 .o
   │  ④ 链接
   ▼
可执行文件
```

对应命令：

```bash
# ① 预处理
clang -E program.c -o program.i

# ② 编译成汇编
clang -S program.c -o program.s

# ③ 汇编成目标文件
clang -c program.c -o program.o      # 常用

# ④ 链接成可执行文件
clang program.o -o program           # 常用
```

## 四、常用选项一览

| 选项 | 作用 |
|------|------|
| `-c` | 只编译到 `.o`，不链接 |
| `-o <file>` | 指定输出文件名 |
| `-S` | 生成汇编 `.s` |
| `-E` | 只做预处理 |
| `-O0/-O1/-O2/-O3` | 优化级别 |
| `-g` | 生成调试信息 |
| `-Wall -Wextra` | 打开警告 |
| `-std=c11` / `-std=c++17` | 指定语言标准 |
| `-I<dir>` | 添加头文件搜索路径 |
| `-L<dir>` | 添加库搜索路径 |
| `-l<name>` | 链接某个库（如 `-lm` 链数学库） |

## 五、多个源文件怎么编

### 方式 1：分别编译成 .o，再统一链接（推荐，适合大项目）

```bash
clang   -c a.c  -o a.o
clang   -c b.c  -o b.o
clang++ -c c.cc -o c.o
clang++ a.o b.o c.o -o program
```

好处：改一个文件只需重编那一个 `.o`，再重新链接。

### 方式 2：一次性全部编译

```bash
clang++ a.c b.c c.cc -o program
```

简单，但每次全量重编。

## 六、C 和 C++ 混合的注意事项

- **`.c` 用 `clang` 编，`.cc` 用 `clang++` 编**，最后用 `clang++` 链接（因为需要 C++ 运行时）。
- 如果 C++ 要调用 C 的函数，C 头文件里要加 `extern "C"`：

```c
#ifdef __cplusplus
extern "C" {
#endif

int foo(int x);

#ifdef __cplusplus
}
#endif
```

否则会出现 `undefined reference`（找不到符号），因为 C++ 会做名字修饰。

## 七、完整例子

假设有 `main.cc` 和 `util.cc`：

```bash
# 编译
clang++ -c -Wall -O2 main.cc -o main.o
clang++ -c -Wall -O2 util.cc -o util.o

# 链接
clang++ main.o util.o -o app

# 运行
./app
```

或者一步到位：
```bash
clang++ -Wall -O2 main.cc util.cc -o app
```

## 八、和你之前的 .ll 文件对比

| 输入 | 命令 | 结果 |
|------|------|------|
| `program.ll`（LLVM IR） | `clang program.ll -o program` | 可执行文件 |
| `program.c`（C 源码） | `clang program.c -o program` | 可执行文件 |
| `program.cc`（C++ 源码） | `clang++ program.cc -o program` | 可执行文件 |
| `program.c` 只要 .o | `clang -c program.c -o program.o` | 目标文件 |

其实 **`.ll` 就是 clang 编译 `.c` 过程中的一个中间产物**。你完全可以让 clang 把 `.c` 编成 `.ll`：

```bash
clang -S -emit-llvm program.c -o program.ll
```

然后再用前面那步把 `.ll` 编成可执行文件。这条链路和你现在项目里做的事情是一致的。

