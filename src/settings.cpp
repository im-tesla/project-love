#include "settings.h"

#include <string.h>

namespace {

constexpr char kNamespace[] = "love";
constexpr char kBlobKey[] = "state";

}  // namespace

bool SettingsStore::begin() {
  loveDefaultSettings(settings_);

  prefs_.begin(kNamespace, /*readOnly=*/false);

  const size_t stored = prefs_.getBytesLength(kBlobKey);
  if (stored != sizeof(Settings)) {
    // Nothing stored, or written by a different firmware version.
    return false;
  }

  Settings loaded{};
  if (prefs_.getBytes(kBlobKey, &loaded, sizeof(loaded)) != sizeof(loaded)) {
    return false;
  }
  if (loaded.version != LOVE_SETTINGS_VERSION || loaded.structSize != sizeof(Settings)) {
    return false;
  }

  // Guard against a truncated write leaving unterminated strings or nonsense
  // numbers, either of which would run the renderer off the end of a buffer.
  loaded.text[sizeof(loaded.text) - 1] = '\0';
  loaded.animId[sizeof(loaded.animId) - 1] = '\0';
  for (uint8_t i = 0; i < LOVE_PLAYLIST_SLOTS; i++) {
    loaded.playlist[i][sizeof(loaded.playlist[i]) - 1] = '\0';
  }
  if (loaded.playlistCount > LOVE_PLAYLIST_SLOTS) {
    loaded.playlistCount = LOVE_PLAYLIST_SLOTS;
  }
  if (loaded.brightness > LOVE_BRIGHTNESS_MAX) {
    loaded.brightness = LOVE_BRIGHTNESS_DEFAULT;
  }
  if (loaded.speed < LOVE_SPEED_MIN || loaded.speed > LOVE_SPEED_MAX) {
    loaded.speed = LOVE_SPEED_DEFAULT;
  }
  if (loaded.nightFromMin > 1439 || loaded.nightToMin > 1439) {
    loaded.nightOn = false;
    loaded.nightFromMin = 22 * 60 + 30;
    loaded.nightToMin = 7 * 60;
  }

  settings_ = loaded;
  return true;
}

void SettingsStore::touch(uint32_t nowMs) {
  dirty_ = true;
  dirtySince_ = nowMs;
}

void SettingsStore::tick(uint32_t nowMs) {
  if (!dirty_) {
    return;
  }
  if (nowMs - dirtySince_ < LOVE_SAVE_DEBOUNCE_MS) {
    return;
  }
  flush();
}

void SettingsStore::flush() {
  if (!dirty_) {
    return;
  }
  settings_.version = LOVE_SETTINGS_VERSION;
  settings_.structSize = sizeof(Settings);
  prefs_.putBytes(kBlobKey, &settings_, sizeof(settings_));
  dirty_ = false;
}

void SettingsStore::factoryReset() {
  prefs_.clear();
  loveDefaultSettings(settings_);
  dirty_ = false;
}
