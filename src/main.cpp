// Project Love -- a BLE-controlled LED matrix.
//
// setup() and loop() only wire things together. The behaviour lives in
// display.cpp (rendering), settings.cpp (persistence), protocol.cpp (the wire
// format) and ble_service.cpp (the phone).

#include <Arduino.h>

#include <string>

#include "ble_service.h"
#include "config.h"
#include "display.h"
#include "protocol.h"
#include "settings.h"
#include "state.h"

namespace {

Display display;
SettingsStore store;
BleService ble;

void handleCommand(const std::string &json, uint32_t now) {
  const protocol::ApplyResult result =
      protocol::applyCommand(json.data(), json.size(), store.mutate());

  if (!result.ok) {
    Serial.println(F("[cmd] ignored an unrecognised command"));
    return;
  }

  // The clock lives in Display, not Settings -- storing it would be
  // meaningless, since it is wrong the moment the board is unplugged.
  if (result.clockSync) {
    display.setClock(result.epochSeconds, result.tzOffsetMin, now);
  }

  if (result.changed) {
    display.apply(store.get(), now);
  }

  if (result.needsSave) {
    // Applies instantly; the flash write waits for her to stop fiddling.
    store.touch(now);
  }

  // Echo the new state so the UI stays in step, including any value we
  // clamped on the way in.
  ble.notifyState(store.get(), display.hasClock());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);  // let native USB CDC come up before the first print

  Serial.println();
  Serial.println(F("== project love =="));

  display.begin();

  if (LOVE_RUN_SELF_TEST) {
    display.selfTest();
  }

  const bool restored = store.begin();
  Serial.printf("settings: %s\n",
                restored ? "restored from flash" : "defaults (nothing stored yet)");

  // Show her message before BLE is even up, so an unplug-replug brings the
  // matrix straight back without a phone anywhere nearby.
  display.apply(store.get(), millis());

  ble.begin();
}

void loop() {
  const uint32_t now = millis();

  if (ble.consumeJustConnected()) {
    ble.notifyState(store.get(), display.hasClock());
  }

  std::string command;
  if (ble.takeCommand(command, now)) {
    handleCommand(command, now);
  }

  display.tick(now);
  store.tick(now);
}
