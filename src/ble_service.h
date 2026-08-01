// The BLE half of the link to her phone.
//
// This file owns only the NimBLE plumbing. The wire format -- framing,
// commands, state -- lives in protocol.cpp, free of Arduino so it can be
// tested on the host.
//
// Threading: NimBLE runs its callbacks on its own task. Nothing here touches
// the display or flash from that task; complete messages are handed to loop()
// through a mutex-guarded slot and processed there.
#pragma once

#include <stdint.h>

#include <string>

#include "protocol.h"
#include "state.h"

class BleService {
 public:
  void begin();

  // Hands over one complete command, if one has arrived. Call from loop().
  // Also expires partial messages whose remaining chunks never turned up.
  bool takeCommand(std::string &out, uint32_t nowMs);

  // Pushes the full state to the phone, chunked to fit the negotiated MTU, so
  // every control in the UI shows her real settings.
  void notifyState(const Settings &settings, bool hasClock);

  bool connected() const;

  // One-shot edge: true exactly once after each connection is established.
  // main() uses it to send the initial state.
  bool consumeJustConnected();

 private:
  friend struct LoveServerCallbacks;
  friend struct LoveCommandCallbacks;
};
