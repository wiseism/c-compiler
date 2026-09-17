; ModuleID = 'null_trap.lm'
source_filename = "null_trap.lm"

define i32 @main() {
entry:
  %pointer = alloca i32*
  store i32* null, i32** %pointer
  %pointer1 = load i32*, i32** %pointer
  %nonnull = icmp ne i32* %pointer1, null
  br i1 %nonnull, label %nullcheck.ok, label %nullcheck.trap

nullcheck.ok:                                     ; preds = %entry
  %deref = load i32, i32* %pointer1
  ret i32 %deref

nullcheck.trap:                                   ; preds = %entry
  call void @llvm.trap()
  unreachable
}

; Function Attrs: cold noreturn nounwind
declare void @llvm.trap() #0

attributes #0 = { cold noreturn nounwind }
