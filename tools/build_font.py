#!/usr/bin/env python3
"""Compile font_source.json into firmware and browser font tables.

    python tools/build_font.py            # build, then print a preview
    python tools/build_font.py --check    # validate only, write nothing

Emits:
    src/font_love.h   inline constexpr tables + binary-search lookup
    web/js/font.js    ES module with the identical column data

Both targets get byte-identical column data, which is what makes the 8x32
canvas preview in the web app pixel-identical to the real matrix.

Column encoding: one byte per column, bit 0 = row 0 = TOP of the display.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tools" / "font_source.json"
HEADER_OUT = ROOT / "src" / "font_love.h"
JS_OUT = ROOT / "web" / "js" / "font.js"

NOTDEF = 0xFFFD

# Every one of these must render as a non-empty glyph or the gift shows
# "Kocham Ci?" instead of "Kocham Cię".
POLISH = "ĄĆĘŁŃÓŚŹŻąćęłńóśźż"


class FontError(Exception):
    pass


def load_source(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise FontError(f"missing font source: {path}")
    except json.JSONDecodeError as exc:
        raise FontError(f"{path} is not valid JSON: {exc}")


def parse_glyphs(source: dict) -> list[dict]:
    height = source["height"]
    on = source["legend"]["on"]
    off = source["legend"]["off"]
    raw = source["glyphs"]

    glyphs = []
    for char, rows in raw.items():
        if len(char) != 1:
            raise FontError(f"glyph key {char!r} must be exactly one character")
        label = f"U+{ord(char):04X} {char!r}"

        if len(rows) != height:
            raise FontError(f"{label}: has {len(rows)} rows, expected {height}")

        width = len(rows[0])
        if width == 0:
            raise FontError(f"{label}: zero width")

        for index, row in enumerate(rows):
            if len(row) != width:
                raise FontError(
                    f"{label}: row {index} is {len(row)} wide, "
                    f"but row 0 is {width} wide -- all rows must match"
                )
            bad = set(row) - {on, off}
            if bad:
                raise FontError(
                    f"{label}: row {index} has unexpected {sorted(bad)}, "
                    f"only {on!r} and {off!r} are allowed"
                )

        # rows -> columns, bit 0 = top row
        columns = []
        for col in range(width):
            byte = 0
            for row in range(height):
                if rows[row][col] == on:
                    byte |= 1 << row
            columns.append(byte)

        glyphs.append(
            {
                "char": char,
                "codepoint": ord(char),
                "width": width,
                "columns": columns,
                "rows": rows,
            }
        )

    glyphs.sort(key=lambda g: g["codepoint"])  # enables binary search
    return glyphs


def validate(glyphs: list[dict]) -> None:
    by_cp = {g["codepoint"]: g for g in glyphs}

    if NOTDEF not in by_cp:
        raise FontError(
            f"no .notdef glyph (U+{NOTDEF:04X}) -- unknown characters would "
            f"have nothing to fall back to"
        )

    missing = [c for c in POLISH if ord(c) not in by_cp]
    if missing:
        raise FontError(f"missing Polish characters: {' '.join(missing)}")

    blank = [c for c in POLISH if not any(by_cp[ord(c)]["columns"])]
    if blank:
        raise FontError(f"Polish characters render blank: {' '.join(blank)}")

    # A diacritic that renders identically to its base letter is a silent
    # failure -- it looks fine in the table and wrong on the wall.
    for accented, base in zip("ĆŃÓŚŹŻćńóśźż", "CNOSZZcnoszz"):
        a, b = by_cp.get(ord(accented)), by_cp.get(ord(base))
        if a and b and a["columns"] == b["columns"]:
            raise FontError(
                f"{accented!r} is identical to {base!r} -- the diacritic is missing"
            )


def render_header(glyphs: list[dict], source: dict) -> str:
    columns: list[int] = []
    entries = []
    for glyph in glyphs:
        offset = len(columns)
        columns.extend(glyph["columns"])
        entries.append(
            f"    {{ 0x{glyph['codepoint']:04X}, {offset:4d}, "
            f"{glyph['width']} }},  // {describe(glyph['char'])}"
        )

    packed = ", ".join(f"0x{c:02X}" for c in columns)
    wrapped = []
    line = "    "
    for chunk in packed.split(", "):
        if len(line) + len(chunk) + 2 > 78:
            wrapped.append(line.rstrip())
            line = "    "
        line += chunk + ", "
    wrapped.append(line.rstrip().rstrip(","))

    return f"""// GENERATED FILE -- DO NOT EDIT
//
// Source:     tools/font_source.json
// Regenerate: python tools/build_font.py
//
// One byte per column, bit 0 = row 0 = TOP of the display.
// Glyphs are sorted by codepoint so lookup can binary search.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define LOVE_FONT_HEIGHT {source['height']}
#define LOVE_FONT_SPACING {source['spacing']}
#define LOVE_FONT_NOTDEF 0x{NOTDEF:04X}u

struct LoveGlyph {{
  uint32_t codepoint;
  uint16_t offset;  // index into LOVE_FONT_COLUMNS
  uint8_t width;    // columns, excluding inter-glyph spacing
}};

inline constexpr uint8_t LOVE_FONT_COLUMNS[] = {{
{chr(10).join(wrapped)}
}};

