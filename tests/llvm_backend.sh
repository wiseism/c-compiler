#!/usr/bin/env bash
set -u

compiler=$1
clang=$2
source_file=$3
output_dir=$4

mkdir -p "$output_dir"

"$compiler" -O2 --emit-llvm "$source_file" -o "$output_dir/program.O2.ll" || exit 1
if ! grep -q '^target triple = ' "$output_dir/program.O2.ll"; then
    echo "optimized IR does not carry the selected target triple" >&2
    exit 1
fi
if grep -q ' alloca ' "$output_dir/program.O2.ll"; then
    echo "O2 pipeline did not promote stack temporaries" >&2
    exit 1
fi
"$clang" "$output_dir/program.O2.ll" -o "$output_dir/from-ir" || exit 1
program_output=$("$output_dir/from-ir")
program_status=$?
if [[ $program_status -ne 42 || "$program_output" != "OK" ]]; then
    echo "optimized IR executable returned $program_status with output '$program_output'" >&2
    exit 1
fi

"$compiler" -O3 -c "$source_file" -o "$output_dir/program.o" || exit 1
if [[ ! -s "$output_dir/program.o" ]]; then
    echo "object output was not created" >&2
    exit 1
fi
"$clang" "$output_dir/program.o" -o "$output_dir/from-object" || exit 1
object_output=$("$output_dir/from-object")
object_status=$?
if [[ $object_status -ne 42 || "$object_output" != "OK" ]]; then
    echo "object executable returned $object_status with output '$object_output'" >&2
    exit 1
fi

"$compiler" -g --emit-llvm "$source_file" -o "$output_dir/debug.ll" || exit 1
if ! grep -q '!DICompileUnit' "$output_dir/debug.ll" ||
   ! grep -Eq '!DILocation\(line: [0-9]+, column: [1-9][0-9]*' "$output_dir/debug.ll"; then
    echo "debug IR is missing compile-unit or line:column metadata" >&2
    exit 1
fi

"$compiler" -g -c "$source_file" -o "$output_dir/debug.o" || exit 1
if [[ ! -s "$output_dir/debug.o" ]]; then
    echo "debug object file is empty" >&2
    exit 1
fi
