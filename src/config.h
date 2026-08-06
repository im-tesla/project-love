// Every knob that depends on the physical hardware lives here.
//
// If the boot self-test looks wrong, the fix is in this file -- see the
// troubleshooting block below rather than editing the renderer.
#pragma once

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Wiring
//
// Software SPI with explicit pins. This sidesteps the ESP32-C3's hardware-SPI
// pin mapping and is comfortably fast enough for four cascaded modules.
//
// Pins avoided on purpose:
//   GPIO8, GPIO9    strapping pins, must read HIGH at boot
//   GPIO18, GPIO19  USB D- / D+ on the C3
// ---------------------------------------------------------------------------
constexpr uint8_t LOVE_PIN_DIN = 6;
constexpr uint8_t LOVE_PIN_CLK = 4;
constexpr uint8_t LOVE_PIN_CS = 7;

constexpr uint8_t LOVE_MODULES = 4;  // 4 x MAX7219, each driving 8x8
constexpr uint8_t LOVE_WIDTH = LOVE_MODULES * 8;  // 32 columns
constexpr uint8_t LOVE_HEIGHT = 8;

// ---------------------------------------------------------------------------
// Boot self-test troubleshooting
//
// At power-up the firmware runs four steps, each announced over serial:
//
//   1. every LED on          all four modules should light
//   2. one lit COLUMN        should be at the LEFT edge
//   3. one lit ROW           should be the TOP row
//   4. "MILENA <heart>"      should scroll past, readable
//
// Match what you see against this table:
//
//   Lit column is at the right edge      -> LOVE_FLIP_H = true
//   Lit row is at the bottom             -> LOVE_FLIP_V = true
//   Blocks of 8 columns out of order,    -> change LOVE_HW_TYPE. FC16_HW is
//   or mirrored within each block           by far the most common for these
//                                           "8x32 red MAX7219" boards; then
//                                           try PAROLA_HW, GENERIC_HW,
//                                           ICSTATION_HW
//   Only some modules light              -> power, not configuration. Four
//                                           modules can pull ~650mA; a laptop
//                                           port may not carry it
//   Nothing at all                       -> check wiring before editing this
//
// The vertical direction is not really in doubt: MD_MAX72XX's own font stores
// 'L' as {127, 64, 64, 64, 64} -- a full stem in bit 0..6 and a bottom bar in
// bit 6 -- so bit 0 is the top row, which is what build_font.py emits. Only
// the horizontal direction depends on how the modules are physically oriented.
// ---------------------------------------------------------------------------
#define LOVE_HW_TYPE MD_MAX72XX::FC16_HW

constexpr bool LOVE_FLIP_H = false;
constexpr bool LOVE_FLIP_V = true;

// Set false once the wiring is confirmed, to skip straight to her message.
constexpr bool LOVE_RUN_SELF_TEST = true;

// ---------------------------------------------------------------------------
// BLE
//
// A 128-bit custom service. The UUIDs are arbitrary but must match
// web/js/protocol.js exactly.
// ---------------------------------------------------------------------------
#define LOVE_SERVICE_UUID "6c6f7665-0001-4d69-6c65-6e61c2a90001"
#define LOVE_CMD_UUID "6c6f7665-0002-4d69-6c65-6e61c2a90002"
#define LOVE_STATE_UUID "6c6f7665-0003-4d69-6c65-6e61c2a90003"

// Shown in the iOS device chooser.
#define LOVE_DEVICE_NAME "Milena \xE2\x99\xA5"

// ---------------------------------------------------------------------------
// Limits
//
// Text is measured in BYTES, not characters. Polish letters cost two bytes
// each in UTF-8, so 256 bytes is roughly 200 Polish characters -- far more
// than fits on a fridge but comfortably more than she will ever type.
// ---------------------------------------------------------------------------
constexpr size_t LOVE_MAX_TEXT_BYTES = 256;
constexpr uint8_t LOVE_PLAYLIST_SLOTS = 6;
constexpr uint8_t LOVE_DRAW_SLOTS = 4;
constexpr size_t LOVE_MAX_ANIM_ID = 16;

// ---------------------------------------------------------------------------
// Behaviour
// ---------------------------------------------------------------------------

// Blank columns between the end of a scrolling message and its start coming
// back around.
constexpr uint8_t LOVE_SCROLL_GAP = 12;

// Messages this wide or narrower sit still and centred instead of scrolling.
constexpr uint8_t LOVE_STATIC_WIDTH = LOVE_WIDTH;

// Milliseconds per one-column shift. Lower is faster.
constexpr uint8_t LOVE_SPEED_MIN = 15;
constexpr uint8_t LOVE_SPEED_MAX = 160;
constexpr uint8_t LOVE_SPEED_DEFAULT = 45;

// MAX7219 has 16 brightness steps.
constexpr uint8_t LOVE_BRIGHTNESS_MAX = 15;
constexpr uint8_t LOVE_BRIGHTNESS_DEFAULT = 6;

// Settings are written to flash this long after she stops fiddling. Every
// change applies to the display instantly; only the flash write waits, so a
// dragged brightness slider costs one write rather than forty.
constexpr uint32_t LOVE_SAVE_DEBOUNCE_MS = 1500;

// A partial BLE message is abandoned if the remaining chunks never arrive.
constexpr uint32_t LOVE_CHUNK_TIMEOUT_MS = 2000;

// What the matrix shows on a factory-fresh board.
#define LOVE_DEFAULT_TEXT "Kocham Ci\xC4\x99 \xE2\x99\xA5"
