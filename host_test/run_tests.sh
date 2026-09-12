#!/usr/bin/env bash
#
# Host-side unit tests for tinySA-Ultra metrology-critical pure functions.
#
# Technique: the functions under test are extracted VERBATIM from the
# firmware sources at build time (no copies in this directory), compiled
# with host gcc, and executed. If the firmware math changes, the next run
# tests the new code automatically.
#
# Usage:  ./host_test/run_tests.sh          (exit 0 = all tests passed)
#
set -euo pipefail
cd "$(dirname "$0")"
SRC=..
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# ---------------------------------------------------------------- extraction
# defs.inc: real typedefs/macros from nanovna.h + sa_core.c (TINYSA4 branch)
{
  awk '/#ifdef TINYSA4/{f=1} f&&/typedef uint64_t freq_t;/{print; exit}' "$SRC/nanovna.h"
  awk '/typedef int16_t  pureRSSI_t;/{print; exit}'                       "$SRC/nanovna.h"
  grep '^#define float_TO_PURE_RSSI\|^#define PURE_TO_float'              "$SRC/nanovna.h"
  grep '^#define FREQ_SCALE_FACTOR\|^#define SCALE_FACTOR'                "$SRC/sa_core.c"
  echo 'enum {'
  awk '/U_DBM=0, U_DBMV,/{print; exit}'                                   "$SRC/nanovna.h"
  echo '};'
  # CORRECTION_POINTS/SIZE are target-specific (TINYSA3: 10/3, TINYSA4: 20/12)
  # and the #ifdef guards move around, so anchor on the VALUES (unique per
  # target) rather than on guard position. A future value change fails loudly
  # at the non-empty-extraction check / compile step instead of silently
  # testing the wrong target.
  awk '/^ *#define CORRECTION_POINTS +20([^0-9]|$)/{print; exit}' \
                                          "$SRC/nanovna.h"
  awk '/^ *#define CORRECTION_SIZE +12([^0-9]|$)/{print; exit}' \
                                          "$SRC/nanovna.h"
} | tr -d "\r" > "$OUT/defs.inc"

# checksum() from flash.c, value()/to_dBm() from plot.c.
# All three put their return type on the line ABOVE the function name and
# the sources are CRLF, so: emit the known type explicitly, slice the
# function body by name, strip \r. Deterministic regardless of line endings.
{ echo uint32_t; awk '/^checksum\(const void \*start, size_t len\)/{f=1} f{print} f&&/^}$/{exit}' "$SRC/flash.c"; } | tr -d '\r' > "$OUT/checksum_extract.inc"
{ echo float; awk '/^value\(const float v\)/{f=1} f{print} f&&/^}$/{exit}' "$SRC/plot.c"; } | tr -d '\r' > "$OUT/value_extract.inc"
{ echo float; awk '/^to_dBm\(const float v\)/{f=1} f{print} f&&/^}$/{exit}' "$SRC/plot.c"; } | tr -d '\r' > "$OUT/to_dBm_extract.inc"
{ echo float; awk '/^dBm_to_Watt\(const float v\)/{f=1} f{print} f&&/^}$/{exit}' "$SRC/plot.c"; } | tr -d '\r' > "$OUT/dbm_to_watt_extract.inc"

# Linear-interpolation core of get_frequency_correction() from sa_core.c
# (search + clamp + interpolation + divide-by-zero guard), verbatim.
awk '/get_frequency_correction/{f=1} f&&/^  int i = 0;$/{g=1} g{print} g&&/^  return\(cv\);$/{exit}' \
  "$SRC/sa_core.c" > "$OUT/interp_extract.inc"

# calculate_correction() precompute (scaled value/multiplier arrays).
awk '/^static void calculate_correction\(void\)/{f=1} f{print} f&&/^}$/{exit}' \
  "$SRC/sa_core.c" > "$OUT/calc_extract.inc"

# sanity: all extractions non-empty
for f in defs checksum_extract value_extract to_dBm_extract dbm_to_watt_extract interp_extract calc_extract; do
  [ -s "$OUT/$f.inc" ] || { echo "EXTRACTION FAILED: $f.inc is empty"; exit 2; }
done

# ---------------------------------------------------------------- build+run
gcc -std=c11 -DTINYSA4 -DTINYSA_F303 -O1 -Wall -I"$OUT" \
  test_metrology.c -lm -o "$OUT/test_metrology"
"$OUT/test_metrology"