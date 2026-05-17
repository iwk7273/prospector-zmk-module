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

- Keyboard advertisement implementation lives under `src/status_advertisement.c` and related headers in `include/zmk/`.
- Scanner shield implementations:
  - `boards/shields/prospector_scanner/` — upstream-compatible multi-keyboard scanner (Field / Operator / Radii layouts, swipe nav, settings screens).
  - `boards/shields/prospector_meteorite/` — dedicated single-keyboard build for Meteorite40 with a fixed Meteorite layout, host USB CDC rate-limit ingress, no touch nav. Phase 2 may extend the ADV protocol with a v2 frame (layer name list + Meteorite custom config); when it does, keep `prospector_scanner` working against the legacy v1 path.
- Keep protocol changes compatible with both keyboard and scanner configs, and update both consuming manifests/tests when changing the module branch or protocol.
