# Lumen C 详细设计

## 1. 源语言定义

### 1.1 词法元素

标识符满足 `[A-Za-z_][A-Za-z0-9_]*`，整数文字由十进制数字组成。保留字为：

```text
int void extern if else while return null
```

`null` 是空指针字面量。双字符运算符 `== != <= >= && ||` 必须先于单字符规则匹配。
其余标点和运算符为：

```text
( ) { } [ ] ; , = + - * / % ! & < >
```

空白被忽略；换行由 Flex 的 `yylineno` 计数。支持 `//` 行注释和 `/* ... */` 块注释。
scanner 使用独立 `COMMENT` start condition，避免注释内容被普通 token 规则解释。

### 1.2 语法摘要

下面的 EBNF 是 `src/parser.y` 的可读表示；实际冲突处理和语义动作以 Bison 文件为准。

```ebnf
translation-unit ::= { external-declaration } ;
external-declaration ::= function-definition | function-declaration | global-declaration ;
base-type ::= "int" | "void" ;
type ::= base-type { "*" } ;
function-definition ::= type identifier "(" [ parameters | "void" ] ")" block ;
function-declaration ::= [ "extern" ] type identifier
                         "(" [ parameters | "void" ] ")" ";" ;
global-declaration ::= [ "extern" ] type identifier
                       ( [ "=" expression ] | "[" integer "]" ) ";" ;
parameters ::= type identifier { "," type identifier } ;

block ::= "{" { statement } "}" ;
statement ::= block
            | type identifier ( [ "=" expression ] | "[" integer "]" ) ";"
            | "return" [ expression ] ";"
            | "if" "(" expression ")" statement [ "else" statement ]
            | "while" "(" expression ")" statement
            | expression ";" ;

expression ::= assignment ;
assignment ::= logical-or | unary "=" assignment ;
logical-or ::= logical-and { "||" logical-and } ;
logical-and ::= equality { "&&" equality } ;
equality ::= relational { ("==" | "!=") relational } ;
relational ::= additive { ("<" | ">" | "<=" | ">=") additive } ;
additive ::= multiplicative { ("+" | "-") multiplicative } ;
multiplicative ::= unary { ("*" | "/" | "%") unary } ;
unary ::= postfix | ("-" | "!" | "&" | "*") unary ;
postfix ::= primary { "[" expression "]" } ;
primary ::= integer | "null"
          | identifier
          | identifier "(" [ arguments ] ")"
          | "(" expression ")" ;
arguments ::= assignment { "," assignment } ;
```

赋值为右结合，其他二元运算符为左结合。`LOWER_THAN_ELSE` 与 `ELSE` precedence 保证
`else` 绑定到最近的未匹配 `if`。

### 1.3 类型和运行时语义

- 标量对象可以是有符号 32 位整数或指针；`void` 只允许作为函数返回类型或指针基类型。
- `0` 为假，任何非零值为真；逻辑及比较表达式的结果规范化为 `0` 或 `1`。
- 未提供初始化器的局部变量初始化为 `0`。
- 加减乘按 LLVM `i32` 自然截断。
- 除零、求余零及有符号除法溢出沿用 LLVM IR 的未定义行为。
- `&&` 与 `||` 从左到右求值并短路；其他二元操作数按左到右生成。
- 非 `void` 函数必须在所有生成路径上显式 `return`；`void` 函数可自然结束。
- 全局 initializer 必须能折叠为整数常量或类型正确的 `null`；不执行隐式运行时初始化。
- `null` 只能在具体指针上下文中使用，不能转换为整数。
- 固定数组是不可赋值的地址对象，不隐式退化；元素默认零初始化。
- 数组下标在运行时检查上下界；编译期可知的越界直接拒绝编译。
- 每次解引用和指针下标都检查空指针，失败执行 `llvm.trap`。
- 裸指针没有运行时 extent；对非空指针的越界索引属于未定义行为。

## 2. AST 数据模型

