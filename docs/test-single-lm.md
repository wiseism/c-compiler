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
