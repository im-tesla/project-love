// UTF-8 decoding and text rasterisation.
//
// Deliberately free of Arduino dependencies so it can be unit tested on the
// host (`pio test -e native`). Everything here is pure: bytes in, column
// bytes out. Anything that touches SPI or timing lives in display.cpp.
//
// Column encoding matches font_love.h -- one byte per column, bit 0 = top row.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace textrender {

// Unicode replacement character, also the font's .notdef.
constexpr uint32_t kReplacement = 0xFFFD;

// Decodes the UTF-8 sequence starting at `pos` and advances `pos` past it.
//
// Malformed input yields kReplacement and advances exactly one byte, so a
// caller looping to the end of a buffer can never get stuck. Overlong forms,
// surrogates and out-of-range codepoints are all rejected -- she is typing
// Polish through a phone keyboard, but the bytes arrive over BLE and a
// truncated chunk must not send the renderer into the weeds.
uint32_t decodeUtf8(const char *bytes, size_t len, size_t &pos);

// Number of codepoints, not bytes. "Zażółć" is 6 characters, 10 bytes.
size_t countChars(const char *bytes, size_t len);

// Rendered width in columns, including inter-glyph spacing but with no
// trailing space. Empty text measures 0.
size_t measure(const char *bytes, size_t len);

// Rasterises text into column bytes.
std::vector<uint8_t> render(const char *bytes, size_t len);

// Text centred inside `width` columns, for messages short enough to sit still.
// Text wider than `width` is left-aligned and clipped.
std::vector<uint8_t> renderCentred(const char *bytes, size_t len, size_t width);

// Text followed by `gap` blank columns, for seamless wrap-around scrolling:
// the renderer reads this modulo its length, so the gap is what separates the
// end of the message from its beginning coming back around.
std::vector<uint8_t> renderScrolling(const char *bytes, size_t len, size_t gap);

}  // namespace textrender
