#include "state.h"

#include <string.h>

const char *loveModeToString(Mode mode) {
  switch (mode) {
    case Mode::Anim: return "anim";
    case Mode::Draw: return "draw";
    case Mode::Playlist: return "playlist";
    case Mode::Text:
    default: return "text";
  }
}

Mode loveModeFromString(const char *name) {
  if (name == nullptr) {
    return Mode::Text;
  }
  if (strcmp(name, "anim") == 0) return Mode::Anim;
  if (strcmp(name, "draw") == 0) return Mode::Draw;
  if (strcmp(name, "playlist") == 0) return Mode::Playlist;
  return Mode::Text;
}

size_t loveCopyText(char *dest, size_t destSize, const char *src) {
  if (dest == nullptr || destSize == 0) {
    return 0;
  }
  if (src == nullptr) {
    dest[0] = '\0';
    return 0;
  }

  const size_t limit = destSize - 1;  // leave room for the terminator
  size_t len = strnlen(src, limit);

  // If we stopped mid-character, walk back to the start of that sequence.
  // Without this a 256-byte cap can slice an "ę" in half and leave a
  // replacement glyph dangling off the end of her message.
  if (len == limit && src[len] != '\0') {
    while (len > 0 && (static_cast<uint8_t>(src[len]) & 0xC0) == 0x80) {
      len--;  // back over continuation bytes
    }
    // len now points at a lead byte (or 0). Drop it too if it starts a
    // sequence that does not fit.
    if (len > 0) {
      const uint8_t lead = static_cast<uint8_t>(src[len]);
      if (lead >= 0xC0) {
        size_t needed = 1;
        if (lead >= 0xF0) {
          needed = 4;
        } else if (lead >= 0xE0) {
          needed = 3;
        } else {
          needed = 2;
        }
        if (len + needed > limit) {
          // The whole sequence cannot fit, so it stays out.
        } else {
          len += needed;
        }
      }
    }
  }

  memcpy(dest, src, len);
  dest[len] = '\0';
  return len;
}

void loveDefaultSettings(Settings &s) {
  memset(&s, 0, sizeof(s));

  s.mode = Mode::Text;
  s.brightness = LOVE_BRIGHTNESS_DEFAULT;
  s.speed = LOVE_SPEED_DEFAULT;

  loveCopyText(s.text, sizeof(s.text), LOVE_DEFAULT_TEXT);
  loveCopyText(s.animId, sizeof(s.animId), "heartbeat");

  // A heart, so switching to draw mode before drawing anything still shows
  // something worth looking at.
  const uint8_t heart[] = {0x0C, 0x1E, 0x3E, 0x7C, 0x3E, 0x1E, 0x0C};
  const uint8_t left = (LOVE_WIDTH - sizeof(heart)) / 2;
  memcpy(s.drawing + left, heart, sizeof(heart));

  s.playlistCount = 0;
  s.playlistDwellS = 30;

  s.nightOn = false;
  s.nightFromMin = 22 * 60 + 30;
  s.nightToMin = 7 * 60;
  s.nightLevel = 0;
}
