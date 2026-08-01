#include "animations.h"

#include <string.h>

namespace anim {
namespace {

// --- sprites ----------------------------------------------------------------
//
// Column bytes, bit 0 = top row. The ASCII art above each one is the source of
// truth for what it should look like; the numbers are derived from it.

// .##...##.
// #########
// #########
// #########
// .#######.
// ..#####..
// ...###...
// ....#....
constexpr uint8_t kHeartBig[] = {0x0E, 0x1F, 0x3F, 0x7E, 0xFE, 0x7E, 0x3F, 0x1F, 0x0E};

// .......
// .##.##.
// #######
// #######
// .#####.
// ..###..
// ...#...
// .......
constexpr uint8_t kHeartMid[] = {0x0C, 0x1E, 0x3E, 0x7C, 0x3E, 0x1E, 0x0C};

// .....
// .....
// .#.#.
// #####
// #####
// .###.
// ..#..
// .....
constexpr uint8_t kHeartSmall[] = {0x18, 0x3C, 0x78, 0x3C, 0x18};

// ......
// ......
// ....#.
// .....#
// ######
// .....#
// ....#.
// ......
constexpr uint8_t kArrow[] = {0x10, 0x10, 0x10, 0x10, 0x54, 0x38};

constexpr size_t kHeartBigW = sizeof(kHeartBig);
constexpr size_t kHeartMidW = sizeof(kHeartMid);
constexpr size_t kHeartSmallW = sizeof(kHeartSmall);
constexpr size_t kArrowW = sizeof(kArrow);

// --- helpers ----------------------------------------------------------------

void clear(uint8_t frame[kColumns]) { memset(frame, 0, kColumns); }

// Draws a sprite with its left edge at column `x`, OR-ed into the frame and
// clipped at both edges. `x` is signed so sprites can walk in from off-screen.
void blit(uint8_t frame[kColumns], const uint8_t *sprite, size_t width, int32_t x) {
  for (size_t i = 0; i < width; i++) {
    const int32_t col = x + static_cast<int32_t>(i);
    if (col >= 0 && col < static_cast<int32_t>(kColumns)) {
      frame[col] |= sprite[i];
    }
  }
}

void blitCentred(uint8_t frame[kColumns], const uint8_t *sprite, size_t width) {
  blit(frame, sprite, width, static_cast<int32_t>((kColumns - width) / 2));
}

// A cheap integer hash, so the sparkle pattern is random-looking but is still
// a pure function of position and time.
uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352DU;
  x ^= x >> 15;
  x *= 0x846CA68BU;
  x ^= x >> 16;
  return x;
}

// --- the animations ---------------------------------------------------------

// Real hearts go lub-DUB ... pause. Two beats close together, then rest, which
// reads as a heartbeat where an evenly-spaced pulse just reads as blinking.
void renderHeartbeat(uint8_t frame[kColumns], uint32_t elapsedMs) {
  constexpr uint32_t kPeriod = 1300;
  const uint32_t t = elapsedMs % kPeriod;

  clear(frame);
  if (t < 130) {
    blitCentred(frame, kHeartBig, kHeartBigW);  // lub
  } else if (t < 260) {
    blitCentred(frame, kHeartMid, kHeartMidW);
  } else if (t < 400) {
    blitCentred(frame, kHeartBig, kHeartBigW);  // DUB
  } else if (t < 520) {
    blitCentred(frame, kHeartMid, kHeartMidW);
  } else {
    blitCentred(frame, kHeartSmall, kHeartSmallW);  // rest
  }
}

// Three hearts drifting right to left at even spacing, wrapping seamlessly.
void renderHearts(uint8_t frame[kColumns], uint32_t elapsedMs) {
  constexpr int32_t kSpacing = 13;
  constexpr int32_t kSpan = kSpacing * 3;  // distance before the pattern repeats
  constexpr uint32_t kMsPerColumn = 55;

  clear(frame);
  const int32_t drift = static_cast<int32_t>((elapsedMs / kMsPerColumn) % kSpan);

  // Start one spacing off the right edge so hearts enter rather than pop in.
  for (int32_t i = -1; i < 4; i++) {
    blit(frame, kHeartMid, kHeartMidW, kColumns - drift + i * kSpacing);
  }
}

void renderSparkle(uint8_t frame[kColumns], uint32_t elapsedMs) {
  constexpr uint32_t kSlotMs = 110;
  const uint32_t slot = elapsedMs / kSlotMs;

  clear(frame);
  for (uint32_t col = 0; col < kColumns; col++) {
    for (uint32_t row = 0; row < 8; row++) {
      // Each star lives for two slots, so they fade in and out rather than
      // strobing every frame.
      const uint32_t a = hash32(col * 911u + row * 3571u + slot * 7919u);
      const uint32_t b = hash32(col * 911u + row * 3571u + (slot - 1) * 7919u);
      if ((a & 0xFF) < 14 || (b & 0xFF) < 8) {
        frame[col] |= static_cast<uint8_t>(1u << row);
      }
    }
  }
}

// An arrow crosses the display and buries itself in a heart, which flinches.
void renderArrow(uint8_t frame[kColumns], uint32_t elapsedMs) {
  constexpr uint32_t kPeriod = 2600;
  constexpr int32_t kStart = -static_cast<int32_t>(kArrowW);
  constexpr int32_t kEnd = kColumns;
  constexpr uint32_t kFlightMs = 1400;

  const uint32_t t = elapsedMs % kPeriod;
  clear(frame);

  // The heart is struck once the arrow's tip reaches the middle.
  const int32_t heartLeft = static_cast<int32_t>((kColumns - kHeartMidW) / 2);
  const bool struck = t > (kFlightMs * 50 / 100) && t < (kFlightMs * 75 / 100);

  if (struck) {
    blitCentred(frame, kHeartBig, kHeartBigW);
  } else {
    blit(frame, kHeartMid, kHeartMidW, heartLeft);
  }

  if (t < kFlightMs) {
    const int32_t travel = kEnd - kStart;
    const int32_t x = kStart + static_cast<int32_t>(t * travel / kFlightMs);
    blit(frame, kArrow, kArrowW, x);
  }
}

}  // namespace

// --- public -----------------------------------------------------------------

const char *toString(Id id) {
  switch (id) {
    case Id::Heartbeat: return "heartbeat";
    case Id::Hearts: return "hearts";
    case Id::Sparkle: return "sparkle";
    case Id::Arrow: return "arrow";
    default: return "heartbeat";
  }
}

Id fromString(const char *id) {
  if (id == nullptr) {
    return Id::Heartbeat;
  }
  for (uint8_t i = 0; i < static_cast<uint8_t>(Id::Count); i++) {
    const Id candidate = static_cast<Id>(i);
    if (strcmp(id, toString(candidate)) == 0) {
      return candidate;
    }
  }
  return Id::Heartbeat;
}

void render(Id id, uint8_t frame[kColumns], uint32_t elapsedMs) {
  switch (id) {
    case Id::Hearts: renderHearts(frame, elapsedMs); break;
    case Id::Sparkle: renderSparkle(frame, elapsedMs); break;
    case Id::Arrow: renderArrow(frame, elapsedMs); break;
    case Id::Heartbeat:
    default: renderHeartbeat(frame, elapsedMs); break;
  }
}

uint16_t frameInterval(Id id) {
  switch (id) {
    case Id::Hearts: return 55;
    case Id::Sparkle: return 110;
    case Id::Arrow: return 40;
    case Id::Heartbeat:
    default: return 40;
  }
}

}  // namespace anim
