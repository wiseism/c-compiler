; ModuleID = 'void_globals_fold.lm'
source_filename = "void_globals_fold.lm"

@result = global i32 0
@base = global i32 20

define void @set_result(i32 %value) {
entry:
  %value1 = alloca i32
  store i32 %value, i32* %value1
  %value2 = load i32, i32* %value1
  store i32 %value2, i32* @result
  ret void
}

define i32 @folded() {
entry:
  ret i32 21
}

define void @tick() {
entry:
  %result = load i32, i32* @result
  %add = add i32 %result, 1
  store i32 %add, i32* @result
  ret void
}

define i32 @main() {
entry:
  %base = load i32, i32* @base
  %call = call i32 @folded()
  %add = add i32 %base, %call
  %sub = sub i32 %add, 1
  call void @set_result(i32 %sub)
  call void @tick()
  %result = load i32, i32* @result
  %add1 = add i32 %result, 1
  ret i32 %add1
}
