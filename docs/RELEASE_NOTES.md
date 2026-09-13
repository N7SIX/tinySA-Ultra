# tinySA-Ultra — Release Notes

**Version:** `v7.6.39.eebbd64` (firmware image: `tinySA4_N7SIX_v7.6.39.eebbd64`)
**Release window:** September 12–13, 2026
**Commits:** 26 · **Branch:** `main`
**Default target:** `STM32F303` (Ultra) · `TARGET=F072` for original tinySA

---

## Overview

This release is focused on three fronts: **fixing display corruption** of the
SD/battery/voltage status cluster, **boosting LCD render and sweep
performance**, and **hardening the engineering toolchain** (host-side metrology
tests, reproducible builds, warning-clean compilation). A full status-column
UI refresh and several serial-shell bug fixes round out the set.

---

## ✨ New Features

- **Host-side metrology test suite** (`host_test/`).
  `./host_test/run_tests.sh` extracts the metrology-critical pure functions
  **verbatim** from the firmware sources at build time and runs them on the
  host (no duplicated math — if the firmware changes, the next run tests the
  new code automatically). Coverage: flash-config `checksum()` rotation
  property, RSSI (pureRSSI) scaling round-trips, `get_frequency_correction()`
  interpolation/clamp/div-by-zero guard, and all unit conversions
  (dBm/dBµV/dBmV/dBV/V/Vpp/W). **482 checks, 0 failures.**
- **Opt-in LCD draw-time instrumentation.** When compiled with `-D__DRAW_TIME__`,
  a `drawtime on|off` shell command reports last/max frame render time and
  cells drawn — zero overhead in release builds.
- **Status column refresh:**
  - Short commit hash now displayed under the version line.
  - All status-footer rows (flags, version/hash, HW version, RTC date/time)
    are horizontally centered in the status column.
  - 12-hour RTC clock split across two centered rows (`HH:MM` / `AM·PM`).
  - SD/battery/voltage cluster bottom-aligned in the corner of the status
    column; the battery voltage readout aligns with the bottom frequency row.

---

## 🐛 Bug Fixes

### Display / UI
- **Fixed: SD/battery/voltage cluster painted over during self-test.**
  Full-area redraws (`REDRAW_AREA`) from the self-test path left grid lines
  across the widgets; the cluster is now always repainted on top.
- **Fixed: periodic "blink" of the SD/battery/voltage area.**
  Each widget is fully opaque to its own box, the status column never clears
  the widget zone, and a full section clear + repaint precedes every battery
  draw — no blank intermediate frame, no stale pixels.
- **Fixed: inverted `dBm_to_Watt()` power conversion.**
- **Fixed: bottom plot border not rendered.** `ili9341_fill()` paints with the
  *background* color; the border draw now selects the grid color correctly.
- **Fixed: plot size mismatch.** The grid right-edge was drawn at `x == WIDTH`,
  one pixel off-screen; a new `PLOT_RIGHT_EDGE` constant makes the right
  border visible.
- **Fixed: bottom-of-scale level text left-aligned / overlapping.**
  The level value is now right-aligned in the bottom-right corner using a
  reserved `BOTTOM_LEVEL_SPACE`, and the STOP frequency text shifts left to
  make room.
- **Fixed: RTC readout used the wrong register macros** (`RTC_DR_*` on the
  time register) and displayed a single-line 12h clock that could cross the
  plot border — corrected and split into centered rows.
- **Fixed: OFFSETX = 30 px could not fit the 7-character commit hash.**
  Plot-area left edge moved to 35 px.

### Serial shell / CLI
- **Fixed: command-line argument truncation.** The shell rejected too many
  arguments *after* filling the argv array, risking out-of-bounds access on
  dispatch; rejection now happens at the boundary.
- **Fixed: link error in `shell_reset_console()`** — now uses the correct
  `oqResetI`/`iqResetI` serial hooks.

---

## 🚀 Performance

- **Row-batched LCD cell rendering.** Adjacent dirty cells in a row are
  rendered into consecutive DMA buffers and pushed with a single
  address-window setup instead of one `setWindow` per cell — fewer SPI
  command bytes and CS/DC toggles per frame.
- **Throttled redraws.** Battery refresh capped at **1 Hz** with a 50 mV
  hysteresis (plus color-threshold change detection); frequency text
  recomputed at **2 Hz** and forced on setting changes.
- **`-O2` on render/sweep hot loops** in `plot.c` and `sa_core.c`
  (vectorized cell clears, FPU values kept in registers); measured code
  growth only +8–16 B/function.
- **Fast sweep abort.** `STOP`/pause is serviced every 8th point during long
  sweeps, responding in <50 ms even when the progress bar is hidden.

---

## 🔧 Build System & Dev Experience

- **Version scheme cleanup.** Introduced `SHORT_VERSION`
  (`v7.6.<commit-count>.<short-hash>`) for the LCD and `PROJECT`
  (`tinySA4_N7SIX_<SHORT_VERSION>`) for the firmware image; the
  **Config → Version** screen now shows the full filename-form version.
- **Makefile `TARGET` validation.** Any value other than `F072`/`F303` is a
  hard error instead of silently building F303; default is `F072`.
- **Warning-clean build.** Vendored ChibiOS/CMSIS warnings under GCC-13
  (implicit-fallthrough, deprecated, missing-field-initializers) are silenced
  with scoped `-Wno-*` flags while project code stays at `-Wall -Wextra`.
- **Removed duplicate `clean` target** (ChibiOS `rules.mk` already removes
  `build/`) — removes the make "overriding recipe" warning.
- **Stale `.dep` artifacts removed from git.** Tracked build dependency files
  were causing a phantom `-dirty` suffix in `git describe` and confusing
  dependency tracking.
- **Docker builds fixed.** `git safe.directory` is set inside the container,
  eliminating "dubious ownership" failures with a mounted workspace.

---

## 📁 Changed Files

| Area | Files |
|---|---|
| Display / UI | `plot.c`, `ui.c`, `nanovna.h` |
| Shell / serial | `main.c` |
| Performance | `plot.c`, `sa_core.c` |
| Build system | `Makefile`, `compile-with-docker.sh` |
| Host tests | `host_test/run_tests.sh`, `host_test/test_metrology.c` |
| Cleanup | `.dep/*` (removed) |

---

## ✅ Verification

```
$ ./host_test/run_tests.sh
482 checks, 0 failures - ALL TESTS PASSED
```

Build target `F303`: `make TARGET=F303`

---

*Full commit range: `0ecd898..eebbd64` (2026-09-12 03:44 → 2026-09-13 06:31 UTC).*