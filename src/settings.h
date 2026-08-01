// How her choices survive a power cut.
//
// The ESP32 has no EEPROM. Arduino's EEPROM library is a deprecated
// flash-emulated shim; Preferences (NVS) is the real thing -- wear-levelled
// key/value storage. The whole struct is written as one versioned blob, so a
// half-finished write can never leave a mix of old and new settings.
//
// The struct itself lives in state.h, free of Arduino dependencies, so the
// protocol code that manipulates it can be tested on the host.
#pragma once

#include <stdint.h>

#include <Preferences.h>

#include "state.h"

// Bump when the layout of Settings changes. A stored blob whose version or
// size does not match is discarded in favour of defaults, which is far better
// than reinterpreting old bytes as a new struct and scrolling garbage.
constexpr uint16_t LOVE_SETTINGS_VERSION = 1;

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