所有 AST 节点继承 `Node`，共享 `line` 字段和虚析构函数。`Expr` 额外持有由 semantic
pass 写入的 `inferredType`；变量、参数和函数均直接持有显式声明类型。节点族如下：

```text
Node
├── Expr
│   ├── NumberExpr
│   ├── NullExpr
│   ├── VariableExpr
│   ├── AssignExpr
│   ├── UnaryExpr
│   ├── BinaryExpr
│   ├── CallExpr
│   └── IndexExpr
├── Stmt
│   ├── BlockStmt
│   ├── ExprStmt
│   ├── VarDeclStmt
│   ├── ReturnStmt
│   ├── IfStmt
│   └── WhileStmt
└── TopLevel
    ├── Prototype
    ├── GlobalVar
    └── Function
```

`Program` 拥有 `vector<unique_ptr<TopLevel>>`。所有复合节点使用 `unique_ptr` 或
`vector<unique_ptr<T>>` 拥有子节点，禁止共享或循环所有权。后端只接收
`const Program&`，不得改变 AST。

### 2.1 Bison 边界上的所有权

Bison `%union` 使用裸指针，因为传统 C skeleton 不能直接安全承载非平凡 C++ 类型。
规则动作遵循以下协议：

1. scanner 为标识符创建 `new std::string`。
2. parser action 读取后立刻 `std::move` 内容并 `delete` 字符串。
3. 临时列表保存裸节点指针。
4. `adopt()` 将整个列表一次性迁移为 `vector<unique_ptr<T>>` 并删除容器。
5. 解析异常退出时，按语义类型注册的 `%destructor` 删除尚未迁移的对象和列表元素。

这套协议确保正常归约和错误弹栈两条路径都只有一个 owner。

### 2.2 显式类型对象

`Type` 是不可变对象，当前建模 `Int32`、`Void`、`Pointer`、`Array` 和推断期使用的
`NullPointer`。基础类型为
进程内 canonical shared object，复合类型递归持有 element type。`equals()` 做结构
等价比较，`toString()` 为诊断提供稳定的人类可读形式。LLVM 8 typed-pointer 模式没有
`void*`，lowering 将语义层 `void*` 映射成 ABI 等价的 `i8*`。

## 3. 驱动详细流程

`main` 按以下顺序执行：

1. 解析 `--help`、`--version`、`-o`、输出种类、优化等级和唯一输入路径。
2. 使用 `fopen` 打开输入，设置 `yyin`，重置 `yylineno` 和 scanner buffer。
3. 调用 `yyparse()`；全局 `parsedProgram` 只在完整 translation unit 归约后赋值。
4. 关闭输入文件；解析失败立即返回。
5. 运行独立 `SemanticAnalyzer`，完成名字/类型/返回路径检查并标注表达式类型。
6. 运行 `ConstantFolder`，折叠纯常量子树并验证全局 initializer。
7. 以输入文件名作为 LLVM module name 创建 `CodeGenerator`。
8. 调用 `generate()`；错误通过 `error()` 返回，并统一加 `error:` 前缀。
9. 根据 `--emit-llvm`/`-c` 调用 `writeIR()`、`printIR()` 或 `writeObject()`。

编译器一次进程只处理一个 translation unit，因此 scanner/parser 的进程级状态不会跨
文件复用。

## 4. Semantic Analysis Pass

semantic pass 在解析完成、任何 LLVM 状态建立之前运行。第一遍登记函数返回类型和完整
参数类型序列；第二遍以递归 visitor 检查函数体。表达式 visitor 返回 `TypePtr` 并同时
写入节点的 `inferredType`。声明 initializer、赋值、调用实参、条件和 return 都执行
显式类型等价检查。函数体 visitor 还计算保守的 `definitelyReturns` 属性：只有直接
`return` 或 then/else 均确定返回的 `if` 才能证明返回；`while` 暂不因常量条件被视为
确定返回。

### 4.1 共享 TypedProgram

