# Project Love — Design

*A BLE-controlled LED matrix gift for Milena.*

## Problem

An 8×32 red MAX7219 LED matrix should display text, hearts and animations that
Milena can change herself from her iPhone, with no app install and no fiddling.

A WiFi access point was the obvious first idea and is the wrong one: iOS makes
AP-hopping miserable, captive portals are unreliable, and she would have to leave
her real network every time she wanted to change a word. Bluetooth LE through the
**Bluefy** browser reconnects in about a second and never touches her network.

Three constraints drive every decision here:

1. **She is not a coder.** The interface must be obvious, in Polish, and forgiving.
2. **State must survive unplugging.** Whatever she sets is what appears on next
   power-up, without connecting a phone.
3. **It must feel like a love letter,** not a dev tool.

## Shape of the system

```
  iPhone / Bluefy                 ESP32-C3 Super Mini            MAX7219 8×32
 ┌──────────────────┐            ┌────────────────────┐         ┌──────────┐
 │ static web app   │            │ NimBLE GATT server │         │          │
 │  ble.js ─────────┼── CMD ────▶│  protocol.cpp      │         │  ♥ text  │
 │  preview.js      │◀─ STATE ───┤  settings.cpp(NVS) │──SPI───▶│          │
 │  ui/*.js         │  (notify)  │  display.cpp       │         │          │
 └──────────────────┘            └────────────────────┘         └──────────┘
       served by nginx over HTTPS
```

The home server only serves static files. It never talks to the ESP32 — all device
communication is browser ↔ BLE, directly. The server could be offline and a
cached page would still drive the matrix.

## Decisions and their reasons

### Persistence: NVS, not EEPROM

The ESP32 has no EEPROM. Arduino's `EEPROM` library is a deprecated flash-emulated
shim. `Preferences` (NVS) is the correct API: wear-levelled key/value storage in
flash.

Writes are **debounced 1.5 s**. Every setting applies to the display immediately,
then commits once she stops fiddling. There is no save button, because a save button
is a thing she can forget to press.

### Transport: BLE, and therefore HTTPS

`navigator.bluetooth` does not exist in an insecure context. A page served over
plain `http://192.168.x.x` cannot use Web Bluetooth at all, and a self-signed
certificate will likely be rejected too. **A real certificate is a hard requirement
of the architecture**, not a deployment nicety. Hosting is nginx + certbot on the
home server.

One consequence we cannot design away: iOS requires a user gesture to open the
device chooser, once per session. She taps *Połącz* and picks the device. That is
the entire connection ritual.

### Text encoding: a custom font with Polish diacritics

The stock MAX7219 fonts are ASCII-only. She would type `Kocham Cię` and the matrix
would show `Kocham Ci?`. That is unacceptable for the actual purpose of the object.

An 8-row font can carry Polish if the rows are budgeted deliberately:

| Row | Use |
|-----|-----|
| 0 | diacritics — kreska (ó ć ń ś ź), kropka (ż) |
| 1–6 | the letter itself (lowercase sits on rows 2–6, ascenders reach row 1) |
| 7 | descenders and the ogonek (ą ę) |

Glyphs needed: **ą ć ę ł ń ó ś ź ż** plus uppercase, plus the symbols **♥ ★ ☺ ♪**
she will actually want to type.

Firmware decodes UTF-8 → codepoint → internal glyph index, with Polish characters
and symbols occupying slots 128–255.

### One font source, two targets

`tools/font_source.json` is the single source of truth. `tools/build_font.py`
compiles it into **both** `src/font_love.h` and `web/js/font.js`.

This buys two things. Glyph drift between firmware and website becomes impossible.
And the 8×32 canvas preview in the web app is *pixel-identical* to the real matrix —
she types, and sees exactly what will appear before sending anything.

### Framing: chunked writes

iOS negotiates an MTU around 185 bytes. A 200-character Polish message is multi-byte
UTF-8 and will exceed a single write, so chunking is designed in from the start
rather than discovered later.

Frame layout is a 2-byte header `[chunkIdx, chunkTotal]` followed by payload.
Write-with-response is ordered and reliable on iOS. The device reassembles into a
buffer, parses, applies, then notifies STATE. Incomplete sequences are dropped after
a 2 s timeout.

### GATT surface

A single custom 128-bit service with two characteristics:

- **CMD** — Write. Phone → device. Chunked command frames.
- **STATE** — Read + Notify. Device → phone. Full state as JSON, so the moment she
  connects, every slider and field in the UI already shows her last settings.

