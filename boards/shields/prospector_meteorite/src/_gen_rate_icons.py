"""One-shot helper: render 1bpp icons for meteorite_layout.c as C arrays.

Run this manually whenever an icon needs to change, then paste the output
into meteorite_layout.c. Not built by the firmware itself.

Generates four icons:

  rate_icon_claude  — Anthropic Sona burst: 4 cardinal "spindle" petals +
                      4 thinner diagonal petals (procedural, 32x32)
  rate_icon_codex   — ChatGPT-style 6-lobe knot: six overlapping circles
                      around a hex-shaped void (procedural, 32x32)
  os_icon_windows   — 4-pane Windows logo (procedural, 24x24)
  os_icon_apple     — Apple silhouette with bite and leaf
                      (hand-drawn pixel art, 24x24 only)

Sizes are wired into the __main__ block at the bottom.
"""

import math


# ============================================================================
# Pattern generators — each returns a list of strings ("#" = on, "." = off)
# ============================================================================

def render_claude_sona(size: int) -> list[str]:
    """Anthropic Sona mark: four parabolic-spindle petals meeting at the
    center, pointing N / E / S / W. Width along each petal is
    pw_max·(1 − t²), so the petals are fattest at the middle and taper
    to zero at the tip — and (after combining all four) the four petals
    sweep through the center as a single chunky cross.

    Diagonal petals were tried (asterisk-burst look) but at 32×32 their
    tips degenerated to isolated dots that read as noise; kept to four
    clean cardinal petals."""
    cx = cy = (size - 1) / 2
    plen   = size * 0.47
    pw_max = size * 0.13
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            dx = x - cx
            dy = y - cy
            on = False
            for angle_deg in (0, 90):
                rad = math.radians(angle_deg)
                axis = dx * math.cos(rad) + dy * math.sin(rad)
                perp = dx * math.sin(rad) - dy * math.cos(rad)
                if abs(axis) <= plen:
                    t = abs(axis) / plen
                    local_w = pw_max * (1.0 - t * t)
                    if abs(perp) <= local_w:
                        on = True
                        break
            row.append("#" if on else ".")
        rows.append("".join(row))
    return rows


def render_codex_knot(size: int) -> list[str]:
    """ChatGPT-style 6-lobe knot: six overlapping circles placed at 60°
    intervals around the center, with a small central void. Adjacent
    circles overlap (petal_dist < 2*petal_r) so the silhouette reads as
    a single 6-fold flower/knot rather than six isolated dots.

    One petal points straight up (90° rotation), matching the canonical
    orientation of the OpenAI/ChatGPT mark."""
    cx = cy = (size - 1) / 2
    petal_r       = size * 0.22
    petal_dist    = size * 0.28
    inner_void_r  = size * 0.10
    centers = []
    for i in range(6):
        angle = math.radians(i * 60 + 90)
        centers.append((petal_dist * math.cos(angle),
                        petal_dist * math.sin(angle)))
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            dx = x - cx
            dy = y - cy
            on = False
            for px, py in centers:
                pdx = dx - px
                pdy = dy - py
                if pdx * pdx + pdy * pdy <= petal_r * petal_r:
                    on = True
                    break
            if on and (dx * dx + dy * dy) < inner_void_r * inner_void_r:
                on = False
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


# Hand-drawn 24x24 Apple silhouette. Procedural ellipse + bite math at
# this size could not reliably keep the bite, the leaf, and the round
# body all recognizable at once, so the pattern is laid out by hand.
APPLE_24 = [
    "........................",
    ".............##.........",
    "............####........",
    "...........#####........",
    "..........####..........",
    ".........####...........",
    "........####....##......",
    ".......######..####.....",
    ".....##########.####....",
    "....################....",
    "...##################...",
    "..####################..",
    "..####################..",
    "..####################..",
    "..####################..",
    "..####################..",
    "..####################..",
    "...##################...",
    "....################....",
    ".....##############.....",
    "......############......",
    ".......##########.......",
    "........########........",
    ".........######.........",
]


def render_os_apple(size: int) -> list[str]:
    """Hand-drawn pixel art — only 24x24 is defined. See APPLE_24."""
    assert size == 24, "os_icon_apple is hand-drawn at 24x24 only"
    return APPLE_24


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
    emit_c("rate_icon_claude", render_claude_sona(32), 32)
    emit_c("rate_icon_codex",  render_codex_knot(32),  32)
    emit_c("os_icon_windows",  render_os_win(24),      24)
    emit_c("os_icon_apple",    render_os_apple(24),    24)