分析成功时，semantic pass 还构造零拷贝 high-level typed IR：

- `TypedSymbol` 为全局、参数和局部提供全 translation unit 唯一的 `SymbolId`；
- `TypedFunction` 合并所有兼容原型，保存唯一 `FunctionId`、完整签名、可选定义和参数
  symbol ID；
- `TypedGlobal` 合并 extern 声明与唯一变量定义；
- `variableBindings` 将每个 `VariableExpr*` 解析到 `SymbolId`；
- `callBindings` 将每个 `CallExpr*` 解析到 `FunctionId`；
- `localDeclarations` 将声明节点解析到其存储 symbol。

AST 继续承载源码结构和所有权，typed IR 只保存不可歧义的语义事实，因此不会复制大型
语法树。`CodeGenerator::generate` 仅接受 `const TypedProgram&`，从类型系统上阻止未做
语义分析的 AST 直接进入后端。

## 5. 名字与作用域

### 5.1 函数符号

函数表记录返回类型和完整参数类型序列。顶层第一遍执行：

- 新名字：创建签名；
- 同名同参数数量的重复 `extern`：允许；
- 同名不同参数数量：报 conflicting declaration；
- 第二个函数体：报 redefinition。

普通原型与 `extern` 原型都只声明签名；若同一 translation unit 存在兼容函数体，IR
只输出定义。函数和全局变量共享顶层名字空间。

全部签名在第二遍函数体生成前可见，所以函数可以递归、互相调用或调用后定义函数。

### 5.2 局部符号

作用域表示为从外到内排列的 map 栈。查询从栈尾向栈首进行，得到最近声明。规则如下：

- 函数参数进入函数最外层作用域；
- `{}` 创建和退出一个作用域；
- `if`、`else`、`while` 的单语句 body 也创建隐式作用域；
- 当前 map 已有同名变量时拒绝重声明；
- 内层 map 可声明同名变量并遮蔽外层 slot；
- initializer 在新名字加入 map 前生成，因此 `int x = x;` 引用外层 `x`，没有外层时
  报错。

查找局部作用域失败后继续查全局变量表，因此局部变量自然遮蔽同名全局。全局可有多个
兼容 `extern` 声明，但只能有一个非 extern 定义。LLVM API 后端的符号值是
`Value*`，文本后端的符号值是 `%vN` 或 `@global` 名字。

## 6. LLVM 类型和存储设计

语言类型到 LLVM 的映射如下：

| 语言概念 | LLVM 表示 |
| --- | --- |
| `int` 值 | `i32` |
| `void` 函数返回 | `void` |
| `T*` | LLVM typed pointer；`void*` 降为 `i8*` |
| `T[N]` | `[N x T]` |
| 条件中间值 | `i1` |
| 局部变量/参数 slot | `alloca T` |
| 函数 | `result-type (parameter-types...)` |
| 全局变量 | `@name = global T value` / `external global T` |

局部变量采用 alloca/load/store 形式，而不是在前端自行维护 SSA。LLVM API 后端把
alloca 插到 entry block；文本后端将 alloca 单独缓冲，最终同样放在 entry 顶部。
后续 LLVM `mem2reg`/优化流水线可以将其提升为 SSA 寄存器。

参数在 entry 中复制到 slot，使参数和局部变量共享完全相同的读取、赋值和遮蔽逻辑。
局部及全局数组用 aggregate zero 初始化。数组不是一等 SSA 值，只能通过下标形成元素
lvalue；指针和数组元素 assignment 统一走“生成地址 → 生成 RHS → store”。

### 6.1 内存安全检查

数组元素地址使用 `getelementptr inbounds [N x T], ..., 0, index`。在 GEP 前生成
`index >= 0 && index < N`，失败分支调用 `llvm.trap` 后 `unreachable`。指针解引用和
指针 GEP 前生成 `pointer != null` 检查并使用相同 trap CFG。这样空指针和已知 extent
数组越界具有确定的进程终止语义；普通 `T*` 不附带 extent，非空越界仍遵循 LLVM
inbounds GEP 的未定义行为。

