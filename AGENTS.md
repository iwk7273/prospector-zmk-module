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
  - **v2 Extended ADV** (opt-in, `CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT=y`, requires `BT_EXT_ADV`): `src/status_advertisement_v2.c` + `include/zmk/status_advertisement_v2.h`. ~60-byte payload via `bt_le_ext_adv_*`. Service UUID `0xABCE`. Shares the 4-byte keyboard_id with v1 so scanners can correlate the two streams. ~1Hz cadence, slow-moving metadata (layer name list + keyboard-specific custom config).
- v2 hooks API (`include/zmk/prospector_v2_hooks.h`): weak getters for keyboard-specific custom config fields (OS mode, CPI, scroll layer 1/2, scroll div). Default getters in `src/prospector_v2_hooks_default.c` return "unknown" sentinels. Real values are provided by `zmk-feature-meteorite-config/src/custom_config/prospector_v2_adapter.c` for the Meteorite40 build.
- Scanner shield implementations:
  - `boards/shields/prospector_scanner/` — upstream-compatible multi-keyboard scanner (Field / Operator / Radii layouts, swipe nav, settings screens). v1 only.
  - `boards/shields/prospector_meteorite/` — dedicated single-keyboard build for Meteorite40 with a fixed Meteorite layout, host USB CDC rate-limit ingress, no touch nav. Enables `CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT=y` for v2 RX. v2 packets ride the existing `scan_callback()` in `src/status_scanner.c` (parses both UUIDs, dispatches to a separate v2 ring buffer), drained in `boards/shields/prospector_meteorite/src/bootstrap.c::refresh_tick()`. v2 fields age out after 30s.
- Keep protocol changes compatible with both keyboard and scanner configs, and update both consuming manifests/tests when changing the module branch or protocol.
