"""One-shot helper: render 24x24 1bpp rate-source icons as C arrays.

Run this manually whenever the icon designs need to change, then paste the
output into meteorite_layout.c. Not built by the firmware itself; kept here
so the source-of-truth pixel patterns live next to the rendered bytes.

Patterns are ASCII art: '#' = pixel on, anything else = off.
Each row must be exactly 24 chars; exactly 24 rows.
"""

# Anthropic Sona — 8-ray asterisk. Reference: claude.ai favicon / Anthropic
# brand mark. Rendered as orthogonal cross + diagonal X, thick strokes.
CLAUDE = [
    "...........###..........",
    "...........###..........",
    "...........###..........",
    "...........###..........",
    "..#........###........#.",
    "...##......###......##..",
    "....##.....###.....##...",
    ".....##....###....##....",
    "......##...###...##.....",
    ".......##..###..##......",
    "........##.###.##.......",
    "########################",
    "########################",
    "........##.###.##.......",
    ".......##..###..##......",
    "......##...###...##.....",
    ".....##....###....##....",
    "....##.....###.....##...",
    "...##......###......##..",
    "..#........###........#.",
    "...........###..........",
    "...........###..........",
    "...........###..........",
    "...........###..........",
]

# OpenAI / ChatGPT mark — hexagonal "knot" outline. We approximate with a
# bold hexagon ring and a small inner notch (the central twist), since a
# faithful knot would be illegible at 24x24 1bpp.
CODEX = [
    "........########........",
    "......####....####......",
    "....###..........###....",
    "...##..............##...",
    "..##................##..",
    ".##..................##.",
    "##....................##",
    "##....................##",
    "##.......######.......##",
    "##......##....##......##",
    "##.....##......##.....##",
    "##.....##......##.....##",
    "##.....##......##.....##",
    "##.....##......##.....##",
    "##......##....##......##",
    "##.......######.......##",
    "##....................##",
    "##....................##",
    ".##..................##.",
    "..##................##..",
    "...##..............##...",
    "....###..........###....",
    "......####....####......",
    "........########........",
]


def render(name: str, rows):
    assert len(rows) == 24, f"{name}: need 24 rows, got {len(rows)}"
    bytes_out = []
    for y, row in enumerate(rows):
        assert len(row) == 24, f"{name} row {y}: need 24 cols, got {len(row)}"
        for byte_idx in range(3):
            b = 0
            for bit in range(8):
                col = byte_idx * 8 + bit
                if row[col] == "#":
                    b |= 1 << (7 - bit)
            bytes_out.append(b)
    return bytes_out


def emit_c(name: str, rows):
    data = render(name, rows)
    print(f"static const uint8_t rate_icon_{name}_map[] = {{")
    for y, row in enumerate(rows):
        triplet = data[y * 3:(y + 1) * 3]
        hex_str = ", ".join(f"0x{b:02X}" for b in triplet)
        # show the visual pattern in a trailing comment for review
        visual = row.replace(".", " ")
        print(f"    {hex_str},  /* {visual} */")
    print("};")
    print()


if __name__ == "__main__":
    emit_c("claude", CLAUDE)
    emit_c("codex", CODEX)
