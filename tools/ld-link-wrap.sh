#!/bin/sh
# Display-only link wrapper (wired via the Makefile LD override; no ChibiOS
# file is touched). Runs the real link, saving ld's --print-memory-usage
# table (emitted on stdout) to <output>.mem for the build summary, and
# hiding the zero-length placeholder regions (flash1-6, ram1-3/5-7: len = 0
# in STM32F303xC.ld) from the console table. Stdout is replayed (so the
# "Creating ..." chain and size table still display), all other stderr (real
# warnings/errors) passes through, and the exit code is preserved. No flags,
# objects, or layout change: the image is unaffected.
REAL="$1"
shift
OUT=""
PREV=""
for A in "$@"; do
  if [ "$PREV" = "-o" ]; then
    OUT="$A"
  fi
  PREV="$A"
done
TMP_OUT="${TMPDIR:-/tmp}/ldwrap.$$.out"
TMP_ERR="${TMPDIR:-/tmp}/ldwrap.$$.err"
"$REAL" "$@" >"$TMP_OUT" 2>"$TMP_ERR"
RC=$?
if [ -n "$OUT" ]; then
  grep -E "^[ ]+(flash|ram)[0-9]+[ ]*:" "$TMP_OUT" > "$OUT.mem" 2>/dev/null || true
fi
CR=$(printf 'X\rX')
CR=${CR#X}
CR=${CR%X}
tr -d "$CR" < "$TMP_OUT" | grep -v -E ":[ ]*0 B[ ]+0 B[ ]*$"
tr -d "$CR" < "$TMP_ERR" | grep -v -E ":[ ]*0 B[ ]+0 B[ ]*$" >&2
rm -f "$TMP_OUT" "$TMP_ERR"
exit "$RC"

