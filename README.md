# Project Love

An 8×32 red LED matrix that shows whatever Milena types into a web page on her
phone. No app to install, no WiFi to join — the page talks to the board
directly over Bluetooth LE.

Whatever she sets stays set: unplug the board, plug it back in a week later,
and the same message comes back without a phone anywhere near it.

```
   iPhone / Bluefy              ESP32-C3 Super Mini         MAX7219 8×32
  ┌────────────────┐           ┌───────────────────┐       ┌──────────┐
  │  static page   │── CMD ───▶│  NimBLE server    │──SPI─▶│  ♥ text  │
  │ (GitHub Pages) │◀─ STATE ──│  NVS persistence  │       │          │
  └────────────────┘           └───────────────────┘       └──────────┘
```

The server only serves files. It never talks to the board — all device traffic
is browser ↔ BLE, directly.

---

## Wiring

| MAX7219 | ESP32-C3 Super Mini |
|---------|---------------------|
| VCC     | `5V`                |
| GND     | `GND`               |
| DIN     | `GPIO6`             |
| CS      | `GPIO7`             |
| CLK     | `GPIO4`             |

Pins avoided deliberately: **GPIO8/9** are strapping pins that must read HIGH
at boot, and **GPIO18/19** are USB D− / D+.

**Power.** Four modules at full brightness can pull ~650 mA, so use a 5 V/2 A
supply rather than a laptop port. The MAX7219 at 5 V wants ~3.5 V to read a
logic high and the C3 only puts out 3.3 V — marginal on paper, fine on
essentially every one of these modules in practice. If the display glitches or
flickers, move VCC to `3V3`: dimmer, completely reliable.

---

## Flashing

```bash
pio run -e esp32c3 -t upload
```

```bash
pio device monitor
```

If the monitor stays silent, that is expected on a bare board *only* if the USB
flags are missing — this board has no USB-UART bridge and talks over native USB.
They are already set in `platformio.ini`.

### The boot self-test

On every power-up the firmware runs four steps and narrates them over serial:

| Step | What you should see |
|------|---------------------|
| 1 | every LED on — all four modules light |
| 2 | one lit **column**, at the **left** edge |
| 3 | one lit **row**, at the **top** |
| 4 | `MILENA ♥` scrolls past, readable |

Fixes all live in [`src/config.h`](src/config.h):

| Symptom | Fix |
|---------|-----|
| Lit column is on the right | `LOVE_FLIP_H = true` |
| Lit row is at the bottom | `LOVE_FLIP_V = true` |
| Blocks of 8 columns scrambled or mirrored | change `LOVE_HW_TYPE` — try `PAROLA_HW`, then `GENERIC_HW`, `ICSTATION_HW` |
| Only some modules light | power, not configuration |
| Nothing at all | check wiring before editing anything |

Set `LOVE_RUN_SELF_TEST = false` once you are happy, and it boots straight to
her message.

---

## The web app

Everything is static. No build step, no npm — the browser loads ES modules
directly, so deploying is copying `web/` somewhere nginx can see it.

### Trying it with no hardware

```bash
python -m http.server 8777 --directory web
```

Then open <http://localhost:8777/index.html?mock=1>. The `?mock=1` swaps the
Bluetooth layer for a fake matrix that applies commands by the same rules as
the firmware, so the entire interface — intro, preview, drawing pad, playlist,
night mode — works on a desktop with nothing plugged in.

### Deploying

**Live at <https://im-tesla.github.io/project-love/>**, via GitHub Pages. A
workflow ([`.github/workflows/pages.yml`](.github/workflows/pages.yml))
publishes `web/` on every push to `master` that touches it — no build step,
since the site is already plain ES modules. Pages gives it a real certificate
for free, which is the one hard requirement here: `navigator.bluetooth` does
not exist in an insecure context, so over `http://` the Połącz button reports
that the browser has no Bluetooth support, even in Bluefy.

