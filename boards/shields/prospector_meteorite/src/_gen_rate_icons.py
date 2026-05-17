"""One-shot helper: render 1bpp icons for meteorite_layout.c as C arrays.

Run this manually whenever an icon needs to change, then paste the output
into meteorite_layout.c. Not built by the firmware itself.

Generates four icons procedurally so they can be re-rendered at any size:

  rate_icon_claude  — 8-ray Anthropic Sona asterisk (rate-source label)
  rate_icon_codex   — 6-petal ChatGPT-style rose curve (rate-source label)
  os_icon_windows   — 4-pane Windows logo (OS-mode indicator)
  os_icon_apple     — Apple silhouette with leaf + bite (OS-mode indicator)

Sizes are wired into the __main__ block at the bottom — change them there.
"""

import math


# ============================================================================
# Pattern generators — each returns a list of strings ("#" = on, "." = off)
# ============================================================================

def render_claude_asterisk(size: int) -> list[str]:
    """Anthropic 8-ray asterisk: 4 axes (horizontal, vertical, both
    diagonals) drawn as constant-thickness strips through the center,
    plus a full-width 2-row horizontal stroke that gives the mark its
    distinctive "weighted middle" look. Thickness scales with size."""
    cx = cy = (size - 1) / 2
    thickness = size * 1.5 / 24.0       # ~4-px-wide arms at size=32
    band_half = max(0.6, size / 24.0)   # 2-row solid center band
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            dx = x - cx
            dy = y - cy
            on = abs(dy) <= band_half
            if not on:
                for angle_deg in (0, 45, 90, 135):
                    rad = math.radians(angle_deg)
                    perp = abs(dx * math.sin(rad) - dy * math.cos(rad))
                    if perp <= thickness:
                        on = True
                        break
            row.append("#" if on else ".")
        rows.append("".join(row))
    return rows


def render_codex_rose(size: int) -> list[str]:
    """6-lobe rose curve evoking the ChatGPT hexagonal knot. Filled lobes
    (r = inner_void .. inner + amp*|cos(3θ)|) — cleaner at 32x32 than the
    earlier hollow-outline variant which left noise in the inter-lobe
    valleys. One petal points straight up."""
    cx = cy = (size - 1) / 2
    inner_r    = size * 3.5 / 24.0
    amp        = size * 7.0 / 24.0
    inner_void = size * 2.5 / 24.0
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            dx = x - cx
            dy = y - cy
            r = math.hypot(dx, dy)
            theta = math.atan2(dy, dx)
            target_r = inner_r + amp * abs(math.cos(3 * theta + math.pi / 2))
            on = (inner_void <= r <= target_r)
            row.append("#" if on else ".")
        rows.append("".join(row))
    return rows


def render_os_win(size: int) -> list[str]:
    """4-pane Windows logo — 2x2 grid of equal squares with a centered
    cross-shaped gap. Each pane is roughly (size-gap)/2 wide."""
    gap = max(1, size // 12)
    half = (size - gap) // 2
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            in_gap_row = (half <= y < half + gap)
            in_gap_col = (half <= x < half + gap)
            row.append("." if (in_gap_row or in_gap_col) else "#")
        rows.append("".join(row))
    return rows


def render_os_apple(size: int) -> list[str]:
    """Apple silhouette: elliptical body, small circular bite at the
    upper-right, single leaf above. All sized relative to `size` so the
    same generator works at 16, 24, 32..."""
    cx = (size - 1) / 2
    cy = (size - 1) / 2 + size * 0.06
    rx = size * 0.40
    ry = size * 0.44
    bite_cx = cx + size * 0.36
    bite_cy = cy - size * 0.25
    bite_r  = size * 0.18
    leaf_cx = cx + size * 0.10
    leaf_cy = cy - ry - size * 0.05
    leaf_rx = size * 0.07
    leaf_ry = size * 0.10
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            ndx = (x - cx) / rx
            ndy = (y - cy) / ry
            in_body = ndx * ndx + ndy * ndy <= 1.0
            bdx = x - bite_cx
            bdy = y - bite_cy
            in_bite = bdx * bdx + bdy * bdy <= bite_r * bite_r
            ldx = (x - leaf_cx) / leaf_rx
            ldy = (y - leaf_cy) / leaf_ry
            in_leaf = ldx * ldx + ldy * ldy <= 1.0
            on = (in_body and not in_bite) or in_leaf
            row.append("#" if on else ".")
        rows.append("".join(row))
    return rows


# ============================================================================
# Render helpers
# ============================================================================

def emit_c(name: str, rows: list[str], size: int) -> None:
    """Emit C uint8_t array for a 1bpp pattern (MSB-first, row-padded to
    a whole-byte stride)."""
    stride = (size + 7) // 8
    print(f"/* {name}: {size}x{size} 1bpp, stride={stride} */")
    print(f"static const uint8_t {name}_map[] = {{")
    for y, row in enumerate(rows):
        assert len(row) == size, f"{name} row {y}: {len(row)} != {size}"
        bytes_for_row = []
        for byte_idx in range(stride):
            b = 0
            for bit in range(8):
                col = byte_idx * 8 + bit
                if col >= size:
                    break
                if row[col] == "#":
                    b |= 1 << (7 - bit)
            bytes_for_row.append(b)
        hex_str = ", ".join(f"0x{b:02X}" for b in bytes_for_row)
        visual = row.replace(".", " ")
        print(f"    {hex_str},  /* {visual} */")
    print("};")
    print()


if __name__ == "__main__":
    # Rate-source icons enlarged to 32x32 (was 24x24).
    emit_c("rate_icon_claude", render_claude_asterisk(32), 32)
    emit_c("rate_icon_codex",  render_codex_rose(32),      32)
    # OS-mode icons enlarged to 24x24 (was 16x16) so they read as
    # primary status indicators next to the BLE/USB pills.
    emit_c("os_icon_windows",  render_os_win(24),          24)
    emit_c("os_icon_apple",    render_os_apple(24),        24)
