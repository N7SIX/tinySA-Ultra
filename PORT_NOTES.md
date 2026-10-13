# Port Notes: NanoVNA-D / upstream patches

Status of the feature ports from the upstream patch set (temp files `patch_609b094.txt`,
`patch_641899e.txt`, `patch_650f538.txt`, `patch_d32b6ca.txt`, `patch_ea71dfd.txt`)
applied to this fork. **All six patches are now ported and the firmware builds** (see
"Build system" below); USB MSC and firmware self-update were completed by syncing with
`erikkaashoek/TinySA` upstream sources.

## Build system (restored 2026-10-13)

- `ChibiOS/` was empty; it is now a git checkout of `edy555/ChibiOS` at commit
  `0cfe7f528` — **the exact revision pinned by upstream `erikkaashoek/TinySA`**
  (2026-09-26, "Backport CDC zero length packet fix from ChibiOS 21.11"). The 2019
  branch tip (`669d4bbc8`) predates the USB-stack changes that `usb_msc.c`/`usbcfg.c`
  depend on; the pin carries updated `hal_usb_lld`/`hal_serial_usb`/queue APIs.
  One fork fix was needed for it: `main.c shell_reset_console()` uses `qResetI()`
  instead of the removed `oqResetI()`/`iqResetI()` (same fix as upstream).
- Build with **Git Bash + mingw32-make** (the Makefile needs a POSIX shell):
  `mingw32-make TARGET=F303 -j8` → `build/tinySA4_N7SIX_v7.6.*` (bin/hex/elf).
  `arm-none-eabi-gcc` 14.3 and WinLibs `mingw32-make` are on PATH.
- The project is **not a git checkout**, so the Makefile's `git rev-list` version
  suffix comes out empty (`v7.6..`); functional, cosmetic only.
- The F072/TINYSA3 target does not link (RAM overflows 16 KB by >16 KB) —
  **pre-existing**, this N7SIX fork is F303-only in practice. All MSC/fw code is
  `TINYSA4`-guarded and adds nothing to F072 builds.

## F303 build result (2026-10-13, arm-none-eabi-gcc 14.3)

| Region | Used | Size | % |
|---|---|---|---|
| flash0 | 198,920 B | 240 KB | 80.9 |
| ram0 (SRAM) | 40,536 B | 40 KB | ~99.0 (~424 B heap left) |
| ram4 (CCM) | 4,540 B | 8 KB | 55.4 (fw_update flasher code+data) |
| flash7 (caldata) | — | 16 KB | reserved |

`usb_msc.c`/`fw_update.c` compile warning-free. Firmware image layout verified:
`.fwinfo` header (magic `tinySAFW`, model 4, version string) directly after the vector
table at 0x080001A0, `.fwtrailer` last in flash0, `__fw_image_size__` == `.bin` size
(198,920 B). Linker ASSERTs guarantee `SAVE_CONFIG_ADDR` (0x0803C000) == end of flash0
and that the trailer is the last data in flash.

## Completed

### 1. RLE8 screen capture (`__CAPTURE_RLE8__`, enabled in `nanovna.h`)
- `ili9341.c`: split `ili9341_read_memory()` into a DMA-stream triple
  `ili9341_read_memory_start/_next/_end` (ST7796S reads one continuous memory window);
  old function is now a wrapper, so existing callers are unchanged.
- `nanovna.h`: feature define + declarations (`capture_verify`, `capture_palette_add`,
  `plot_capture_colors`, read-memory stream prototypes under `TINYSA4`).
- `main.c`: `capture_read_row()` ping-pong helper (next row DMA-read while the caller
  streams the previous one), PackBits encoder (DiSlord NanoVNA-D `common.c` algorithm),
  sorted 256-entry palette, `capture_rle8()` emitting the NanoVNA-D RLE8 stream
  (`"BM"` header, palette, per-row `u16 size + packed indices`), and the `cmd_capture`
  dispatch: **any argument** sends RLE8, no argument keeps the raw 16-bit stream.
  Two-pass verify when the screen shows colours outside the predicted palette
  (image viewer, display test): pass 0 collects colours, pass 1 sends.
- `plot.c`: `plot_capture_colors()` pre-seeds the palette with waterfall colours and all
  OR-combinations of enabled trace colours over `below_traces` (bg/ham/grid/trigger/
  grid-value) because `draw_cell()` ORs these together; `display_test()` sets
  `capture_verify` (its pattern is outside any palette).
- Fallbacks: palette overflow in pass 0 → raw capture; overflow detected in pass 1 →
  next capture runs two passes; palette full mid-pass → raw capture.
- Buffer math: needs `256 + 2*480 = 1216` of `SPI_BUFFER_SIZE = 2048` pixels, plus a
  `_Static_assert` against the FatFs overlay (`fs_volume`/`fs_file` at the end of
  `spi_buffer`). Estimated palette load ≈ 231/256 worst case (124 unique waterfall +
  32 config palette + trace OR-combos, heavy overlap), raw fallback covers the rest.
- Raw `cmd_capture` (no argument) on TINYSA4 now also uses the DMA row stream.

### 2. Keypad preset default + long-press erase (`ui.c`)
- `ui_preset_keypad_text()` presets `kp_buf` so a single ENTER accepts the default
  (used when storing a preset back to SD: pre-fills the loaded `sd_preset_path`).
- `keypad_text_preset` is consumed only when the keypad is really shown
  (`!in_menu_command`), so a remote menu invoke does not silently accept the default.
