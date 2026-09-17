; ModuleID = 'forward_call.lm'
source_filename = "forward_call.lm"

define i32 @main() {
entry:
  %call = call i32 @later(i32 40, i32 2)
  ret i32 %call
}

define i32 @later(i32 %left, i32 %right) {
entry:
  %right2 = alloca i32
  %left1 = alloca i32
  store i32 %left, i32* %left1
  store i32 %right, i32* %right2
  %left3 = load i32, i32* %left1
  %right4 = load i32, i32* %right2
  %add = add i32 %left3, %right4
  ret i32 %add
}
