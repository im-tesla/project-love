// Frame generators for the animation gallery.
//
// Every animation is a pure function of elapsed time: same millisecond in,
// same 32 columns out. No internal state, which means they are testable on the
// host, they resume correctly after a mode change, and adding one is a matter
// of writing a single function.
//
// Free of Arduino dependencies on purpose -- see platformio.ini's native env.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace anim {

constexpr size_t kColumns = 32;

enum class Id : uint8_t {
  Heartbeat = 0,  // a heart beating in a lub-dub rhythm
  Hearts,         // small hearts drifting across
  Sparkle,        // twinkling stars
  Arrow,          // an arrow flying into a heart
  Count,
};

// The identifiers sent over BLE. Kept in sync with web/js/ui/anim.js.
const char *toString(Id id);

// Unknown or malformed ids fall back to Heartbeat rather than failing --
// a typo in a command should still light something up.
Id fromString(const char *id);

// Renders one frame into `frame`, overwriting all 32 columns.
// One byte per column, bit 0 = top row, matching font_love.h.
void render(Id id, uint8_t frame[kColumns], uint32_t elapsedMs);

// How often the caller should re-render this animation, in milliseconds.
uint16_t frameInterval(Id id);

}  // namespace anim
