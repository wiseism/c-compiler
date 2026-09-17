; ModuleID = 'pointers_arrays.lm'
source_filename = "pointers_arrays.lm"

@global_values = global [3 x i32] zeroinitializer

define void @write_value(i32* %target, i32 %value) {
entry:
  %value2 = alloca i32
  %target1 = alloca i32*
  store i32* %target, i32** %target1
  store i32 %value, i32* %value2
  %target3 = load i32*, i32** %target1
  %nonnull = icmp ne i32* %target3, null
  br i1 %nonnull, label %nullcheck.ok, label %nullcheck.trap

nullcheck.ok:                                     ; preds = %entry
  %value4 = load i32, i32* %value2
  store i32 %value4, i32* %target3
  ret void

nullcheck.trap:                                   ; preds = %entry
  call void @llvm.trap()
  unreachable
}

define i32 @sum_three(i32* %values) {
entry:
  %values1 = alloca i32*
  store i32* %values, i32** %values1
  %values2 = load i32*, i32** %values1
  %nonnull = icmp ne i32* %values2, null
  br i1 %nonnull, label %nullcheck.ok, label %nullcheck.trap

nullcheck.ok:                                     ; preds = %entry
  %pointer.element = getelementptr inbounds i32, i32* %values2, i32 0
  %element = load i32, i32* %pointer.element
  %values3 = load i32*, i32** %values1
  %nonnull4 = icmp ne i32* %values3, null
  br i1 %nonnull4, label %nullcheck.ok5, label %nullcheck.trap6

nullcheck.trap:                                   ; preds = %entry
  call void @llvm.trap()
  unreachable

nullcheck.ok5:                                    ; preds = %nullcheck.ok
  %pointer.element7 = getelementptr inbounds i32, i32* %values3, i32 1
  %element8 = load i32, i32* %pointer.element7
  %add = add i32 %element, %element8
  %values9 = load i32*, i32** %values1
  %nonnull10 = icmp ne i32* %values9, null
  br i1 %nonnull10, label %nullcheck.ok11, label %nullcheck.trap12

nullcheck.trap6:                                  ; preds = %nullcheck.ok
  call void @llvm.trap()
  unreachable

nullcheck.ok11:                                   ; preds = %nullcheck.ok5
  %pointer.element13 = getelementptr inbounds i32, i32* %values9, i32 2
  %element14 = load i32, i32* %pointer.element13
  %add15 = add i32 %add, %element14
  ret i32 %add15

nullcheck.trap12:                                 ; preds = %nullcheck.ok5
  call void @llvm.trap()
  unreachable
}

