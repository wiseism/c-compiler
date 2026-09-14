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
