#!/usr/bin/env bash
set -u

compiler=$1
clang=$2
identity_source=$3
guard_source=$4
output_dir=$5
mkdir -p "$output_dir"

"$compiler" -O0 "$identity_source" -o "$output_dir/identity.O0.ll" || exit 1
"$compiler" -O1 --save-pass-ir "$output_dir/identity-passes" \
    "$identity_source" -o "$output_dir/identity.O1.ll" || exit 1
identity_snapshot=$(find "$output_dir/identity-passes" -name '*-algebraic_identity.ll' | head -1)
if [[ -z "$identity_snapshot" ]] || grep -Eq ' = (add .* 0|mul .* 1)' "$identity_snapshot"; then
    echo "algebraic identity pass did not remove x + 0 and x * 1" >&2
    exit 1
fi

"$compiler" -O1 --save-pass-ir "$output_dir/guard-passes" \
    "$guard_source" -o "$output_dir/guard.O1.ll" || exit 1
guard_snapshot=$(find "$output_dir/guard-passes" -name '*-algebraic_identity.ll' | head -1)
if [[ -z "$guard_snapshot" ]] || ! grep -Eq ' = (add .* 1|mul .* 2)' "$guard_snapshot"; then
    echo "algebraic identity pass removed a non-identity operation" >&2
    exit 1
fi

for level in O0 O1; do
    "$clang" "$output_dir/identity.$level.ll" -o "$output_dir/identity.$level" || exit 1
    "$output_dir/identity.$level"
    status=$?
    if [[ $status -ne 42 ]]; then
        echo "identity program at $level returned $status, expected 42" >&2
        exit 1
    fi
done

"$clang" "$output_dir/guard.O1.ll" -o "$output_dir/guard.O1" || exit 1
"$output_dir/guard.O1"
guard_status=$?
if [[ $guard_status -ne 42 ]]; then
    echo "guard program returned $guard_status, expected 42" >&2
    exit 1
fi
