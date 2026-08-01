#include "text_render.h"

#include "font_love.h"

namespace textrender {
namespace {

inline bool isContinuation(uint8_t b) { return (b & 0xC0) == 0x80; }

}  // namespace

uint32_t decodeUtf8(const char *bytes, size_t len, size_t &pos) {
  if (pos >= len) {
    return kReplacement;
  }

  const uint8_t *p = reinterpret_cast<const uint8_t *>(bytes);
  const uint8_t b0 = p[pos];

  // ASCII fast path.
  if (b0 < 0x80) {
    pos += 1;
    return b0;
  }

  size_t extra;
  uint32_t codepoint;
  uint8_t lowerBound;  // constraint on the first continuation byte
  uint8_t upperBound;

  if (b0 >= 0xC2 && b0 <= 0xDF) {  // 0xC0/0xC1 would be overlong
    extra = 1;
    codepoint = b0 & 0x1F;
    lowerBound = 0x80;
    upperBound = 0xBF;
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    extra = 2;
    codepoint = b0 & 0x0F;
    // E0 A0..BF rejects overlongs; ED 80..9F rejects UTF-16 surrogates.
    lowerBound = (b0 == 0xE0) ? 0xA0 : 0x80;
    upperBound = (b0 == 0xED) ? 0x9F : 0xBF;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    extra = 3;
    codepoint = b0 & 0x07;
    lowerBound = (b0 == 0xF0) ? 0x90 : 0x80;
    upperBound = (b0 == 0xF4) ? 0x8F : 0xBF;
  } else {
    // A bare continuation byte, or 0xF5..0xFF which is beyond U+10FFFF.
    pos += 1;
    return kReplacement;
  }

  // The sequence occupies bytes pos .. pos+extra inclusive.
  if (pos + extra >= len) {
    // Truncated -- most likely a chunk boundary landed mid-character.
    pos += 1;
    return kReplacement;
  }

  for (size_t i = 1; i <= extra; i++) {
    const uint8_t b = p[pos + i];
    const bool ok = (i == 1) ? (b >= lowerBound && b <= upperBound) : isContinuation(b);
    if (!ok) {
      pos += 1;
      return kReplacement;
    }
    codepoint = (codepoint << 6) | (b & 0x3F);
  }

  pos += extra + 1;
  return codepoint;
}

size_t countChars(const char *bytes, size_t len) {
  size_t pos = 0;
  size_t count = 0;
  while (pos < len) {
    decodeUtf8(bytes, len, pos);
    count++;
  }
  return count;
}

size_t measure(const char *bytes, size_t len) {
  size_t pos = 0;
  size_t width = 0;
  bool any = false;
  while (pos < len) {
    const uint32_t cp = decodeUtf8(bytes, len, pos);
    width += loveFontGlyph(cp).width + LOVE_FONT_SPACING;
    any = true;
  }
  return any ? width - LOVE_FONT_SPACING : 0;  // no trailing gap
}

std::vector<uint8_t> render(const char *bytes, size_t len) {
  std::vector<uint8_t> columns;
  columns.reserve(measure(bytes, len));

  size_t pos = 0;
  bool first = true;
  while (pos < len) {
    const uint32_t cp = decodeUtf8(bytes, len, pos);
    if (!first) {
      for (uint8_t i = 0; i < LOVE_FONT_SPACING; i++) {
        columns.push_back(0);
      }
    }
    first = false;

    const LoveGlyph &glyph = loveFontGlyph(cp);
    const uint8_t *data = loveFontColumns(glyph);
    for (uint8_t i = 0; i < glyph.width; i++) {
      columns.push_back(data[i]);
    }
  }
  return columns;
}

std::vector<uint8_t> renderCentred(const char *bytes, size_t len, size_t width) {
  std::vector<uint8_t> text = render(bytes, len);
  std::vector<uint8_t> out(width, 0);

  if (text.size() >= width) {
    for (size_t i = 0; i < width; i++) {
      out[i] = text[i];
    }
    return out;
  }

  const size_t left = (width - text.size()) / 2;
  for (size_t i = 0; i < text.size(); i++) {
    out[left + i] = text[i];
  }
  return out;
}

std::vector<uint8_t> renderScrolling(const char *bytes, size_t len, size_t gap) {
  std::vector<uint8_t> columns = render(bytes, len);
  columns.insert(columns.end(), gap, 0);
  return columns;
}

}  // namespace textrender
