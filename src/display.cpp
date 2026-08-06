#include "display.h"

#include <Arduino.h>
#include <string.h>

#include "text_render.h"

namespace {

uint8_t reverseBits(uint8_t b) {
  b = static_cast<uint8_t>((b & 0xF0) >> 4 | (b & 0x0F) << 4);
  b = static_cast<uint8_t>((b & 0xCC) >> 2 | (b & 0x33) << 2);
  b = static_cast<uint8_t>((b & 0xAA) >> 1 | (b & 0x55) << 1);
  return b;
}

}  // namespace

void Display::begin() {
  mx_.begin();
  mx_.control(MD_MAX72XX::INTENSITY, LOVE_BRIGHTNESS_DEFAULT);
  mx_.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
  mx_.clear();
}

// --- self test --------------------------------------------------------------

void Display::selfTest() {
  Serial.println(F("[self-test] 1/4 all LEDs on -- all four modules should light"));
  memset(frame_, 0xFF, LOVE_WIDTH);
  push();
  delay(600);

  Serial.println(F("[self-test] 2/4 one column -- should be at the LEFT edge"));
  Serial.println(F("             if it is on the right, set LOVE_FLIP_H = true"));
  memset(frame_, 0, LOVE_WIDTH);
  frame_[0] = 0xFF;
  push();
  delay(1200);

  Serial.println(F("[self-test] 3/4 one row -- should be the TOP row"));
  Serial.println(F("             if it is at the bottom, set LOVE_FLIP_V = true"));
  for (uint8_t c = 0; c < LOVE_WIDTH; c++) {
    frame_[c] = 0x01;  // bit 0 = row 0
  }
  push();
  delay(1200);

  Serial.println(F("[self-test] 4/4 scrolling text -- should read Milena followed by a heart"));
  const char *banner = "Milena \xE2\x99\xA5";
  std::vector<uint8_t> bmp = textrender::render(banner, strlen(banner));
  // Blank columns either side so it scrolls fully in and fully out.
  std::vector<uint8_t> padded(LOVE_WIDTH, 0);
  padded.insert(padded.end(), bmp.begin(), bmp.end());
  padded.insert(padded.end(), LOVE_WIDTH, 0);

  for (size_t offset = 0; offset + LOVE_WIDTH <= padded.size(); offset++) {
    for (uint8_t c = 0; c < LOVE_WIDTH; c++) {
      frame_[c] = padded[offset + c];
    }
    push();
    delay(35);
  }

  memset(frame_, 0, LOVE_WIDTH);
  push();
  Serial.println(F("[self-test] done"));
}

// --- pipeline ---------------------------------------------------------------

const char *Display::activeText() const {
  if (settings_.mode == Mode::Playlist && settings_.playlistCount > 0) {
    const uint8_t index = playlistIndex_ % settings_.playlistCount;
    return settings_.playlist[index];
  }
  return settings_.text;
}

void Display::apply(const Settings &settings, uint32_t nowMs) {
  const bool animChanged = strncmp(settings_.animId, settings.animId, LOVE_MAX_ANIM_ID) != 0;
  const bool modeChanged = settings_.mode != settings.mode;

  settings_ = settings;

  if (settings_.playlistCount > 0) {
    playlistIndex_ %= settings_.playlistCount;
  } else {
    playlistIndex_ = 0;
  }

  animId_ = anim::fromString(settings_.animId);
  // Restart the animation only when it actually changes, so nudging the
  // brightness slider does not reset the heartbeat mid-beat.
  if (animChanged || modeChanged) {
    animStartMs_ = nowMs;
  }

  applyBrightness();
  rebuild(nowMs);
}

void Display::rebuild(uint32_t nowMs) {
  bitmap_.clear();
  scrollX_ = 0;
  lastStepMs_ = nowMs;
  messageStartMs_ = nowMs;
  needsPush_ = true;

  switch (settings_.mode) {
    case Mode::Draw:
      bitmap_.assign(settings_.drawing, settings_.drawing + LOVE_WIDTH);
      scrolling_ = false;
      break;

    case Mode::Anim:
      // Frames are generated per tick; no bitmap needed.
      scrolling_ = false;
      break;

    case Mode::Text:
    case Mode::Playlist:
    default: {
      const char *text = activeText();
      const size_t len = strnlen(text, LOVE_MAX_TEXT_BYTES);
      if (textrender::measure(text, len) <= LOVE_STATIC_WIDTH) {
        // Short enough to sit still and be read at a glance.
        bitmap_ = textrender::renderCentred(text, len, LOVE_WIDTH);
        scrolling_ = false;
      } else {
        bitmap_ = textrender::renderScrolling(text, len, LOVE_SCROLL_GAP);
        scrolling_ = true;
      }
      break;
    }
  }

  buildWindow();
}

