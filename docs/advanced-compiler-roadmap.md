# 编译器进阶学习路线：教材、课程、论文与项目

这份路线不以本仓库代码为目录，而以“从能写一个小编译器，到能理解现代生产编译器和
编译器论文”为目标。建议周期为 12～18 个月，每周投入 8～12 小时。不要等所有理论读完
才动手；每个阶段都应同时包含教材、论文和一个可运行项目。

## 1. 最终能力目标

完成主线后，应能够：

- 独立设计 AST、类型系统、IR 和运行时语义；
- 在 CFG/SSA 上实现数据流分析和中端优化；
- 理解指令选择、寄存器分配、ABI、目标文件和链接；
- 构建解释器、字节码 VM、AOT 或 JIT 编译器中的至少一种；
- 使用基准、差分测试、fuzzing 和 translation validation 验证编译器；
- 阅读 PLDI、POPL、CGO、CC、OOPSLA 等会议的编译器论文并复现实验。

## 2. 前置知识

如果下列内容不熟，先用 2～4 周补齐：

- 数据结构与算法：图、DFS、拓扑排序、并查集、工作列表算法；
- 离散数学：偏序、格、不动点、集合与关系；
- C/C++：内存布局、未定义行为、RAII、构建和调试；
- 体系结构：寄存器、栈帧、调用约定、缓存、流水线；
- 操作系统：进程地址空间、虚拟内存、动态库和系统调用；
- 至少熟悉一种汇编，推荐 x86-64 或 AArch64。

体系结构可配合 Patterson 与 Hennessy 的 *Computer Organization and Design*；类型系统
基础可选 Pierce 的 *Types and Programming Languages*。

## 3. 主线学习计划

### 阶段一：前端理论与语言语义（4～6 周）

知识点：正则语言、上下文无关文法、LL/LR、错误恢复、名字解析、作用域、类型推导、
子类型、闭包和语义规则。

学习资料：

