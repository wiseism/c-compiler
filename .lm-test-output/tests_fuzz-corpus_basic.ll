; ModuleID = 'basic.lm'
source_filename = "basic.lm"

define i32 @main() {
entry:
  %value = alloca i32
  store i32 42, i32* %value
  %value1 = load i32, i32* %value
  ret i32 %value1
}