### 6.2 常量折叠

常量折叠在 typed AST 上递归运行。`+ - *` 使用 32 位无符号位模式计算，避免宿主 C++
有符号溢出；`/ %` 在除零或 `INT32_MIN / -1` 时保留原表达式。比较和逻辑结果归一为
0/1。`0 && rhs` 与非零 `lhs || rhs` 直接替换为常量且不要求访问 RHS，因此保持语言
短路副作用。赋值与调用节点本身绝不折叠，只递归处理其子表达式。

## 7. 表达式生成

### 7.1 基础表达式

| AST | LLVM IR 形态 |
| --- | --- |
| 数字 | `i32` 常量 |
| 变量 | 从对象地址 `load T` |
| 赋值 | 生成目标 lvalue 地址和 RHS，`store`，并返回 RHS 值 |
| 负号 | `sub i32 0, value` / `CreateNeg` |
| 逻辑非 | `icmp eq value, 0` 后 `zext i1 to i32` |
| 函数调用 | 校验函数存在及 arity 后 `call i32` |
| 取址/解引用 | lvalue 地址 / 空检查后 `load` |
| 下标 | 数组 bounds check 或指针 null check 后 GEP + load |

返回类型为 `void` 的调用生成 `call void`，仅允许作为 expression statement；semantic
pass 会阻止它进入赋值、算术、条件或有值 return。

### 7.2 二元运算

算术映射为 `add/sub/mul/sdiv/srem`。比较映射为有符号 `icmp`：

```text
== → eq    != → ne
<  → slt   >  → sgt
<= → sle   >= → sge
```

比较得到 `i1`，立即 `zext` 为语言层 `i32`。

### 7.3 短路逻辑

`lhs && rhs` 的控制流：

```text
lhs-block:
    lhs.bool = lhs != 0
    br lhs.bool, rhs-block, merge-block
rhs-block:
    rhs.bool = rhs != 0
    br merge-block
merge-block:
    result.i1 = phi [false, lhs-block], [rhs.bool, rhs-block]
    result.i32 = zext result.i1
```

`||` 交换 lhs 为真时的分支，并让 PHI 的短路常量为 `true`。记录“完成 RHS 生成后的
当前基本块”而不是假定它仍是最初 rhs block，因为嵌套逻辑表达式会在 RHS 内继续拆分
基本块。

## 8. 语句与控制流生成

### 8.1 代码块和终止状态

后端跟踪当前 insertion block 是否已经存在 terminator。遇到 `return` 或无后继的
结构后，跳过同一 block 中的后续不可达语句，防止在 terminator 后插入指令。

### 8.2 if/else

生成 `if.then`、可选 `if.else` 和 `if.end`。每个仍可下落的分支补一条到 merge block
的无条件跳转。当 then/else 都终止时，merge block 以 `unreachable` 终止，使 IR 保持
结构完整，同时将后续语句标记为不可达。

### 8.3 while

生成三个 block：

```text
current → while.cond
while.cond → while.body | while.end
while.body → while.cond       # body 未 return 时
```

生成完循环后 insertion point 位于 `while.end`，因为当前静态分析不证明常量条件导致
循环永不退出。

### 8.4 return

有值返回先生成表达式，再创建 `ret i32`；空返回创建 `ret void`。二者都会标记 block
terminated。非 void 函数结束时当前 block 仍未终止则报告 missing return；void 函数
自动补 `ret void`。LLVM API 后端随后调用 `verifyFunction`。

## 9. 双后端一致性

`include/codegen.hpp` 使用 PImpl 隔离 LLVM 头文件，两个实现提供完全相同的方法：

```cpp
bool generate(const TypedProgram&);
bool writeIR(const std::string&) const;
void printIR() const;
const std::string& error() const;
```

