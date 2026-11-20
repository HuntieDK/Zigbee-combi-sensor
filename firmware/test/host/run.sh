#!/bin/sh
# Build and run host-side unit tests (no ESP-IDF needed).
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/combi_host_tests"
mkdir -p "$OUT"
gcc -Wall -Wextra -Werror -o "$OUT/test_pir_debounce" test_pir_debounce.c ../../main/pir_debounce.c
"$OUT/test_pir_debounce"
gcc -Wall -Wextra -Werror -o "$OUT/test_ld2410_proto" test_ld2410_proto.c ../../main/ld2410_proto.c ../../main/presence_filter.c
"$OUT/test_ld2410_proto"
gcc -Wall -Wextra -Werror -o "$OUT/test_button_hold" test_button_hold.c ../../main/button_hold.c
"$OUT/test_button_hold"
