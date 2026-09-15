#!/usr/bin/env bash
set -u

if grep -q 'TRIGGER_FAILURE' "$1"; then
    exit 1
fi
exit 0