- Aho 等人的 *Compilers: Principles, Techniques, and Tools*，重点读第 2～6 章；
- Cooper 与 Torczon 的 *Engineering a Compiler*，重点读前端与 IR 章节；
- Stanford [CS143 Compilers](https://web.stanford.edu/class/cs143/)；
- 免费在线书 [Crafting Interpreters](https://craftinginterpreters.com/)，用来补齐闭包、类、
  字节码 VM 和 GC 的工程直觉。

项目：从零实现一个手写 lexer 和递归下降 parser，不使用生成器；加入函数、闭包、静态
类型检查和多错误恢复。为文法写 FIRST/FOLLOW 集计算器，并将手写 parser 与 LR 工具生成
的 parser 做输入差分。

验收标准：能解释 shift/reduce conflict 的成因；能为语言写静态语义规则；错误程序不会
导致 parser 崩溃，并能在一次编译中报告多个诊断。

### 阶段二：解释器、字节码与运行时（5～7 周）

知识点：树遍历解释、栈式/寄存器式字节码、调用帧、闭包捕获、对象表示、tagged value、
异常、垃圾回收和 native interface。

学习资料：

- *Crafting Interpreters* 的 clox 部分；
- Jones、Hosking、Moss 的 *The Garbage Collection Handbook*；
- Appel 的 *Modern Compiler Implementation in ML* 中运行时和 GC 章节。

项目：实现一个字节码 VM，至少包含闭包、递归、数组、异常和 mark-sweep GC；记录每条
opcode 的执行次数，并实现一个简单的 inline cache 或 superinstruction。

验收标准：能够画出函数调用前后的栈帧；能够解释精确 GC 如何找到 roots；能够用基准
数据说明某个 VM 优化是否真的有效。

### 阶段三：CFG、数据流与 SSA（6～8 周）

这是从“会写语言前端”进入“会做优化编译器”的关键阶段。

知识点：基本块、CFG、支配关系、支配树、支配边界、SSA、phi、use-def chain、活跃变量、
可达定义、格、单调数据流框架、稀疏分析和不动点迭代。

核心教材与课程：

- *Engineering a Compiler* 的数据流和优化章节；
- Muchnick 的 *Advanced Compiler Design and Implementation*；
- Cornell [CS 6120 Advanced Compilers 自学课程](https://www.cs.cornell.edu/courses/cs6120/2025fa/self-guided/)；
- LLVM [Language Reference](https://llvm.org/docs/LangRef.html)。

必读论文：

1. Kildall, [A Unified Approach to Global Program Optimization](https://doi.org/10.1145/512927.512945)，建立格和不动点视角。
2. Cytron 等人, [Efficiently Computing Static Single Assignment Form and the Control Dependence Graph](https://doi.org/10.1145/115372.115320)，理解支配边界和 phi 插入。
3. Wegman 与 Zadeck, [Constant Propagation with Conditional Branches](https://doi.org/10.1145/103135.103136)，学习 SCCP。

项目：设计自己的三地址 IR，完成 CFG 构造、Lengauer–Tarjan 支配树、SSA 构造和 SSA
销毁；实现活跃变量、常量传播与不可达块删除。为每个分析输出 Graphviz 图。

验收标准：能够手工算出循环 CFG 的 dominator、dominance frontier 和 phi 位置；工作列表
顺序变化不影响最终分析结果；SSA verifier 能发现重复定义和非法支配关系。

### 阶段四：中端优化（8～10 周）

知识点：常量折叠、SCCP、GVN/CSE、DCE、ADCE、LICM、强度削弱、归纳变量、别名分析、
逃逸分析、内联、循环展开、向量化、pass ordering 和成本模型。

学习资料：

- LLVM [Writing an Optimization](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/) 与
  [Passes 文档](https://llvm.org/docs/Passes.html)；
- LLVM 原始设计论文：Lattner 与 Adve，
  [LLVM: A Compilation Framework for Lifelong Program Analysis & Transformation](https://llvm.org/pubs/2004-01-30-CGO-LLVM.html)；
- LLVM 源码中的 `InstCombine`、`GVN`、`SCCP` 和 `Loop` 分析实现。

项目：在自有 IR 或 LLVM new pass manager 上实现以下 pass：

1. SCCP，同时删除不可达 CFG 边；
2. 基于 dominator tree 的 GVN；
3. LICM，带保守 alias/side-effect 判断；
4. 一个函数内联器，使用可解释的成本模型。

每个 pass 都要记录编译时间、代码大小、动态指令数和运行时间，不能只比较 IR 文本。

验收标准：明确区分“优化合法性”和“优化收益”；能写出 pass 的前置条件、保持的不变量
与可能失效的分析；遇到性能下降时能判断是成本模型还是 pass ordering 问题。

### 阶段五：机器后端（8～10 周）

知识点：DAG/tree instruction selection、legalization、machine IR、calling convention、
instruction scheduling、寄存器活跃区间、spilling、图着色、linear scan、栈帧、汇编打印、
目标文件和重定位。

教材与资料：

- *Engineering a Compiler* 的代码生成章节；
- Appel 的 *Modern Compiler Implementation*；
- LLVM [Writing an LLVM Backend](https://llvm.org/docs/WritingAnLLVMBackend.html)；
- Levine 的 *Linkers and Loaders*；
- System V AMD64 ABI 或目标平台的 AAPCS64 文档。

必读论文：

1. Aho、Ganapathi、Tjiang, [Code Generation Using Tree Matching and Dynamic Programming](https://doi.org/10.1145/69558.75700)。
2. Chaitin 等人, [Register Allocation via Coloring](https://doi.org/10.1016/0141-9331(81)90048-5)。
3. Briggs、Cooper、Torczon, [Improvements to Graph Coloring Register Allocation](https://doi.org/10.1145/177492.177575)。
4. Poletto 与 Sarkar, [Linear Scan Register Allocation](https://doi.org/10.1145/330249.330250)。

项目：为一个小型 RISC ISA 或 WebAssembly 写后端；实现 tree/DAG instruction selector、
linear scan register allocator、spill/reload、prologue/epilogue 和 ELF object emission。
进阶选项是在 LLVM 中加入一个教学目标后端。

验收标准：能够从 IR 一直追踪到机器指令和重定位；解释 caller/callee-saved 寄存器；用
压力测试证明 allocator 在高寄存器压力下不会生成错误代码。

### 阶段六：JIT、动态优化与 GC（6～8 周）

知识点：baseline compiler、tiered compilation、profiling、OSR、deoptimization、inline
cache、speculation、guard、trace JIT、code cache 和 safepoint。

学习资料：

- LLVM [Building an ORC JIT](https://llvm.org/docs/tutorial/BuildingAJIT1.html)；
- HotSpot、V8 TurboFan、Graal/Truffle 的官方设计资料；
- *The Garbage Collection Handbook* 的 generational、concurrent 和 incremental GC 章节。

项目：给字节码 VM 加一个 baseline JIT；收集类型和调用热点，达到阈值后编译热函数；
加入 guard 与 deoptimization，能够退回解释器继续执行。

验收标准：能说明 speculative optimization 为什么必须配套去优化；能测量 warm-up、稳态
吞吐和尾延迟，而不是只报告一次运行时间。

### 阶段七：现代多层 IR 与新目标（6～8 周）

知识点：多层 lowering、dialect、rewrite pattern、shape inference、polyhedral model、
tensor compiler、GPU/SIMD、WebAssembly 和领域专用语言。

资料与论文：

- MLIR 官方 [Toy Tutorial](https://mlir.llvm.org/docs/Tutorials/Toy/)；
- Lattner 等人, [MLIR: Scaling Compiler Infrastructure for Domain Specific Computation](https://arxiv.org/abs/2002.11054)；
- Haas 等人, [Bringing the Web up to Speed with WebAssembly](https://research.google/pubs/bringing-the-web-up-to-speed-with-webassembly/)；
- WebAssembly [Core Specification](https://www.w3.org/TR/wasm-core-2/)。

项目任选其一：

- 用 MLIR 定义 tensor DSL，从自定义 dialect 逐层 lowering 到 LLVM；
- 为语言增加 Wasm 后端，并在浏览器和 Wasmtime 中运行；
- 实现矩阵算子的 tiling、fusion、vectorization，再比较不同调度的性能。

验收标准：能说明为什么过早 lowering 会丢失高层语义；能够给每层 IR 定义 verifier，
并证明 lowering 前后的可观察行为一致。

### 阶段八：正确性、安全与编译器研究（持续）

知识点：随机生成、差分测试、equivalence modulo inputs、metamorphic testing、translation
validation、SMT、未定义行为、形式语义和编译器正确性证明。

必读论文：

1. Yang 等人, [Finding and Understanding Bugs in C Compilers](https://users.cs.utah.edu/~regehr/papers/pldi11-preprint.pdf)，即 Csmith 工作。
2. Le、Afshari、Su, [Compiler Validation via Equivalence Modulo Inputs](https://doi.org/10.1145/2594291.2594334)。
3. Lopes 等人, [Alive2: Bounded Translation Validation for LLVM](https://web.ist.utl.pt/nuno.lopes/pubs.php?id=alive2-pldi21)。
4. Leroy, [Formal Verification of a Realistic Compiler](https://xavierleroy.org/publi/compcert-CACM.pdf)。

项目：为自己的 IR 写随机程序生成器和 reducer；对优化前后程序做差分执行；再将一个
peephole 优化编码为 SMT 查询，检查 refinement。进阶学习 Rocq/Coq，并证明一个表达式
编译器或栈机编译器保持语义。

验收标准：测试生成器不会依赖未定义行为；失败样例能够自动最小化；能够区分 compiler
verification、translation validation、testing 三者提供的保证。

## 4. 推荐教材的使用方式

不要从第一页顺序读完所有教材。建议按问题查阅：

| 资料 | 最适合解决的问题 |
|---|---|
| *Engineering a Compiler* | 现代编译器主线、IR、分析、优化和后端 |
| *Compilers: Principles, Techniques, and Tools* | 词法、文法、类型和经典理论参考 |
| *Modern Compiler Implementation in ML* | 以完整实现串联闭包、IR、寄存器分配和 GC |
| *Advanced Compiler Design and Implementation* | 深入优化、依赖分析和机器相关技术 |
| *Types and Programming Languages* | 类型系统、操作语义和类型安全 |
| *The Garbage Collection Handbook* | GC 算法、写屏障、并发与分代收集 |
| *Linkers and Loaders* | object、symbol、relocation、动态链接 |
| LLVM/MLIR 官方文档 | 生产工具链 API 和实现细节 |

## 5. 论文阅读顺序与方法

第一轮只读：摘要、引言、核心图、结论，回答“解决什么问题、旧方法哪里不够”。第二轮
写出算法的输入、输出、不变量、复杂度和反例。第三轮才读评测和实现细节，并做最小复现。

建议维护一页式论文卡片：

```text
问题：
前提与语义模型：
核心算法：
正确性依据：
复杂度：
实验基线与指标：
最强证据：
威胁与限制：
我能复现的最小部分：
与其他论文的关系：
```

阅读顺序推荐：Kildall → Cytron SSA → SCCP → LLVM → 指令选择 → 图着色 → linear scan
→ Csmith → CompCert → Alive2 → MLIR。先掌握长期稳定的基础，再读某个具体框架的实现论文。

## 6. 五个递进项目

1. `MiniLang-VM`：带闭包、异常和 GC 的字节码语言。
2. `MiniSSA`：自定义 SSA IR、verifier、解释器和 4 个优化 pass。
3. `MiniNative`：面向 x86-64/AArch64/RISC-V 的指令选择、寄存器分配和 ELF 输出。
4. `MiniJIT`：profiling、热点编译、guard、OSR/deoptimization。
5. `MiniValidate`：随机程序生成、差分执行、自动缩减和 SMT translation validation。

每个项目都应有语言/IR 规范、自动测试、benchmark、设计文档和实验报告。保留失败案例，
因为真正的编译器能力通常来自解释“为什么错”，而不只是展示“可以运行”。

## 7. 每周学习节奏

- 3 小时：教材或课程；
- 2 小时：精读一篇论文并写论文卡片；
- 4～6 小时：实现、测试和 benchmark；
- 1 小时：整理实验结果和下周问题。

每四周安排一次复盘：删除没有证据支持的性能结论，补齐失败测试，画一张新的编译流水线
或数据流图，并用 15 分钟向别人讲清本月最重要的算法。

## 8. 如何选择深入方向

- 喜欢语言设计：类型系统、宏、effects、closure conversion、CPS；
- 喜欢性能：循环优化、向量化、PGO、并行化、GPU/tensor compiler；
- 喜欢系统：JIT、GC、linker、debugger、binary rewriting；
- 喜欢硬件：instruction selection、scheduling、register allocation、新 ISA 后端；
- 喜欢严谨性：形式语义、CompCert、Alive2、SMT、translation validation；
- 喜欢找 bug：fuzzing、差分测试、reducer、未定义行为和安全加固。

主线完成前不要同时深入三个方向。先选择一个方向做出可测量、可复现的项目，再扩展到
相邻领域。

## 9. 常见误区

- 只会调用 LLVM API，不理解 CFG、SSA 和数据流；
- 只看 IR 是否“更短”，不检查语义保持和真实性能；
- benchmark 不预热、不固定输入、不报告方差；
- 测试程序含未定义行为，导致差分结论无效；
- 一上来读大型生产编译器，缺少自己的小 IR 作为实验平台；
- 读论文只记结论，不复现算法、不寻找反例；
- 把 parser 当成编译器最难的部分，而忽视优化合法性、运行时和机器约束。

最有效的学习循环是：定义语义 → 实现 → 构造反例 → 自动验证 → 测量 → 阅读论文解释
结果 → 修改设计。这个循环比单纯增加语言特性更接近真实编译器研究与工程。
