// Everything she has chosen, and how it survives a power cut.
//
// The ESP32 has no EEPROM. Arduino's EEPROM library is a deprecated
// flash-emulated shim; Preferences (NVS) is the real thing -- wear-levelled
// key/value storage. The whole struct is written as one versioned blob, so a
// half-finished write can never leave a mix of old and new settings.
#pragma once

#include <stdint.h>

#include <Preferences.h>

#include "config.h"

// Bump when the layout of Settings changes. A stored blob whose version or
// size does not match is discarded in favour of defaults, which is far better
// than reinterpreting old bytes as a new struct and scrolling garbage.
constexpr uint16_t LOVE_SETTINGS_VERSION = 1;

enum class Mode : uint8_t {
  Text = 0,
  Anim,
  Draw,
  Playlist,
};

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
  uint8_t nightLevel;  // 0 = blank the display, else brightness while asleep
};

// Fills `s` with what a factory-fresh board should show.
void loveDefaultSettings(Settings &s);

class SettingsStore {
 public:
  // Loads from flash, falling back to defaults if nothing valid is stored.
  // Returns true if stored settings were restored.
  bool begin();

  const Settings &get() const { return settings_; }

  // Mutate through this, then call touch(). Changes take effect on the display
  // immediately; only the flash write is deferred.
  Settings &mutate() { return settings_; }

  // Restarts the debounce timer. Call after any change.
  void touch(uint32_t nowMs);

  // Commits if the debounce has elapsed. Call from loop().
  void tick(uint32_t nowMs);

  // Writes immediately, ignoring the debounce.
  void flush();

  bool dirty() const { return dirty_; }

  // Wipes stored settings and returns to defaults.
  void factoryReset();

 private:
  Settings settings_{};
  Preferences prefs_;
  bool dirty_ = false;
  uint32_t dirtySince_ = 0;
};
