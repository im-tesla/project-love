// Everything she has chosen, as plain data.
//
// Deliberately separate from settings.h: that one pulls in Arduino's
// Preferences library, and keeping the struct free of it means protocol.cpp
// can be tested on the host -- including "does this JSON command actually
// change the right field", which is most of the contract with the web app.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"

enum class Mode : uint8_t {
  Text = 0,
  Anim,
  Draw,
  Playlist,
};

// Identifiers as they travel over BLE. Kept in sync with web/js/protocol.js.
const char *loveModeToString(Mode mode);

// Unknown values fall back to Text rather than failing -- a garbled command
// should still leave something readable on the wall.
Mode loveModeFromString(const char *name);

struct Settings {
  uint16_t version;
  uint16_t structSize;

  Mode mode;
  uint8_t brightness;  // 0..LOVE_BRIGHTNESS_MAX
  uint8_t speed;       // ms per column shift

  char text[LOVE_MAX_TEXT_BYTES];
  char animId[LOVE_MAX_ANIM_ID];

  uint8_t drawing[LOVE_WIDTH];  // column bytes, bit 0 = top row

  char playlist[LOVE_PLAYLIST_SLOTS][LOVE_MAX_TEXT_BYTES];
  uint8_t playlistCount;
  uint16_t playlistDwellS;

  bool nightOn;
  uint16_t nightFromMin;  // minutes since midnight
  uint16_t nightToMin;
  uint8_t nightLevel;  // 0 = blank while asleep, else brightness
};

// Fills `s` with what a factory-fresh board should show.
void loveDefaultSettings(Settings &s);

// Copies `src` into a fixed-size text field, truncating on a UTF-8 character
// boundary so a long message never ends in half an "ę".
// Returns the number of bytes written, excluding the terminator.
size_t loveCopyText(char *dest, size_t destSize, const char *src);