Commands are compact JSON:

```json
{"c":"mode",  "v":"text|anim|draw|playlist"}
{"c":"text",  "v":"Kocham Cię ♥"}
{"c":"bright","v":7}
{"c":"speed", "v":50}
{"c":"anim",  "v":"heartbeat"}
{"c":"draw",  "v":"<base64 32 bytes>"}
{"c":"playlist","v":["...","..."],"dwell":30}
{"c":"night", "on":true,"from":"22:30","to":"07:00","level":0}
{"c":"time",  "v":1754049600,"tz":120}
```

**No pairing or PIN.** Range is roughly 10 m and the device lives in her room. A
passkey would add friction to the one interaction that must stay effortless, in
exchange for protection against a threat that does not exist. The device name is
configurable if that ever changes.

### Web app: no bundler

Native ES modules, served as a directory by nginx. WebKit and Bluefy support them,
so a build step would add a dependency on the home server for no benefit. Modules
stay small and single-purpose rather than collapsing into one large HTML file.

Fonts are self-hosted rather than loaded from a CDN, so the page works offline once
cached and does not leak her browsing to a third party.

`?mock=1` swaps `ble.js` for `mock-ble.js`, a fake device that responds to the same
protocol. The entire interface can be built, styled and walked through on a desktop
with no hardware attached.

## Hardware

| MAX7219 | ESP32-C3 Super Mini |
|---|---|
| VCC | `5V` |
| GND | `GND` |
| DIN | `GPIO6` |
| CS  | `GPIO7` |
| CLK | `GPIO4` |

Software SPI via the explicit-pin `MD_MAX72XX` constructor. This sidesteps C3
hardware-SPI pin-mapping surprises and is comfortably fast enough for four modules.

Pins avoided deliberately: **GPIO8/9** are strapping pins that must be HIGH at boot,
and **GPIO18/19** are USB D− / D+.

Power notes: the MAX7219 at 5 V has `V_IH ≈ 3.5 V`, so 3.3 V logic is marginal on
paper but works on essentially all of these modules in practice. If the display
glitches, moving VCC to `3V3` is the fix — dimmer, fully reliable. Four modules at
full brightness can draw ~650 mA, so this wants a 5 V/2 A supply rather than a
laptop port.

## Screens

All copy in Polish.

1. **Intro** — cream paper fades up, ink-serif *„made with love for Milena ♥"*,
   heart drawn with an SVG `stroke-dashoffset` sweep. ~2.5 s, tap to skip.
2. **Połącz** — one large button, opens the system device chooser.
3. **Wiadomość** — text field, live 8×32 preview, symbol palette (♥ ★ ☺ ♪),
   brightness and speed sliders.
4. **Animacje** — gallery of cards, each showing a small animated preview.
5. **Rysuj** — finger-paintable 8×32 grid, saved to a slot and sent.
6. **Playlista** — saved messages, reorderable, with a rotation interval.
7. **Noc** — night-mode toggle with from/to times.

## Testing

`protocol.cpp` is written free of hardware dependencies specifically so it can be
tested on the host with `pio test -e native`. Chunk reassembly is the component most
likely to fail *silently* and corrupt a message, so it is developed test-first.

| What | How |
|---|---|
| Font + protocol | `pio test -e native` |
| Wiring / hardware type | Boot self-test pattern; flip `HARDWARE_TYPE` in `config.h` if garbled |
| Whole UI, no hardware | `web/index.html?mock=1` on desktop |
| Real BLE | Bluefy on iPhone, each feature changes the matrix |
| Persistence | Set text → unplug → 10 s → replug → same text, without connecting |
| Polish characters | `Zażółć gęślą jaźń ♥` — all nine diacritics legible |
| Long message | 200+ character Polish text reassembles correctly |
| Night mode | Window set to `now+1min` dims on schedule |

## Known risks

**Hardware type.** These 8×32 modules ship as `FC16_HW` or `PAROLA_HW` depending on
the batch, and the wrong one produces mirrored or scrambled output. The boot
self-test turns this from a mystery into a ten-second one-line fix.

**No RTC.** The board loses the time on power cut, so night mode cannot know the
hour after an unplug. The app sends `{"c":"time"}` silently on every connect, and
night mode stays inactive until the first sync following boot. She never sees this
happen.

**3.3 V → 5 V logic** is marginal by the datasheet; fallback documented above.

## Out of scope

A 3D-printed enclosure. The A1 Mini can produce one comfortably (the matrix is
~128×32 mm against a 180 mm bed) but the electronics should work first.