inline constexpr LoveGlyph LOVE_FONT_GLYPHS[] = {{
{chr(10).join(entries)}
}};

inline constexpr size_t LOVE_FONT_GLYPH_COUNT =
    sizeof(LOVE_FONT_GLYPHS) / sizeof(LOVE_FONT_GLYPHS[0]);

// Returns nullptr when the codepoint has no glyph.
inline const LoveGlyph *loveFontFind(uint32_t codepoint) {{
  size_t lo = 0;
  size_t hi = LOVE_FONT_GLYPH_COUNT;
  while (lo < hi) {{
    size_t mid = lo + (hi - lo) / 2;
    if (LOVE_FONT_GLYPHS[mid].codepoint < codepoint) {{
      lo = mid + 1;
    }} else {{
      hi = mid;
    }}
  }}
  if (lo < LOVE_FONT_GLYPH_COUNT && LOVE_FONT_GLYPHS[lo].codepoint == codepoint) {{
    return &LOVE_FONT_GLYPHS[lo];
  }}
  return nullptr;
}}

// Never returns nullptr -- falls back to .notdef, which the build script
// guarantees is present.
inline const LoveGlyph &loveFontGlyph(uint32_t codepoint) {{
  const LoveGlyph *found = loveFontFind(codepoint);
  return found ? *found : *loveFontFind(LOVE_FONT_NOTDEF);
}}

// Column bytes for a glyph, `glyph.width` of them.
inline const uint8_t *loveFontColumns(const LoveGlyph &glyph) {{
  return &LOVE_FONT_COLUMNS[glyph.offset];
}}
"""


def render_js(glyphs: list[dict], source: dict) -> str:
    entries = []
    for glyph in glyphs:
        cols = ", ".join(str(c) for c in glyph["columns"])
        entries.append(
            f"  [0x{glyph['codepoint']:04X}, [{cols}]],"
            f"  // {describe(glyph['char'])}"
        )

    return f"""// GENERATED FILE -- DO NOT EDIT
//
// Source:     tools/font_source.json
// Regenerate: python tools/build_font.py
//
// Identical column data to src/font_love.h, which is what makes the canvas
// preview pixel-identical to the real matrix.
// One byte per column, bit 0 = row 0 = TOP of the display.

export const FONT_HEIGHT = {source['height']};
export const FONT_SPACING = {source['spacing']};
export const NOTDEF = 0x{NOTDEF:04X};

/** @type {{Map<number, number[]>}} codepoint -> column bytes */
export const GLYPHS = new Map([
{chr(10).join(entries)}
]);

/** Column bytes for a codepoint, falling back to .notdef. */
export function glyphFor(codepoint) {{
  return GLYPHS.get(codepoint) ?? GLYPHS.get(NOTDEF);
}}

/** Rendered width of a string in columns, including inter-glyph spacing. */
export function measure(text) {{
  const chars = [...text];
  if (chars.length === 0) return 0;
  let width = 0;
  for (const ch of chars) width += glyphFor(ch.codePointAt(0)).length + FONT_SPACING;
  return width - FONT_SPACING;
}}

/**
 * Render text to a flat column array. Each entry is a byte, bit 0 = top row.
 * @returns {{number[]}}
 */
export function renderText(text) {{
  const columns = [];
  const chars = [...text];
  chars.forEach((ch, index) => {{
    columns.push(...glyphFor(ch.codePointAt(0)));
    if (index < chars.length - 1) {{
      for (let i = 0; i < FONT_SPACING; i++) columns.push(0);
    }}
  }});
  return columns;
}}
"""


def describe(char: str) -> str:
    if char == " ":
        return "space"
    if char == "\\":
        return "backslash"
    return char


def preview(glyphs: list[dict], only: str) -> str:
    lines = []
    for char in only:
        glyph = next((g for g in glyphs if g["char"] == char), None)
        if glyph is None:
            continue
        lines.append(f"  {describe(char)}  (U+{glyph['codepoint']:04X}, w={glyph['width']})")
        for row in glyph["rows"]:
            lines.append("    " + row.replace("#", "██").replace(".", "· "))
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate only, write nothing")
    parser.add_argument("--preview", metavar="CHARS", help="print these glyphs as ASCII art")
    args = parser.parse_args()

    try:
        source = load_source(SOURCE)
        glyphs = parse_glyphs(source)
        validate(glyphs)
    except FontError as exc:
        print(f"font error: {exc}", file=sys.stderr)
        return 1

    total_columns = sum(g["width"] for g in glyphs)

    if args.preview:
        print(preview(glyphs, args.preview))

    if args.check:
        print(f"ok: {len(glyphs)} glyphs, {total_columns} columns, all Polish present")
        return 0

    HEADER_OUT.parent.mkdir(parents=True, exist_ok=True)
    JS_OUT.parent.mkdir(parents=True, exist_ok=True)
    HEADER_OUT.write_text(render_header(glyphs, source), encoding="utf-8", newline="\n")
    JS_OUT.write_text(render_js(glyphs, source), encoding="utf-8", newline="\n")

    print(f"{len(glyphs)} glyphs, {total_columns} columns of data")
    print(f"  -> {HEADER_OUT.relative_to(ROOT)}")
    print(f"  -> {JS_OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