void Display::buildWindow() {
  if (bitmap_.empty()) {
    memset(frame_, 0, LOVE_WIDTH);
    return;
  }
  for (uint8_t c = 0; c < LOVE_WIDTH; c++) {
    frame_[c] = bitmap_[(scrollX_ + c) % bitmap_.size()];
  }
}

void Display::push() {
  mx_.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  for (uint8_t c = 0; c < LOVE_WIDTH; c++) {
    uint8_t value = frame_[LOVE_FLIP_H ? (LOVE_WIDTH - 1 - c) : c];
    if (LOVE_FLIP_V) {
      value = reverseBits(value);
    }
    mx_.setColumn(c, value);
  }
  mx_.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

void Display::applyBrightness() {
  const uint8_t wanted = (wasAsleep_ && settings_.nightLevel > 0) ? settings_.nightLevel
                                                                 : settings_.brightness;
  if (wanted != appliedBrightness_) {
    mx_.control(MD_MAX72XX::INTENSITY, wanted > LOVE_BRIGHTNESS_MAX ? LOVE_BRIGHTNESS_MAX : wanted);
    appliedBrightness_ = wanted;
  }
}

// --- night mode -------------------------------------------------------------

void Display::setClock(uint32_t epochSeconds, int16_t tzOffsetMin, uint32_t nowMs) {
  clockEpoch_ = epochSeconds;
  tzOffsetMin_ = tzOffsetMin;
  clockSyncedAtMs_ = nowMs;
  hasClock_ = true;
}

uint16_t Display::minutesSinceMidnight(uint32_t nowMs) const {
  if (!hasClock_) {
    return 0;
  }
  const uint32_t elapsedS = (nowMs - clockSyncedAtMs_) / 1000UL;
  int64_t local = static_cast<int64_t>(clockEpoch_) + elapsedS +
                  static_cast<int64_t>(tzOffsetMin_) * 60;
  int64_t minutes = (local / 60) % 1440;
  if (minutes < 0) {
    minutes += 1440;
  }
  return static_cast<uint16_t>(minutes);
}

bool Display::asleep(uint32_t nowMs) const {
  // Without a clock we cannot know, and a display that blanks at the wrong
  // time is worse than one that never blanks.
  if (!settings_.nightOn || !hasClock_) {
    return false;
  }
  const uint16_t from = settings_.nightFromMin;
  const uint16_t to = settings_.nightToMin;
  if (from == to) {
    return false;
  }
  const uint16_t now = minutesSinceMidnight(nowMs);
  if (from < to) {
    return now >= from && now < to;
  }
  return now >= from || now < to;  // the window wraps past midnight
}

// --- per-frame --------------------------------------------------------------

void Display::tick(uint32_t nowMs) {
  const bool sleeping = asleep(nowMs);
  if (sleeping != wasAsleep_) {
    wasAsleep_ = sleeping;
    needsPush_ = true;
    applyBrightness();
  }

  // Asleep with level 0 means blank -- push once, then leave the panel alone.
  if (sleeping && settings_.nightLevel == 0) {
    if (needsPush_) {
      memset(frame_, 0, LOVE_WIDTH);
      push();
      needsPush_ = false;
    }
    return;
  }

  if (settings_.mode == Mode::Anim) {
    if (nowMs - lastStepMs_ >= anim::frameInterval(animId_)) {
      lastStepMs_ = nowMs;
      anim::render(animId_, frame_, nowMs - animStartMs_);
      push();
    }
    return;
  }

  // Rotate to the next saved message when its turn is up.
  if (settings_.mode == Mode::Playlist && settings_.playlistCount > 1) {
    const uint32_t dwellMs = static_cast<uint32_t>(settings_.playlistDwellS) * 1000UL;
    if (dwellMs > 0 && nowMs - messageStartMs_ >= dwellMs) {
      playlistIndex_ = static_cast<uint8_t>((playlistIndex_ + 1) % settings_.playlistCount);
      rebuild(nowMs);
    }
  }

  if (scrolling_ && !bitmap_.empty()) {
    if (nowMs - lastStepMs_ >= settings_.speed) {
      lastStepMs_ = nowMs;
      scrollX_ = (scrollX_ + 1) % bitmap_.size();
      buildWindow();
      push();
    }
    return;
  }

  if (needsPush_) {
    buildWindow();
    push();
    needsPush_ = false;
  }
}
