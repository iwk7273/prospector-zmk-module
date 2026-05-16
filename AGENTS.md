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
- Scanner shield implementation lives under `boards/shields/prospector_scanner/`.
- Keep protocol changes compatible with both keyboard and scanner configs, and update both consuming manifests/tests when changing the module branch or protocol.