The site is a **project page**, so it lives under a `/project-love/` subpath
rather than at a bare domain. Every import and asset reference in `web/` is
relative for exactly that reason — check that a new file follows the same rule
before adding it.

The repo is public, which is what makes the free tier of Pages possible. That
also means the source — and the surprise — is visible to anyone who finds the
repo. Decided that trade was worth it over paying for GitHub Pro to keep Pages
on a private repo.

Self-hosting behind your own nginx is still fully documented in
[docs/hosting.md](docs/hosting.md) if you ever want to move off Pages — the
DuckDNS + certbot path, a Cloudflare-fronted path, and the nginx configs for
both are all still there and still correct.

### On her phone

1. Install **Bluefy** from the App Store (Safari has no Web Bluetooth).
2. Open your URL in Bluefy and bookmark it.
3. Tap **Połącz** and pick `Milena ♥`.

iOS requires that tap once per session to open the device chooser. It cannot be
automated away — it is a WebKit rule, not a limitation of this project.

---

## Changing the font

`tools/font_source.json` is editable ASCII art and is the single source of
truth. It compiles into **both** the firmware and the browser:

```bash
python tools/build_font.py
```

That regenerates `src/font_love.h` and `web/js/font.js` from the same data,
which is why the preview in the app is pixel-identical to the matrix — and why
the two cannot drift apart.

Preview a few glyphs without writing anything:

```bash
python tools/build_font.py --check --preview "AąĆŁż♥"
```

The generator refuses to emit a font where a Polish letter is missing, blank, or
byte-identical to its unaccented base. All three look fine in the table and
wrong on the wall.

---

## Tests

```bash
python tools/run_host_tests.py
```

69 tests covering UTF-8 decoding, text rasterisation, BLE chunk reassembly, the
command contract, and state serialisation. No hardware needed.

`pio test -e native` is the idiomatic command and works on Linux/macOS, but not
on Windows: PlatformIO's native platform hardcodes `env.Tool("gcc")` and the
SCons it bundles has the MSVC support module stripped out. The runner above
drives Visual Studio's compiler instead and runs the same suites.

---

## Layout

```
src/
  config.h          pins, UUIDs, limits — and the self-test troubleshooting table
  main.cpp          setup/loop wiring only
  display.{h,cpp}   rendering, scrolling, night mode
  animations.{h,cpp} frame generators, pure functions of time
  state.{h,cpp}     the Settings struct, free of Arduino so it can be tested
  settings.{h,cpp}  NVS persistence with debounced writes
  protocol.{h,cpp}  chunk framing, JSON commands, state serialisation
  ble_service.{h,cpp} NimBLE plumbing only
  font_love.h       GENERATED — do not edit
tools/
  font_source.json  glyphs as editable ASCII art
  build_font.py     → font_love.h + font.js
  run_host_tests.py host test runner
web/
  index.html        one page; no bundler
  css/              paper, components, intro
  js/               protocol, ble, mock-ble, preview, animations, ui/
  js/font.js        GENERATED — do not edit
deploy/
  nginx-love.conf
docs/superpowers/specs/
  2026-08-01-love-matrix-design.md   why it is built this way
```

---

## Things worth knowing

**Persistence is NVS, not EEPROM.** The ESP32 has no EEPROM; Arduino's library
is a deprecated flash-emulated shim. Settings are one versioned blob, written
1.5 s after she stops fiddling — every change applies to the display instantly,
but dragging a slider costs one flash write rather than forty.

**Night mode needs the phone.** The board has no RTC, so after a power cut it
genuinely does not know the hour. The app sends the time silently on every
connect, and night mode stays idle until it does — better than blanking at the
wrong hour. The app says so on screen rather than looking broken.

**No BLE pairing.** Range is ~10 m and the device lives in her room. A passkey
would add friction to the one interaction that has to stay effortless, in
exchange for protecting against a threat that does not exist. Change
`LOVE_DEVICE_NAME` in `config.h` if you want it to advertise as something else.

---

zrobione z miłością ♥
