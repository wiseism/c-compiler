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

if "$compiler" -g "$source_file" -o "$output_dir/debug.ll" \
    2>"$output_dir/debug.txt"; then
    echo "textual backend unexpectedly accepted -g" >&2
    exit 1
fi
if ! grep -Fq -- "-g requires an LLVM development package" \
    "$output_dir/debug.txt"; then
    echo "textual backend debug-info diagnostic was not specific" >&2
    exit 1
fi

if "$compiler" --save-pass-ir "$output_dir/pass-ir" "$source_file" \
    -o "$output_dir/snapshot.ll" 2>"$output_dir/snapshot.txt"; then
    echo "textual backend unexpectedly accepted --save-pass-ir" >&2
    exit 1
fi
if ! grep -Fq -- "--save-pass-ir requires an LLVM development package" \
    "$output_dir/snapshot.txt"; then
    echo "textual backend snapshot diagnostic was not specific" >&2
    exit 1
fi
