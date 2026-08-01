// Project Love -- a BLE-controlled LED matrix.
//
// setup() and loop() only wire things together; the behaviour lives in
// display.cpp (rendering), settings.cpp (persistence) and ble_service.cpp
// (the phone).

#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "settings.h"

namespace {

Display display;
SettingsStore store;

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

  display.apply(store.get(), millis());
}

void loop() {
  const uint32_t now = millis();
  display.tick(now);
  store.tick(now);
}