define i32 @main() {
entry:
  %first = alloca i32*
  %index = alloca i32
  %local = alloca [3 x i32]
  store [3 x i32] zeroinitializer, [3 x i32]* %local
  store i32 0, i32* %index
  br label %while.cond

while.cond:                                       ; preds = %boundscheck.ok, %entry
  %index1 = load i32, i32* %index
  %lt = icmp slt i32 %index1, 3
  %cmp.i32 = zext i1 %lt to i32
  %truth = icmp ne i32 %cmp.i32, 0
  br i1 %truth, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %index2 = load i32, i32* %index
  %index.nonnegative = icmp sge i32 %index2, 0
  %index.inrange = icmp slt i32 %index2, 3
  %index.valid = and i1 %index.nonnegative, %index.inrange
  br i1 %index.valid, label %boundscheck.ok, label %boundscheck.trap

while.end:                                        ; preds = %while.cond
  br i1 true, label %boundscheck.ok6, label %boundscheck.trap7

boundscheck.ok:                                   ; preds = %while.body
  %array.element = getelementptr inbounds [3 x i32], [3 x i32]* %local, i32 0, i32 %index2
  %index3 = load i32, i32* %index
  %add = add i32 %index3, 1
  %mul = mul i32 %add, 10
  store i32 %mul, i32* %array.element
  %index4 = load i32, i32* %index
  %add5 = add i32 %index4, 1
  store i32 %add5, i32* %index
  br label %while.cond

boundscheck.trap:                                 ; preds = %while.body
  call void @llvm.trap()
  unreachable

boundscheck.ok6:                                  ; preds = %while.end
  %array.element8 = getelementptr inbounds [3 x i32], [3 x i32]* %local, i32 0, i32 0
  store i32* %array.element8, i32** %first
  br i1 true, label %boundscheck.ok9, label %boundscheck.trap10

boundscheck.trap7:                                ; preds = %while.end
  call void @llvm.trap()
  unreachable

boundscheck.ok9:                                  ; preds = %boundscheck.ok6
  %array.element11 = getelementptr inbounds [3 x i32], [3 x i32]* %local, i32 0, i32 1
  call void @write_value(i32* %array.element11, i32 20)
  %first12 = load i32*, i32** %first
  %ne = icmp ne i32* %first12, null
  %cmp.i3213 = zext i1 %ne to i32
  %truth14 = icmp ne i32 %cmp.i3213, 0
  br i1 %truth14, label %logic.rhs, label %logic.end

boundscheck.trap10:                               ; preds = %boundscheck.ok6
  call void @llvm.trap()
  unreachable

logic.rhs:                                        ; preds = %boundscheck.ok9
  br label %logic.end

logic.end:                                        ; preds = %logic.rhs, %boundscheck.ok9
  %logic = phi i1 [ false, %boundscheck.ok9 ], [ true, %logic.rhs ]
  %logic.i32 = zext i1 %logic to i32
  %truth15 = icmp ne i32 %logic.i32, 0
  br i1 %truth15, label %if.then, label %if.end

if.then:                                          ; preds = %logic.end
  br i1 true, label %boundscheck.ok16, label %boundscheck.trap17

if.end:                                           ; preds = %nullcheck.ok29, %logic.end
  br i1 true, label %boundscheck.ok33, label %boundscheck.trap34

boundscheck.ok16:                                 ; preds = %if.then
  %first18 = load i32*, i32** %first
  %nonnull = icmp ne i32* %first18, null
  br i1 %nonnull, label %nullcheck.ok, label %nullcheck.trap

boundscheck.trap17:                               ; preds = %if.then
  call void @llvm.trap()
  unreachable

nullcheck.ok:                                     ; preds = %boundscheck.ok16
  %deref = load i32, i32* %first18
  store i32 %deref, i32* getelementptr inbounds ([3 x i32], [3 x i32]* @global_values, i32 0, i32 0)
  br i1 true, label %boundscheck.ok19, label %boundscheck.trap20

nullcheck.trap:                                   ; preds = %boundscheck.ok16
  call void @llvm.trap()
  unreachable

boundscheck.ok19:                                 ; preds = %nullcheck.ok
  %first21 = load i32*, i32** %first
  %nonnull22 = icmp ne i32* %first21, null
  br i1 %nonnull22, label %nullcheck.ok23, label %nullcheck.trap24

boundscheck.trap20:                               ; preds = %nullcheck.ok
  call void @llvm.trap()
  unreachable

nullcheck.ok23:                                   ; preds = %boundscheck.ok19
  %pointer.element = getelementptr inbounds i32, i32* %first21, i32 1
  %element = load i32, i32* %pointer.element
  store i32 %element, i32* getelementptr inbounds ([3 x i32], [3 x i32]* @global_values, i32 0, i32 1)
  br i1 true, label %boundscheck.ok25, label %boundscheck.trap26

nullcheck.trap24:                                 ; preds = %boundscheck.ok19
  call void @llvm.trap()
  unreachable

boundscheck.ok25:                                 ; preds = %nullcheck.ok23
  %first27 = load i32*, i32** %first
  %nonnull28 = icmp ne i32* %first27, null
  br i1 %nonnull28, label %nullcheck.ok29, label %nullcheck.trap30

boundscheck.trap26:                               ; preds = %nullcheck.ok23
  call void @llvm.trap()
  unreachable

nullcheck.ok29:                                   ; preds = %boundscheck.ok25
  %pointer.element31 = getelementptr inbounds i32, i32* %first27, i32 2
  %element32 = load i32, i32* %pointer.element31
  %sub = sub i32 %element32, 18
  store i32 %sub, i32* getelementptr inbounds ([3 x i32], [3 x i32]* @global_values, i32 0, i32 2)
  br label %if.end

nullcheck.trap30:                                 ; preds = %boundscheck.ok25
  call void @llvm.trap()
  unreachable

boundscheck.ok33:                                 ; preds = %if.end
  %call = call i32 @sum_three(i32* getelementptr inbounds ([3 x i32], [3 x i32]* @global_values, i32 0, i32 0))
  ret i32 %call

boundscheck.trap34:                               ; preds = %if.end
  call void @llvm.trap()
  unreachable
}

; Function Attrs: cold noreturn nounwind
declare void @llvm.trap() #0

attributes #0 = { cold noreturn nounwind }
