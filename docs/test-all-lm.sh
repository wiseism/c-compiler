#!/usr/bin/env bash
# Compile every Lumen source file and check its expected compiler status.
set -uo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd "$script_dir/.." && pwd)
compiler=${1:-"$project_dir/build/lumenc"}
output_dir=${2:-"$project_dir/.lm-test-output"}

if [[ ! -x "$compiler" ]]; then
    echo "compiler not found or not executable: $compiler" >&2
    echo "usage: $0 /path/to/lumenc [output-dir]" >&2
    exit 2
fi

rm -rf "$output_dir"
mkdir -p "$output_dir"

# These fixtures intentionally contain syntax or semantic errors.
is_expected_failure() {
    case "$1" in
        constant_bounds_error|duplicate_parameter|missing_return|multiple_semantic_errors|\
        multiple_syntax_errors|nonconstant_global|pointer_type_error|semantic_error|\
        syntax_error|void_value_error|wrong_arity|fuzz-corpus/recovery) return 0 ;;
        *) return 1 ;;
    esac
}

passed=0
failed=0
while IFS= read -r source; do
    relative=${source#"$project_dir/"}
    name=${relative%.lm}
    fixture=${name#tests/}
    output="$output_dir/${name//\//_}.ll"
    log="$output_dir/${name//\//_}.log"
    "$compiler" "$source" -o "$output" >"$log" 2>&1
    status=$?
    should_fail=0
    is_expected_failure "$fixture" && should_fail=1

    if [[ $should_fail -eq 1 && $status -ne 0 ]] ||
       [[ $should_fail -eq 0 && $status -eq 0 ]]; then
        echo "PASS [$relative] (status=$status)"
        passed=$((passed + 1))
    else
        echo "FAIL [$relative] (expected $([[ $should_fail -eq 1 ]] && echo failure || echo success), status=$status)"
        sed -n '1,3p' "$log" >&2
        failed=$((failed + 1))
    fi
done < <(find "$project_dir/tests" "$project_dir/examples" -type f -name '*.lm' -print | sort)

echo "Summary: $passed passed, $failed failed"
exit "$([[ $failed -eq 0 ]] && echo 0 || echo 1)"
