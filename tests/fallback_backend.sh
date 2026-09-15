#!/usr/bin/env bash
set -u

compiler=$1
source_file=$2
output_dir=$3
mkdir -p "$output_dir"

if "$compiler" -O2 "$source_file" -o "$output_dir/optimized.ll" \
    2>"$output_dir/optimization.txt"; then
    echo "textual backend unexpectedly accepted -O2" >&2
    exit 1
fi
if ! grep -Fq -- "-O requires an LLVM development package" \
    "$output_dir/optimization.txt"; then
    echo "textual backend optimization diagnostic was not specific" >&2
    exit 1
fi

if "$compiler" -c "$source_file" -o "$output_dir/program.o" \
    2>"$output_dir/object.txt"; then
    echo "textual backend unexpectedly emitted an object" >&2
    exit 1
fi
if ! grep -Fq "object emission requires an LLVM development package" \
    "$output_dir/object.txt"; then
    echo "textual backend object diagnostic was not specific" >&2
    exit 1
fi
