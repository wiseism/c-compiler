#!/usr/bin/env bash
set -u

python=$1
minimizer=$2
input=$3
predicate=$4
output=$5

"$python" "$minimizer" -o "$output" "$input" -- bash "$predicate" '{input}' || exit 1
if ! grep -q 'TRIGGER_FAILURE' "$output"; then
    echo "minimizer removed the interesting failure" >&2
    exit 1
fi
if [[ $(wc -l <"$output") -ge $(wc -l <"$input") ]]; then
    echo "minimizer did not reduce its input" >&2
    exit 1
fi