- Long press (≥ 500 ms, `BUTTON_DOWN_LONG_TICKS`) on ENTER erases the whole input and
  stays in the keypad; a lever click (`EVT_BUTTON_SINGLE_CLICK`) never counts.

### 3. SD preset round-trip
- `vna_browser.c` remembers `sd_preset_path` when a `.prs` preset is loaded;
  `flash.c` (`reset_preset`) and `sa_core.c` (mode change) clear it, so the default only
  applies while the active preset actually came from SD.
- `ui.c` `sa_save_file()`: for `FMT_PRS_FILE` with a known SD path, the keypad is
  pre-filled with that path (extension stripped, keypad re-adds it).

### 4. Units: dBV added (`nanovna.h`, `main.c`, …)
- `enum unit_type` extended with `U_DBV` before `U_DBC`; `MAX_UNIT_TYPE 8`.
  Existing values are unchanged (they are persisted in flash presets / `.prs` files and
  must not be renumbered — `U_DBC` stays the "delta only" sentinel).
- `cmd_trace`/`cmd_type` accept `dBV` in the unit list (`dBm|dBmV|dBuV|RAW|V|Vpp|W|dBV`).

### 5. USB MSC mass-storage mode (`__USE_USB_MSC__`, enabled for TINYSA4)
- `usb_msc.c/h` copied verbatim from upstream (Bulk-Only Transport + SCSI, runs in its
  own thread, owns SPI1/`spi_buffer` only while the medium is ready; outside USB DISK
  mode the LUN reports "medium not present" so the composite device never re-enumerates).
- `usbcfg.c` synced verbatim with upstream: composite CDC+MSC descriptors (IAD,
  interface 2, endpoints 3 IN/OUT), event handling (RESET/CONFIGURED incl. the
  SET_CONFIGURATION(0) packet-memory re-arm), `requests_hook` dispatching class
  requests by interface. `usbcfg.h` already matched upstream.
- `main.c`: `shell_direct_cmd` flag, `msc_init()` before `usbStart()` (both shell
  variants), sweep-thread hook (`msc_enter_request` → `usb_disk_mode()`), shell
  command `usbdisk [on|off]` (`CMD_DISK_OK`), command table uses `CMD_DISK_OK` for
  `version`/`reset`/`threads`; the shell loop refuses non-`CMD_DISK_OK` commands while
  `msc_disk_mode` is active and suppresses the post-command redraw.
- `ui.c`: `usb_disk_mode()` full-screen mode (waits for eject/touch/button/`usbdisk off`,
  restores MCU clock via `dirty`), `menu_usb_disk_cb`, `USB DISK` menu entry in
  Storage→Config.
- `nanovna.h`: feature define, externs, prototypes, `__USE_SD_CARD__` dependency check.
- Supporting deltas: `SD_SPI_SPEED`/`SD_INIT_SPI_SPEED` moved to `spi.h` (shared with
  `fw_update.c`), `ili9341.c` gained `SD_BlockAddressing()` (upstream has it too).

### 6. `fw_update` firmware self-update (`__SD_FIRMWARE_UPDATE__`, enabled for TINYSA4)
- `fw_update.c/h` copied verbatim from upstream. Header `.fwinfo` after the vectors and
  trailer `.fwtrailer` at the end of the image; the flasher runs from CCM RAM
  (`.ram4_init.fwu_text`, copied by crt1 `__init_ram_areas`) with interrupts off,
  never touches `SAVE_CONFIG_ADDR`+ (settings/calibration/presets survive an update).
- `STM32F303xC.ld` synced with upstream: inlines the code sections (so `.fwinfo`
  directly follows the vectors), includes `rules_stacks.ld`/`rules_data.ld`, adds the
  trailer section and the layout ASSERTs (`__fwu_app_end__` == end of flash0).
- `main.c`: shell command `fwupdate [{file} [flash]]` (dry run without `flash`),
  refuses all other commands while `fwu_active`.
- `ui.c` + `vna_browser.c`: `UPDATE FIRMWARE` menu entry (Storage→Config) opens the SD
  file browser on `*.bin`; selecting a file shows the confirm dialog (installed vs new
  version, CANCEL default), checks the file, then flashes and restarts.
  `FMT_BIN_FILE` conditions extended to `__SD_CARD_DUMP_FIRMWARE__ || __SD_FIRMWARE_UPDATE__`.
- `fw_update.c` refuses to build with `__WATCHDOG__` (the flasher runs ~10 s with
  interrupts off); the fork has the watchdog disabled — keep it that way.

## Deliberately NOT ported

The remaining upstream differences are unrelated feature work in `sa_core.c`/`ui.c`/
`plot.c` (~17 k lines of drift, e.g. F303-specific calibration/UI changes) — this port
targets the six patches only, not a full upstream merge.

## Verification performed
- Full F303 firmware build from clean, twice (before/after feature port), deterministic;
  zero warnings from `usb_msc.c`/`fw_update.c`.
- `objdump`/`nm` checks: `.fwinfo`/`.fwtrailer` placement and ASSERTs, `__fw_image_size__`
  == bin size, `fw_header` content (magic/model/version), MSC thread and `fwu_*` symbols
  present, CCM code resident in `.ram4_init`.
- Symbol cross-checks for all earlier ports (declared in `nanovna.h`, defined once,
  every use resolves).
- On-device validation still pending: USB enumeration as composite CDC+MSC, `usbdisk on`
  from shell and menu, SD read/write from PC, eject, `fwupdate` dry run and a real
  install (keep DFU access available for the first flash back if anything is wrong).
