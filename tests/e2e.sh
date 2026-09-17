#!/usr/bin/env bash
set -u

compiler=$1
clang=$2
fixtures=$3
output_dir=$4

mkdir -p "$output_dir"

"$compiler" "$fixtures/program.lm" -o "$output_dir/program.ll" || exit 1
"$clang" "$output_dir/program.ll" -o "$output_dir/program" || exit 1

program_output=$("$output_dir/program")
program_status=$?
if [[ $program_status -ne 42 ]]; then
    echo "expected program exit status 42, got $program_status" >&2
    exit 1
fi
if [[ "$program_output" != "OK" ]]; then
    echo "expected program output OK, got: $program_output" >&2
    exit 1
fi

"$compiler" "$fixtures/forward_call.lm" -o "$output_dir/forward_call.ll" || exit 1
"$clang" "$output_dir/forward_call.ll" -o "$output_dir/forward_call" || exit 1
"$output_dir/forward_call"
forward_status=$?
if [[ $forward_status -ne 42 ]]; then
    echo "expected forward-call program exit status 42, got $forward_status" >&2
    exit 1
fi

"$compiler" "$fixtures/bitwise_xor.lm" -o "$output_dir/bitwise_xor.ll" || exit 1
"$clang" "$output_dir/bitwise_xor.ll" -o "$output_dir/bitwise_xor" || exit 1
"$output_dir/bitwise_xor"
xor_status=$?
if [[ $xor_status -ne 42 ]]; then
    echo "expected bitwise-xor program exit status 42, got $xor_status" >&2
    exit 1
fi
if "$compiler" "$fixtures/bitwise_xor_type_error.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/bitwise_xor_type_error.txt"; then
    echo "pointer xor unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "left operand of '\^' requires 'int', got 'int\*'" \
    "$output_dir/bitwise_xor_type_error.txt"; then
    echo "bitwise-xor type diagnostic was not specific" >&2
    exit 1
fi

"$compiler" "$fixtures/break_loop.lm" -o "$output_dir/break_loop.ll" || exit 1
"$clang" "$output_dir/break_loop.ll" -o "$output_dir/break_loop" || exit 1
"$output_dir/break_loop"
break_status=$?
if [[ $break_status -ne 42 ]]; then
    echo "expected break-loop program exit status 42, got $break_status" >&2
    exit 1
fi
if "$compiler" "$fixtures/break_outside_loop.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/break_outside_loop.txt"; then
    echo "break outside loop unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "'break' is only valid inside a loop" \
    "$output_dir/break_outside_loop.txt"; then
    echo "break placement diagnostic was not specific" >&2
    exit 1
fi

"$compiler" "$fixtures/string_puts.lm" -o "$output_dir/string_puts.ll" || exit 1
"$clang" "$output_dir/string_puts.ll" -o "$output_dir/string_puts" || exit 1
string_output=$("$output_dir/string_puts")
string_status=$?
if [[ $string_status -ne 42 || "$string_output" != "Hello, Lumen!" ]]; then
    echo "string/puts test failed: status=$string_status output=$string_output" >&2
    exit 1
fi
if "$compiler" "$fixtures/string_type_error.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/string_type_error.txt"; then
    echo "string-to-int initialization unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "cannot initialize 'value' of type 'int' with 'char\*'" \
    "$output_dir/string_type_error.txt"; then
    echo "string type diagnostic was not specific" >&2
    exit 1
fi

"$compiler" "$fixtures/const_local.lm" -o "$output_dir/const_local.ll" || exit 1
"$clang" "$output_dir/const_local.ll" -o "$output_dir/const_local" || exit 1
"$output_dir/const_local"
const_status=$?
if [[ $const_status -ne 42 ]]; then
    echo "expected const-local program exit status 42, got $const_status" >&2
    exit 1
fi
if "$compiler" "$fixtures/const_assignment_error.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/const_assignment_error.txt"; then
    echo "assignment to const unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "cannot assign to const object" "$output_dir/const_assignment_error.txt"; then
    echo "const assignment diagnostic was not specific" >&2
    exit 1
fi
if "$compiler" "$fixtures/const_initializer_error.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/const_initializer_error.txt"; then
    echo "nonconstant const initializer unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "initializer for const variable 'answer' is not a constant expression" \
    "$output_dir/const_initializer_error.txt"; then
    echo "const initializer diagnostic was not specific" >&2
    exit 1
fi

if "$compiler" "$fixtures/semantic_error.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/semantic_error.txt"; then
    echo "semantic error input unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "unknown variable 'missing'" "$output_dir/semantic_error.txt"; then
    echo "semantic diagnostic did not identify the unknown variable" >&2
    exit 1
fi

if "$compiler" "$fixtures/syntax_error.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/syntax_error.txt"; then
    echo "syntax error input unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "line 3" "$output_dir/syntax_error.txt"; then
    echo "syntax diagnostic did not include the source line" >&2
    exit 1
fi

if "$compiler" "$fixtures/multiple_syntax_errors.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/multiple_syntax_errors.txt"; then
    echo "multiple syntax errors unexpectedly compiled" >&2
    exit 1
