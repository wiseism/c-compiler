; ModuleID = 'program.lm'
source_filename = "program.lm"

declare i32 @putchar(i32)

define i32 @factorial(i32 %n) {
entry:
  %result = alloca i32
  %n1 = alloca i32
  store i32 %n, i32* %n1
  store i32 1, i32* %result
  br label %while.cond

while.cond:                                       ; preds = %while.body, %entry
  %n2 = load i32, i32* %n1
  %gt = icmp sgt i32 %n2, 1
  %cmp.i32 = zext i1 %gt to i32
  %truth = icmp ne i32 %cmp.i32, 0
  br i1 %truth, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %result3 = load i32, i32* %result
  %n4 = load i32, i32* %n1
  %mul = mul i32 %result3, %n4
  store i32 %mul, i32* %result
  %n5 = load i32, i32* %n1
  %sub = sub i32 %n5, 1
  store i32 %sub, i32* %n1
  br label %while.cond

while.end:                                        ; preds = %while.cond
  %result6 = load i32, i32* %result
  ret i32 %result6
}

define i32 @choose(i32 %condition, i32 %left, i32 %right) {
entry:
  %right3 = alloca i32
  %left2 = alloca i32
  %condition1 = alloca i32
  store i32 %condition, i32* %condition1
  store i32 %left, i32* %left2
  store i32 %right, i32* %right3
  %condition4 = load i32, i32* %condition1
  %truth = icmp ne i32 %condition4, 0
  br i1 %truth, label %logic.rhs, label %logic.end

logic.rhs:                                        ; preds = %entry
  %left5 = load i32, i32* %left2
  %ne = icmp ne i32 %left5, 0
  %cmp.i32 = zext i1 %ne to i32
  %truth6 = icmp ne i32 %cmp.i32, 0
  br label %logic.end

logic.end:                                        ; preds = %logic.rhs, %entry
  %logic = phi i1 [ false, %entry ], [ %truth6, %logic.rhs ]
  %logic.i32 = zext i1 %logic to i32
  %truth7 = icmp ne i32 %logic.i32, 0
  br i1 %truth7, label %if.then, label %if.else

if.then:                                          ; preds = %logic.end
  %left8 = load i32, i32* %left2
  ret i32 %left8

if.else:                                          ; preds = %logic.end
  %right9 = load i32, i32* %right3
  ret i32 %right9

if.end:                                           ; No predecessors!
  unreachable
}

define i32 @operators(i32 %value) {
entry:
  %result19 = alloca i32
  %result = alloca i32
  %value1 = alloca i32
  store i32 %value, i32* %value1
  %value2 = load i32, i32* %value1
  %neg = sub i32 0, %value2
  %add = add i32 %neg, 10
  store i32 %add, i32* %result
  %result3 = load i32, i32* %result
  %le = icmp sle i32 %result3, 4
  %cmp.i32 = zext i1 %le to i32
  %truth = icmp ne i32 %cmp.i32, 0
  %not = xor i1 %truth, true
  %not.i32 = zext i1 %not to i32
  %truth4 = icmp ne i32 %not.i32, 0
  br i1 %truth4, label %logic.rhs, label %logic.end

logic.rhs:                                        ; preds = %entry
  %result5 = load i32, i32* %result
  %ge = icmp sge i32 %result5, 5
  %cmp.i326 = zext i1 %ge to i32
  %truth7 = icmp ne i32 %cmp.i326, 0
  br label %logic.end

logic.end:                                        ; preds = %logic.rhs, %entry
  %logic = phi i1 [ false, %entry ], [ %truth7, %logic.rhs ]
  %logic.i32 = zext i1 %logic to i32
  %truth8 = icmp ne i32 %logic.i32, 0
  br i1 %truth8, label %logic.rhs9, label %logic.end10

logic.rhs9:                                       ; preds = %logic.end
  %result11 = load i32, i32* %result
  %ne = icmp ne i32 %result11, 6
  %cmp.i3212 = zext i1 %ne to i32
  %truth13 = icmp ne i32 %cmp.i3212, 0
  br label %logic.end10

logic.end10:                                      ; preds = %logic.rhs9, %logic.end
  %logic14 = phi i1 [ false, %logic.end ], [ %truth13, %logic.rhs9 ]
  %logic.i3215 = zext i1 %logic14 to i32
  %truth16 = icmp ne i32 %logic.i3215, 0
  br i1 %truth16, label %if.then, label %if.end

if.then:                                          ; preds = %logic.end10
  %result17 = load i32, i32* %result
  %add18 = add i32 %result17, 3
  store i32 %add18, i32* %result
  br label %if.end

if.end:                                           ; preds = %if.then, %logic.end10
  store i32 100, i32* %result19
  %result20 = load i32, i32* %result19
  %add21 = add i32 %result20, 1
  store i32 %add21, i32* %result19
  %result22 = load i32, i32* %result
  ret i32 %result22
}

define i32 @main() {
entry:
  %answer = alloca i32
  br i1 false, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  ret i32 100

if.end:                                           ; preds = %entry
  %call = call i32 @factorial(i32 4)
  %call1 = call i32 @operators(i32 5)
  %add = add i32 %call1, 10
  %call2 = call i32 @choose(i32 1, i32 %add, i32 99)
  %add3 = add i32 %call, %call2
  store i32 %add3, i32* %answer
  %answer4 = load i32, i32* %answer
  %eq = icmp eq i32 %answer4, 42
  %cmp.i32 = zext i1 %eq to i32
  %truth = icmp ne i32 %cmp.i32, 0
  br i1 %truth, label %logic.end, label %logic.rhs

logic.rhs:                                        ; preds = %if.end
  %call5 = call i32 @putchar(i32 88)
  %truth6 = icmp ne i32 %call5, 0
  br label %logic.end

logic.end:                                        ; preds = %logic.rhs, %if.end
  %logic = phi i1 [ true, %if.end ], [ %truth6, %logic.rhs ]
  %logic.i32 = zext i1 %logic to i32
  %truth9 = icmp ne i32 %logic.i32, 0
  br i1 %truth9, label %if.then7, label %if.end8

if.then7:                                         ; preds = %logic.end
  %call10 = call i32 @putchar(i32 79)
  %call11 = call i32 @putchar(i32 75)
  %call12 = call i32 @putchar(i32 10)
  br label %if.end8

if.end8:                                          ; preds = %if.then7, %logic.end
  %answer13 = load i32, i32* %answer
  ret i32 %answer13
}
