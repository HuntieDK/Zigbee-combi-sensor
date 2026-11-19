#!/bin/sh
# Build and run host-side unit tests (no ESP-IDF needed).
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/combi_host_tests"
mkdir -p "$OUT"
gcc -Wall -Wextra -Werror -o "$OUT/test_pir_debounce" test_pir_debounce.c ../../main/pir_debounce.c
"$OUT/test_pir_debounce"