fi
syntax_count=$(grep -c '^line [0-9][0-9]*:[0-9][0-9]*:' \
    "$output_dir/multiple_syntax_errors.txt")
if [[ $syntax_count -lt 2 ]]; then
    echo "parser did not recover and report multiple line:column diagnostics" >&2
    exit 1
fi

if "$compiler" "$fixtures/multiple_semantic_errors.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/multiple_semantic_errors.txt"; then
    echo "multiple semantic errors unexpectedly compiled" >&2
    exit 1
fi
for missing_name in first_missing second_missing third_missing; do
    if ! grep -q "unknown variable '$missing_name'" \
        "$output_dir/multiple_semantic_errors.txt"; then
        echo "semantic analyzer did not aggregate $missing_name" >&2
        exit 1
    fi
done
if ! grep -Eq '^error: line [0-9]+:[0-9]+:' \
    "$output_dir/multiple_semantic_errors.txt"; then
    echo "semantic diagnostic did not include a source column" >&2
    exit 1
fi

if "$compiler" "$fixtures/wrong_arity.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/wrong_arity.txt"; then
    echo "wrong-arity call unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "expects 1 argument(s), got 2" "$output_dir/wrong_arity.txt"; then
    echo "wrong-arity diagnostic was not specific" >&2
    exit 1
fi

if "$compiler" "$fixtures/missing_return.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/missing_return.txt"; then
    echo "missing return unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "may reach the end without returning" "$output_dir/missing_return.txt"; then
    echo "missing-return diagnostic was not specific" >&2
    exit 1
fi

if "$compiler" "$fixtures/duplicate_parameter.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/duplicate_parameter.txt"; then
    echo "duplicate parameter unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "duplicate parameter 'value'" "$output_dir/duplicate_parameter.txt"; then
    echo "duplicate-parameter diagnostic was not specific" >&2
    exit 1
fi

"$compiler" "$fixtures/void_globals_fold.lm" -o "$output_dir/void_globals_fold.ll" || exit 1
"$clang" "$output_dir/void_globals_fold.ll" -o "$output_dir/void_globals_fold" || exit 1
"$output_dir/void_globals_fold"
feature_status=$?
if [[ $feature_status -ne 42 ]]; then
    echo "expected void/global/folding program exit status 42, got $feature_status" >&2
    exit 1
fi
if ! grep -Eq '@base = .*global i32 20' "$output_dir/void_globals_fold.ll"; then
    echo "global constant expression was not folded to 20" >&2
    exit 1
fi
if ! grep -Eq 'ret i32 21' "$output_dir/void_globals_fold.ll"; then
    echo "function constant expression was not folded to 21" >&2
    exit 1
fi

if "$compiler" "$fixtures/void_value_error.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/void_value_error.txt"; then
    echo "void value misuse unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "return requires 'int', got 'void'" "$output_dir/void_value_error.txt"; then
    echo "void value diagnostic was not specific" >&2
    exit 1
fi

if "$compiler" "$fixtures/nonconstant_global.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/nonconstant_global.txt"; then
    echo "nonconstant global initializer unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "initializer for global 'global' is not a constant expression" \
    "$output_dir/nonconstant_global.txt"; then
    echo "nonconstant global diagnostic was not specific" >&2
    exit 1
fi

"$compiler" "$fixtures/pointers_arrays.lm" -o "$output_dir/pointers_arrays.ll" || exit 1
"$clang" "$output_dir/pointers_arrays.ll" -o "$output_dir/pointers_arrays" || exit 1
"$output_dir/pointers_arrays"
pointer_status=$?
if [[ $pointer_status -ne 42 ]]; then
    echo "expected pointer/array program exit status 42, got $pointer_status" >&2
    exit 1
fi
if ! grep -q "llvm.trap" "$output_dir/pointers_arrays.ll"; then
    echo "pointer/array IR does not contain runtime safety traps" >&2
    exit 1
fi

for trap_case in null_trap bounds_trap; do
    "$compiler" "$fixtures/$trap_case.lm" -o "$output_dir/$trap_case.ll" || exit 1
    "$clang" "$output_dir/$trap_case.ll" -o "$output_dir/$trap_case" || exit 1
    if "$output_dir/$trap_case" >"$output_dir/$trap_case.stdout" 2>"$output_dir/$trap_case.stderr"; then
        echo "$trap_case unexpectedly completed without trapping" >&2
        exit 1
    fi
done

if "$compiler" "$fixtures/constant_bounds_error.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/constant_bounds_error.txt"; then
    echo "constant out-of-bounds index unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "is outside \[0, 2)" "$output_dir/constant_bounds_error.txt"; then
    echo "constant bounds diagnostic was not specific" >&2
    exit 1
fi

if "$compiler" "$fixtures/pointer_type_error.lm" -o "$output_dir/bad.ll" \
    2>"$output_dir/pointer_type_error.txt"; then
    echo "invalid pointer initializer unexpectedly compiled" >&2
    exit 1
fi
if ! grep -q "cannot initialize 'pointer' of type 'int\*' with 'int'" \
    "$output_dir/pointer_type_error.txt"; then
    echo "pointer type diagnostic was not specific" >&2
    exit 1
fi