API 后端直接持有 `LLVMContext`、`Module` 和 `IRBuilder`；文本后端持有临时值计数器、
标签计数器及输出 stream。二者都以 typed IR 的 symbol/call binding 查找实体，不维护
字符串作用域或重复检查签名。CMake 在配置时只选择一个实现源文件。

两份实现仍分别负责 LLVM 指令序列化，因此任何 lowering 扩展必须让 CI 在两个后端运行
相同测试；语义规则和诊断则只有共享 semantic pass 一个事实来源。

### 9.1 优化流水线与目标机

LLVM API 后端在初始 module verifier 成功后，根据 `-O1/-O2/-O3` 将用户等级映射到
LLVM `OptimizationLevel`，注册 Loop/Function/CGSCC/Module analysis manager，通过
`crossRegisterProxies` 建立依赖，再运行 `buildPerModuleDefaultPipeline`。优化后再次运行
module verifier，防止 pass 前提或自定义 IR 缺陷被误认为成功。

需要优化或对象输出时初始化 native target/asm printer，以宿主 triple 创建
`TargetMachine`，并把 triple 与 data layout 写入 module。`writeObject()` 使用 legacy
codegen pass manager 调用 `addPassesToEmitFile(ObjectFile)`；这里的 legacy manager 是
LLVM 目标代码生成接口要求，不影响中端使用新 PassBuilder。

文本 IR 后端没有 LLVM library 可调用，因此只支持 `-O0 --emit-llvm`。对 `-O1...O3`
和 `-c` 明确失败，禁止静默忽略用户请求。

### 9.2 源位置、诊断恢复与调试信息

Flex 的 `YY_USER_ACTION` 同时填写 Bison `YYLTYPE` 的 line/column；换行规则重置列计数。
parser 把产生式关键 token 的起始位置复制到 AST，语义诊断因而采用稳定的
`line:column` 格式。文法在 `error ';'` 处同步：函数体内插入不会进入后端的空占位
statement，顶层则丢弃错误 declaration。驱动检查诊断集合，任何已恢复错误仍使编译失败。

semantic analyzer 不再只保存第一条错误。顶层登记和 block statement 循环分别构成安全
恢复边界；失败节点跳过依赖它的后续分析，其余节点继续产生诊断。只有诊断集合为空时，
typed IR 才交给 constant folder 与后端。

`-g` 启用 LLVM `DIBuilder`。每个定义创建 `DISubprogram`，参数和局部 alloca 关联
`llvm.dbg.declare`，IRBuilder 在表达式/语句入口设置来自 AST 的 `DILocation`。所有函数
完成后统一 `finalize()` metadata，再执行 module verifier；对象生成阶段由 TargetMachine
写出 DWARF。fallback 对 `-g` 返回能力诊断。

### 9.3 Pass 快照与故障最小化

`--save-pass-ir <dir>` 启用 `PassInstrumentationCallbacks::registerAfterPassCallback`。
后端先保存 `frontend` module；每个 pass 回调把当前 module 与上一份文本比较，仅当 IR
改变时用递增序号和清洗后的 pass 名写文件；流水线完成后强制保存 `final`。这保留了
pass 顺序，又避免 analysis/no-op pass 生成大量重复文件。目录创建或写入失败属于编译
错误。fallback 明确拒绝该选项。

`tools/minimize_failure.py` 实现基于行的 ddmin：把候选内容写入隔离临时目录，替换命令
中的 `{input}`，非零退出表示目标故障仍存在；通过逐步删除分块并缩小粒度得到近似
1-minimal 结果。超时不视为复现，以免把“变慢”误判为目标失败。该工具既可处理 `.lm`，
也可处理 pass snapshot `.ll`。

## 10. 构建系统

CMake 的生成依赖为：

```text
parser.y --BISON_TARGET--> parser.cpp + parser.hpp
lexer.l  --FLEX_TARGET---> lexer.cpp
parser.hpp -------------> lexer.cpp
```

