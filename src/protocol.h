// The wire contract with the phone.
//
// Free of Arduino dependencies so the whole thing can be tested on the host.
// This is deliberate: chunk reassembly is the one component here that fails
// *silently* -- a dropped or misordered chunk does not crash anything, it just
// quietly turns "Kocham Cię" into something else.
//
// Framing
// -------
// iOS negotiates an MTU around 185 bytes, and a 200-character Polish message
// is multi-byte UTF-8, so writes are split. Every frame is:
//
//     [0] chunk index, 0-based
//     [1] chunk total, >= 1
//     [2..] payload
//
// Write-with-response is ordered and reliable on iOS, so chunks are expected
// strictly in sequence. Anything else resets the buffer rather than trying to
// be clever -- the phone will simply resend.
//
// The device notifies its state back using the same framing.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "state.h"

namespace protocol {

constexpr size_t kHeaderSize = 2;

// Generous: the largest command is a full playlist, six messages of 256 bytes
// plus JSON overhead.
constexpr size_t kMaxMessageBytes = 2048;

// Buffer size a caller should allocate for serialiseState().
constexpr size_t kStateBufferSize = 2560;

// --- reassembly -------------------------------------------------------------

class Reassembler {
 public:
  enum class Result : uint8_t {
    NeedMore,  // chunk accepted, waiting for the rest
    Complete,  // message() is now valid
    Invalid,   // malformed or out of sequence; buffer was reset
    Overflow,  // message exceeded kMaxMessageBytes; buffer was reset
  };

  Result accept(const uint8_t *data, size_t len, uint32_t nowMs);

  // Valid only immediately after Complete.
  const char *message() const { return buffer_.c_str(); }
  size_t messageLength() const { return buffer_.size(); }

  // Abandons a partial message whose remaining chunks never arrived. Call from
  // loop(); without it a phone that walks out of range mid-message leaves the
  // buffer wedged until reboot.
  void expire(uint32_t nowMs);

  bool inProgress() const { return active_; }
  void reset();

 private:
  std::string buffer_;
  uint8_t expectedTotal_ = 0;
  uint8_t nextIndex_ = 0;
  uint32_t startedMs_ = 0;
  bool active_ = false;
};

// --- commands ---------------------------------------------------------------

struct ApplyResult {
  bool ok = false;       // the JSON parsed and the command was understood
  bool changed = false;  // settings were actually modified
  bool needsSave = false;

  // {"c":"time"} does not touch Settings -- it goes to Display, which owns the
  // clock. The caller forwards these on.
  bool clockSync = false;
  uint32_t epochSeconds = 0;
  int16_t tzOffsetMin = 0;

  bool factoryReset = false;
};

// Applies one JSON command to `settings`.
//
// Out-of-range numbers are clamped rather than rejected: she is dragging a
// slider, not calling an API, and a value one past the end should mean "the
// end" rather than "nothing happens".
ApplyResult applyCommand(const char *json, size_t len, Settings &settings);

// Serialises the full state as JSON, so the UI can hydrate every control the
// moment she connects. Returns bytes written, excluding the terminator, or 0
// if the buffer was too small.
size_t serialiseState(const Settings &settings, bool hasClock, char *out, size_t outSize);

// --- helpers, exposed for testing -------------------------------------------

// "22:30" -> 1350. Returns false on anything malformed.
bool parseClockTime(const char *text, uint16_t &minutesOut);

// 1350 -> "22:30". `out` needs at least 6 bytes.
void formatClockTime(uint16_t minutes, char *out, size_t outSize);

// Standard base64. Returns bytes decoded, or 0 on malformed input.
size_t decodeBase64(const char *text, size_t len, uint8_t *out, size_t outSize);

// Returns bytes written excluding the terminator, or 0 if `out` is too small.
size_t encodeBase64(const uint8_t *data, size_t len, char *out, size_t outSize);

}  // namespace protocol
