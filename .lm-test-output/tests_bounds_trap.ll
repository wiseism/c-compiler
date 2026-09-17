; ModuleID = 'bounds_trap.lm'
source_filename = "bounds_trap.lm"

define i32 @main() {
entry:
  %index = alloca i32
  %values = alloca [2 x i32]
  store [2 x i32] zeroinitializer, [2 x i32]* %values
  store i32 2, i32* %index
  %index1 = load i32, i32* %index
  %index.nonnegative = icmp sge i32 %index1, 0
  %index.inrange = icmp slt i32 %index1, 2
  %index.valid = and i1 %index.nonnegative, %index.inrange
  br i1 %index.valid, label %boundscheck.ok, label %boundscheck.trap

boundscheck.ok:                                   ; preds = %entry
  %array.element = getelementptr inbounds [2 x i32], [2 x i32]* %values, i32 0, i32 %index1
  %element = load i32, i32* %array.element
  ret i32 %element

boundscheck.trap:                                 ; preds = %entry
  call void @llvm.trap()
  unreachable
}

; Function Attrs: cold noreturn nounwind
declare void @llvm.trap() #0

attributes #0 = { cold noreturn nounwind }
