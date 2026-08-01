// Wires the transport, the preview and the panels together.
//
// Two rules shape everything here:
//
//   1. The preview updates the instant she touches a control, not when the
//      device replies. A phone-to-BLE round trip is fast but not free, and a
//      preview that lags behind her finger feels broken.
//
//   2. The device is still the source of truth. Its state notification is
//      merged back in, but never into a control she is currently editing --
//      otherwise a reply arriving mid-word would yank the cursor.

import { BleTransport } from './ble.js';
import { MockTransport } from './mock-ble.js';
import { Preview } from './preview.js';
import { decodeColumns } from './protocol.js';

import { setupIntro } from './ui/intro.js';
import { setupMessage } from './ui/message.js';
import { setupAnimations } from './ui/anim.js';
import { setupDrawing } from './ui/draw.js';
import { setupPlaylist } from './ui/playlist.js';
import { setupDials } from './ui/dials.js';
import { setupNight } from './ui/night.js';

const $ = (selector) => document.querySelector(selector);

// ?mock=1 for local development. LOVE_FORCE_MOCK is set by the single-file
// build (tools/build_single_file.py), which has no query string to read.
const useMock = new URLSearchParams(location.search).get('mock') === '1'
  || window.LOVE_FORCE_MOCK === true;
const Transport = useMock ? MockTransport : BleTransport;
const link = new Transport();

/** Everything the preview and the panels read. Mutated in place. */
const view = {
  mode: 'text',
  text: '',
  bright: 6,
  speed: 45,
  anim: 'heartbeat',
  draw: new Uint8Array(32),
  playlist: [],
  dwell: 30,
  night: { on: false, from: '22:30', to: '07:00', level: 0 },
  clock: false,
};

const preview = new Preview($('#preview'));

// --- sending ---------------------------------------------------------------

const pending = new Map();
let flushTimer = null;

/**
 * Queues a command, collapsing repeats of the same type. Dragging a slider
 * produces one write per idle moment instead of one per pixel of travel.
 */
function send(command, delay = 200) {
  pending.set(command.c, command);
  clearTimeout(flushTimer);
  flushTimer = setTimeout(flush, delay);
}

/** Sends immediately, for taps where any delay would feel unresponsive. */
function sendNow(command) {
  pending.set(command.c, command);
  clearTimeout(flushTimer);
  flush();
}

function flush() {
  const queued = [...pending.values()];
  pending.clear();
  for (const command of queued) {
    link.send(command);
  }
}

/** Repaints the preview from `view` after a local edit. */
function refresh() {
  preview.setSettings(view);
}

const ctx = { view, send, sendNow, refresh, preview };

// --- panels ----------------------------------------------------------------

const panels = {
  text: setupMessage(ctx),
  anim: setupAnimations(ctx),
  draw: setupDrawing(ctx),
  playlist: setupPlaylist(ctx),
};
const dials = setupDials(ctx);
const night = setupNight(ctx);

function showMode(mode, { announce = true } = {}) {
  view.mode = mode;
  for (const chip of document.querySelectorAll('.chip')) {
    chip.classList.toggle('is-active', chip.dataset.mode === mode);
  }
  for (const panel of document.querySelectorAll('.panel')) {
    panel.classList.toggle('is-active', panel.dataset.panel === mode);
  }
  // Speed only means anything while something is scrolling.
  $('#speed-dial').hidden = mode === 'draw' || mode === 'anim';

  refresh();
  if (announce) {
    sendNow({ c: 'mode', v: mode });
  }
}

for (const chip of document.querySelectorAll('.chip')) {
  chip.addEventListener('click', () => showMode(chip.dataset.mode));
}

// --- device state ----------------------------------------------------------

link.onState = (state) => {
  view.mode = state.mode ?? view.mode;
  view.text = state.text ?? view.text;
  view.bright = state.bright ?? view.bright;
  view.speed = state.speed ?? view.speed;
  view.anim = state.anim ?? view.anim;
  view.playlist = state.playlist ?? view.playlist;
  view.dwell = state.dwell ?? view.dwell;
  view.night = state.night ?? view.night;
  view.clock = state.clock ?? view.clock;

  const columns = decodeColumns(state.draw);
  if (columns.length === 32) {
    view.draw = columns;
  }

  // Hydrate every control, then repaint.
  for (const panel of Object.values(panels)) {
    panel.hydrate?.();
  }
  dials.hydrate();
  night.hydrate();
  showMode(view.mode, { announce: false });
};

link.onConnection = (connected) => {
  const indicator = $('#link');
  indicator.classList.toggle('link--on', connected);
  indicator.classList.toggle('link--off', !connected);
  $('#link-text').textContent = connected ? 'połączono' : 'rozłączono';

  if (connected) {
    $('#gate').hidden = true;
    $('#app').hidden = false;
    preview.start();
  }
};

// --- connecting ------------------------------------------------------------

const gateNote = $('#gate-note');
const connectButton = $('#connect');

function explainUnavailable() {
  if (!Transport.secureContext) {
    return 'Ta strona musi być otwarta przez https — bez tego telefon nie pozwala na Bluetooth.';
  }
  return 'Ta przeglądarka nie obsługuje Bluetooth. Na iPhonie otwórz stronę w aplikacji Bluefy.';
}

connectButton.addEventListener('click', async () => {
  if (!Transport.available || !Transport.secureContext) {
    gateNote.textContent = explainUnavailable();
    return;
  }

  connectButton.disabled = true;
  gateNote.textContent = 'Szukam tabliczki…';
  try {
    // Must run inside the tap: iOS will not open the chooser otherwise.
    await link.connect();
    gateNote.textContent = '';
  } catch (error) {
    gateNote.textContent = error?.name === 'NotFoundError'
      ? 'Nie wybrano tabliczki. Spróbuj jeszcze raz — powinna nazywać się „Milena ♥”.'
      : 'Nie udało się połączyć. Sprawdź, czy tabliczka jest podłączona do prądu.';
  } finally {
    connectButton.disabled = false;
  }
});

// --- start -----------------------------------------------------------------

preview.setSettings(view);

setupIntro(() => {
  $('#gate').hidden = false;
  if (!Transport.available || !Transport.secureContext) {
    gateNote.textContent = explainUnavailable();
  }
});
