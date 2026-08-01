// A fake matrix, so the whole interface can be built and checked on a desktop
// with no hardware, no Bluetooth and no HTTPS.
//
//   open index.html?mock=1
//
// It implements the same surface as BleTransport and applies commands with the
// same rules as src/protocol.cpp -- clamping rather than rejecting, switching
// mode on a text or draw command, clearing the playlist tail on a shorter
// list. If the two ever disagree, this file is the one that is wrong.

import { decodeColumns, encodeColumns, formatClockTime, parseClockTime } from './protocol.js';

const clamp = (value, low, high) => Math.min(high, Math.max(low, value));

function defaultState() {
  // Matches loveDefaultSettings() in src/state.cpp.
  const drawing = new Uint8Array(32);
  drawing.set([0x0c, 0x1e, 0x3e, 0x7c, 0x3e, 0x1e, 0x0c], 12);
  return {
    mode: 'text',
    text: 'Kocham Cię ♥',
    bright: 6,
    speed: 45,
    anim: 'heartbeat',
    draw: encodeColumns(drawing),
    playlist: [],
    dwell: 30,
    night: { on: false, from: '22:30', to: '07:00', level: 0 },
    clock: false,
  };
}

export class MockTransport {
  #state = defaultState();
  #connected = false;

  onState = () => {};
  onConnection = () => {};

  static get available() { return true; }
  static get secureContext() { return true; }

  // Mirrors BleTransport.getAvailability() so main.js can call it on either
  // transport without checking which one it has.
  static async getAvailability() { return true; }

  get connected() { return this.#connected; }

  async connect() {
    // A beat of latency, so the connecting state is actually visible during
    // development rather than flashing past.
    await new Promise((resolve) => setTimeout(resolve, 450));
    this.#connected = true;
    this.onConnection(true);
    this.#report();
    await this.syncClock();
  }

  async disconnect() {
    this.#connected = false;
    this.onConnection(false);
  }

  async syncClock() {
    const now = new Date();
    await this.send({
      c: 'time',
      v: Math.floor(now.getTime() / 1000),
      tz: -now.getTimezoneOffset(),
    });
  }

  async send(command) {
    if (!this.#connected) return;
    const s = this.#state;

    switch (command.c) {
      case 'mode':
        s.mode = ['text', 'anim', 'draw', 'playlist'].includes(command.v) ? command.v : 'text';
        break;

      case 'text':
        s.text = truncateUtf8(String(command.v ?? ''), 255);
        s.mode = 'text';
        break;

      case 'bright':
        s.bright = clamp(Math.round(Number(command.v) || 0), 0, 15);
        break;

      case 'speed':
        s.speed = clamp(Math.round(Number(command.v) || 0), 15, 160);
        break;

      case 'anim':
        s.anim = String(command.v ?? 'heartbeat');
        s.mode = 'anim';
        break;

      case 'draw': {
        const columns = decodeColumns(command.v);
        if (columns.length === 32) {
          s.draw = command.v;
          s.mode = 'draw';
        }
        break;
      }

      case 'playlist':
        if (Array.isArray(command.v)) {
          s.playlist = command.v.slice(0, 6).map((m) => truncateUtf8(String(m ?? ''), 255));
          if (command.dwell != null) {
            s.dwell = clamp(Math.round(Number(command.dwell) || 30), 3, 3600);
          }
          s.mode = 'playlist';
        }
        break;

      case 'night': {
        if (typeof command.on === 'boolean') s.night.on = command.on;
        const from = parseClockTime(command.from);
        const to = parseClockTime(command.to);
        if (from !== null) s.night.from = formatClockTime(from);
        if (to !== null) s.night.to = formatClockTime(to);
        if (command.level != null) s.night.level = clamp(Number(command.level) || 0, 0, 15);
        break;
      }

      case 'time':
        s.clock = true;
        break;

      case 'reset':
        this.#state = defaultState();
        this.#state.clock = s.clock;
        break;

      default:
        return; // unknown commands are ignored, as on the device
    }

    this.#report();
  }

  #report() {
    // Structured-clone so callers cannot mutate our copy, mirroring the fact
    // that the real device sends a fresh serialisation each time.
    this.onState(JSON.parse(JSON.stringify(this.#state)));
  }
}

/** Byte-capped truncation on a character boundary, like loveCopyText(). */
function truncateUtf8(text, maxBytes) {
  const encoder = new TextEncoder();
  if (encoder.encode(text).length <= maxBytes) return text;
  let out = '';
  let bytes = 0;
  for (const ch of text) {
    const size = encoder.encode(ch).length;
    if (bytes + size > maxBytes) break;
    out += ch;
    bytes += size;
  }
  return out;
}
