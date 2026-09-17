; ModuleID = 'fibonacci.lm'
source_filename = "fibonacci.lm"

declare i32 @putchar(i32)

define i32 @fib(i32 %n) {
entry:
  %n1 = alloca i32
  store i32 %n, i32* %n1
  %n2 = load i32, i32* %n1
  %lt = icmp slt i32 %n2, 2
  %cmp.i32 = zext i1 %lt to i32
  %truth = icmp ne i32 %cmp.i32, 0
  br i1 %truth, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %n3 = load i32, i32* %n1
  ret i32 %n3

if.end:                                           ; preds = %entry
  %n4 = load i32, i32* %n1
  %sub = sub i32 %n4, 1
  %call = call i32 @fib(i32 %sub)
  %n5 = load i32, i32* %n1
  %sub6 = sub i32 %n5, 2
  %call7 = call i32 @fib(i32 %sub6)
  %add = add i32 %call, %call7
  ret i32 %add
}

define i32 @main() {
entry:
  %value = alloca i32
  %call = call i32 @fib(i32 9)
  store i32 %call, i32* %value
  %value1 = load i32, i32* %value
  %eq = icmp eq i32 %value1, 34
  %cmp.i32 = zext i1 %eq to i32
  %truth = icmp ne i32 %cmp.i32, 0
  br i1 %truth, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %call2 = call i32 @putchar(i32 79)
  %call3 = call i32 @putchar(i32 75)
  %call4 = call i32 @putchar(i32 10)
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  %value5 = load i32, i32* %value
  ret i32 %value5
}
