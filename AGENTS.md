# AGENTS.md

This repository is the user-owned Prospector ZMK module fork used by both Meteorite40 keyboard firmware and Prospector Scanner firmware.

## Repository Role

- `main` is the canonical user-owned module branch.
- `origin` should point to `iwk7273/prospector-zmk-module`.
- `upstream` should point to `t-ogura/prospector-zmk-module` and is used only for upstream tracking.
- The initial baseline is upstream `v2.2.1`.

## Consumers

- `zmk-config-meteorite40/config/west.yml` uses this module for keyboard-side status advertisement.
- `zmk-config-prospector/config/west.yml` uses this module for scanner shield, display, and status protocol code.

## Editing Rules

- Keyboard advertisement implementations:
  - **v1 legacy ADV** (always on): `src/status_advertisement.c` + `include/zmk/status_advertisement.h`. 26-byte payload via legacy `bt_le_adv_*`. Service UUID `0xABCD`. High cadence (sub-second), keyboard state.
  - **v2 multi-frame legacy ADV** (opt-in, `CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT=y`, **does NOT require BT_EXT_ADV** — that flag silences the v1 piggyback path): `src/status_advertisement_v2.c` + `include/zmk/status_advertisement_v2.h`. Three fixed 26-byte frames (config / layers A / layers B) cycled into the same legacy ADV transport as v1. Service UUID `0xABCE`. Shares the 4-byte keyboard_id with v1 so scanners can correlate the two streams. v2 takes ~1 out of every `CONFIG_PROSPECTOR_STATUS_ADV_V2_INTERVAL` ticks (default 10), so v1 keeps the dominant share of the channel.
- v2 hooks API (`include/zmk/prospector_v2_hooks.h`): weak getters for keyboard-specific custom config fields (OS mode, CPI, scroll layer 1/2, scroll div). Default getters in `src/prospector_v2_hooks_default.c` return "unknown" sentinels. Real values are provided by `zmk-feature-meteorite-config/src/custom_config/prospector_v2_adapter.c` for the Meteorite40 build.
- Scanner shield implementations:
  - `boards/shields/prospector_scanner/` — upstream-compatible multi-keyboard scanner (Field / Operator / Radii layouts, swipe nav, settings screens). v1 only.
  - `boards/shields/prospector_meteorite/` — dedicated single-keyboard build for Meteorite40 with a fixed Meteorite layout, host USB CDC rate-limit ingress, no touch nav. Enables `CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT=y` for v2 RX. v2 packets ride the existing `scan_callback()` in `src/status_scanner.c` (parses both UUIDs, dispatches to a separate v2 SPSC ring), drained in `boards/shields/prospector_meteorite/src/bootstrap.c::refresh_tick()` which reassembles into the data store. v2 fields age out after 30s.
- **Phase 3a — Host PC rate-limit ingress** (Meteorite shield only):
  - File: `boards/shields/prospector_meteorite/src/host_rate_rx.c` + `.h`.
  - Shares the `zephyr,console` CDC ACM device. Device->host stays as Zephyr logs (TX); host->device is parsed line-by-line by a UART IRQ callback, dispatched to `meteorite_data_set_rate_limit()`.
  - Line protocol (LF terminated, CRLF tolerated):
    - `rate <codex|claude> <pct5h> <pctW> <sec5h> <secW>` — pct in 0..100, secs are decimal uint32.
    - `ping`   -> `pong meteorite v=1`
    - `hello`  -> `hello meteorite-rate v1`
    - Unknown / malformed lines: silent drop.
  - Replies are dispatched via the system workqueue so the IRQ never blocks on `printk`'s poll-out path.
  - Discovery from the host side: VID/PID `0x2886/0x802F` + a `hello` round-trip.
- Keep protocol changes compatible with both keyboard and scanner configs, and update both consuming manifests/tests when changing the module branch or protocol.
