#!/usr/bin/env bash
set -u

api_compiler=$1
text_compiler=$2
clang=$3
fixtures=$4
output_dir=$5
mkdir -p "$output_dir"

run_backend() {
    local compiler=$1
    local backend=$2
    local case_name=$3
    "$compiler" "$fixtures/$case_name.lm" -o "$output_dir/$case_name.$backend.ll" || return 1
    "$clang" "$output_dir/$case_name.$backend.ll" \
        -o "$output_dir/$case_name.$backend.exe" || return 1
    "$output_dir/$case_name.$backend.exe" >"$output_dir/$case_name.$backend.out"
    printf '%s\n' "$?" >"$output_dir/$case_name.$backend.status"
}

for case_name in program forward_call void_globals_fold pointers_arrays; do
    run_backend "$api_compiler" api "$case_name" || exit 1
    run_backend "$text_compiler" text "$case_name" || exit 1
    if ! cmp -s "$output_dir/$case_name.api.out" "$output_dir/$case_name.text.out" ||
       ! cmp -s "$output_dir/$case_name.api.status" "$output_dir/$case_name.text.status"; then
        echo "backend behavior differs for $case_name" >&2
        diff -u "$output_dir/$case_name.api.out" "$output_dir/$case_name.text.out" >&2 || true
        diff -u "$output_dir/$case_name.api.status" \
            "$output_dir/$case_name.text.status" >&2 || true
        exit 1
    fi
done
