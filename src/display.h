// Drives the matrix: mode switching, scrolling, night mode.
//
// The renderer keeps its own column bitmap and copies a 32-wide window out of
// it each frame. The web app's canvas preview does exactly the same thing with
// the same font data, which is what makes the two agree pixel for pixel.
#pragma once

#include <MD_MAX72xx.h>

#include <stdint.h>

#include <vector>

#include "animations.h"
#include "config.h"
#include "settings.h"

class Display {
 public:
  void begin();

  // Diagnostic pattern that identifies wiring and orientation problems.
  // See the troubleshooting table in config.h.
  void selfTest();

  // Rebuilds the render pipeline. Call once at startup and after any change.
  void apply(const Settings &settings, uint32_t nowMs);

  // Call every loop iteration.
  void tick(uint32_t nowMs);

  // The board has no RTC, so it cannot know the hour on its own. The phone
  // sends the time on every connect; until it does, hasClock() is false and
  // night mode stays inactive rather than guessing.
  void setClock(uint32_t epochSeconds, int16_t tzOffsetMin, uint32_t nowMs);
  bool hasClock() const { return hasClock_; }

  uint16_t minutesSinceMidnight(uint32_t nowMs) const;
  bool asleep(uint32_t nowMs) const;

 private:
  void rebuild(uint32_t nowMs);
  void buildWindow();
  void push();
  void applyBrightness();
  const char *activeText() const;

  MD_MAX72XX mx_{LOVE_HW_TYPE, LOVE_PIN_DIN, LOVE_PIN_CLK, LOVE_PIN_CS, LOVE_MODULES};

  Settings settings_{};

  // The full rendered message. For static content this is exactly LOVE_WIDTH
  // columns; for scrolling it is the message plus a trailing gap, read modulo
  // its length so the wrap is seamless.
  std::vector<uint8_t> bitmap_;
  uint8_t frame_[LOVE_WIDTH]{};

  bool scrolling_ = false;
  size_t scrollX_ = 0;
  uint32_t lastStepMs_ = 0;
  bool needsPush_ = true;

  anim::Id animId_ = anim::Id::Heartbeat;
  uint32_t animStartMs_ = 0;

  uint8_t playlistIndex_ = 0;
  uint32_t messageStartMs_ = 0;

  bool wasAsleep_ = false;
  uint8_t appliedBrightness_ = 0xFF;  // 0xFF = nothing applied yet

  bool hasClock_ = false;
  uint32_t clockEpoch_ = 0;
  int16_t tzOffsetMin_ = 0;
  uint32_t clockSyncedAtMs_ = 0;
};
