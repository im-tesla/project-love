#include "settings.h"

#include <string.h>

namespace {

constexpr char kNamespace[] = "love";
constexpr char kBlobKey[] = "state";

}  // namespace

void loveDefaultSettings(Settings &s) {
  memset(&s, 0, sizeof(s));

  s.version = LOVE_SETTINGS_VERSION;
  s.structSize = sizeof(Settings);

  s.mode = Mode::Text;
  s.brightness = LOVE_BRIGHTNESS_DEFAULT;
  s.speed = LOVE_SPEED_DEFAULT;

  strncpy(s.text, LOVE_DEFAULT_TEXT, sizeof(s.text) - 1);
  strncpy(s.animId, "heartbeat", sizeof(s.animId) - 1);

  // A heart, so a fresh board still shows something worth looking at if she
  // switches to draw mode before drawing anything.
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

  // Guard against a truncated write leaving unterminated strings, which would
  // otherwise run the renderer off the end of the buffer.
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