`ADD_FLEX_BISON_DEPENDENCY` 保证并行构建时 parser header 先于 scanner 编译可用。源码
目录和 binary 目录都加入 include path。找到 LLVM 时链接 `core`、`support`、`passes`、
`nativecodegen` 和 `native` 组件；没有找到时不引入任何 LLVM headers 或 libraries。

`LUMEN_BUILD_FUZZER=ON` 增加不含 driver/codegen 的前端 fuzz target，默认使用
`-fsanitize=fuzzer,address,undefined`。`LUMEN_FUZZER_STANDALONE=ON` 为缺少 libFuzzer
runtime 的环境提供同一入口的 seed-only sanitizer runner，不替代 CI 中的变异测试。

## 11. 测试设计

`tests/e2e.sh` 接受 compiler、clang、fixture 目录和输出目录四个参数。测试分为：

- 正向主程序：编译、链接并检查输出 `OK`、退出码 42；
- 前向调用：证明两遍函数登记有效；
- 短路副作用：RHS 中的 `putchar` 不应执行；
- 指针/数组：取址、解引用、参数、全局/局部数组和动态下标；
- 内存陷阱：空指针、动态数组越界必须异常终止；
- 静态边界：折叠后常量越界必须在编译期诊断；
- LLVM 后端：验证 O2 IR 变化、目标 triple、O3 对象输出、debug metadata 及链接后行为；
- pass 可观测性：验证 frontend/changed-pass/final 快照均存在；
- fallback 限制：验证不支持的优化、对象和 `-g` 模式产生确定诊断；
- 语义负例：未知变量、错误 arity、缺少 return，并验证一次聚合多个错误；
- 语法负例：检查失败退出码、源码行列和分号边界恢复；
- 差分：比较两个后端生成程序的 stdout 和退出码；
- fuzz/minimizer：seed corpus sanitizer smoke 与确定性 ddmin 回归。

CTest 通过 `$<TARGET_FILE:lumenc>` 传入实际构建产物，避免依赖 PATH。CI 为 LLVM API
和 fallback 建立不同 build directory，防止 CMake cache 让后端选择结果互相污染。

## 12. CI 设计

`.github/workflows/ci.yml` 在 push、pull request 和手工触发时运行三个 Linux job：

1. `llvm-api` 安装 LLVM 开发包，将 `llvm-config --cmakedir` 显式传给 CMake，并断言
   配置日志选择 LLVM API 后端。
2. `text-ir` 显式设置 `CMAKE_DISABLE_FIND_PACKAGE_LLVM=TRUE`，验证最小依赖路径。
3. `frontend-fuzz` 使用 Clang/libFuzzer 与 ASan/UBSan 执行 seed corpus 的 200 次变异。

`llvm-api` job 还构建文本 reference compiler 并运行行为差分。所有 job 都从干净环境
生成 Flex/Bison 代码，普通后端 job 执行完整 CTest。

## 13. 调试手册

### 13.1 前端崩溃

1. 使用最小 `.lm` 重现并先单独运行 `bison -Wall -Wcounterexamples`。
2. 用 ASan/UBSan 构建，检查 parser error path 的 `%destructor` 和 action 中的所有权转移。
3. 检查 scanner 返回的 token 是否携带了正确 `%union` 成员。

### 13.2 LLVM verifier 失败

1. 保存未优化 `.ll`，运行 `opt -verify -disable-output input.ll`。
2. 检查每个 basic block 是否恰有一个 terminator。
3. 检查 PHI predecessor 是否与 CFG 完全一致。
4. 检查 load/store 指针元素类型和 call 参数数量。

### 13.3 优化 pass 后失败

1. 先证明 `-O0` IR 通过 verifier 并能执行。
2. 用 `opt -O1/-O2` 确认首次失败优化等级。
3. 用 `--save-pass-ir snapshots` 比较相邻编号文件，锁定首次改变行为的 pass。
4. 对对应 `.ll` 执行 `tools/minimize_failure.py`；有新版 LLVM 时还可进一步使用
   `llvm-reduce` 做 IR-aware 最小化。
