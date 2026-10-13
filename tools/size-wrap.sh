#!/bin/sh
# Display-only wrapper for $(SZ): strips CRs and expands TABs to spaces so
# the text/data/bss table pastes cleanly (no ^I / glyph rendering in
# terminals). Values and the exit code are preserved; the image is
# unaffected.
REAL="$1"
shift
OUT="$("$REAL" "$@")"
RC=$?
CR=$(printf 'X\rX')
CR=${CR#X}
CR=${CR%X}
printf '%s\n' "$OUT" | tr -d "$CR" | expand
exit "$RC"

