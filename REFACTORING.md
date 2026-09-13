# tinySA-Ultra — Refactoring Assessment & Roadmap

> Data-driven assessment of the codebase (measured September 13, 2026,
> after the v7.6.39 release work) with a risk-ordered plan for future
> cleanup. **Verdict: targeted refactoring, not a rewrite.**

---

## 1. Measured state of the codebase

| Metric | Value | Assessment |
|---|---|---|
| App code (excl. fonts/radio tables) | ~30k lines: `sa_core.c` 8.7k, `ui.c` 8.6k, `main.c` 3.6k, `plot.c` 2.5k, `nanovna.h` 2.0k, `sa_cmd.c` 1.5k | Monolithic |
| Extern globals declared in `nanovna.h` | **153** | Global-state hub |
| `setting` struct accesses | **1,257×** in `sa_core.c`, **482×** in `ui.c` | Deep coupling |
| `#ifdef` density | `sa_core.c` 428, `ui.c` 349, `main.c` 143, `plot.c` 114 | Dual-target (TINYSA3/TINYSA4) branching interleaved |
| Largest function (`draw_cal_status()` in `ui.c`) | **412 lines** | 3 responsibilities in one function |
| Dead `#if 0` blocks | **163** | Accumulated cruft |
| Stale TODO/FIXME/HACK | 23 (e.g. `main.c:1319` — comment says "should be … -7", value is 0) | Misleading comments |
| SD/FatFs diskio location | was 30 functions inside `ili9341.c` → **fixed: now `sd_card.c`** | ✅ resolved |
| Style enforcement | `.clang-format` exists but unused (`if(b){` ×45 vs `if (b){` ×45) | Format not enforced |
| CI coverage | was `make` only (F072) → **fixed: builds F072 + F303** | ✅ resolved |
| Test coverage | `host_test/` (pure metrology functions only); no UI/sweep tests | Near zero |

### What argues against aggressive refactoring

1. **Working embedded firmware.** Macro-generated menu callbacks, the giant
   dispatch switch, and global `setting` state are idiomatic for
   RAM-constrained bare-metal code — and shipping.
2. **No hardware-in-the-loop.** A structural rewrite of `sa_core.c` or the
   menu system can only be validated by hand on a physical unit.
3. **Already improving.** Zone-ownership contracts, self-documenting layout
   constants (`PLOT_RIGHT_EDGE`, `BOTTOM_LEVEL_SPACE`, `SD_BATT_ZONE_Y`),
   host tests, and a warning-clean build were added in v7.6.39.

---

## 2. Roadmap (risk-ordered)

| # | Item | Status | Risk |
|---|---|---|---|
| 1 | Fix CI: build **both** `TARGET=F072` and `TARGET=F303` on every push | ✅ **Done** (`.circleci/config.yml`) | None |
| 2 | Extract SD/FatFs diskio from `ili9341.c` into `sd_card.c` (+`sd_card.h` shared SPI1 interface) | ✅ **Done** — code-size neutral on both targets | Low |
| 3 | Split `draw_cal_status()` (~412 lines) along the zone-ownership lines (status text column vs. widget zone) | ⬜ Open | Low–Medium |
| 4 | Prune dead code: 163 `#if 0` blocks; fix stale comments that contradict values (harmonic-offset "-7" comment) | ⬜ Open | Low |
| 5 | Tame the 400+ `#ifdef`s by moving TINYSA3/TINYSA4 differences behind per-target driver modules, one subsystem at a time (Si4432/Si4468 is already naturally separated) | ⬜ Long-term | High |

**Ground rules** (why the order): nothing structural lands before CI covers
the F303 build; moves are verbatim extraction verified by `make` on both
targets; no behavior change in the same commit as a move.

---

*Baseline sizes after the SD extraction: F072 text+data = 117,792 + 828,
F303 text+data = 198,824 + 4,132 (both targets build warning-clean).*
