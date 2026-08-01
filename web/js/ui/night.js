// Night mode.
//
// The board has no real-time clock, so it genuinely does not know the hour
// until the phone tells it. The app syncs on every connect, but after an
// unplug there is a window where the device is awake and clockless. Rather
// than let the matrix blank at the wrong hour, the firmware keeps night mode
// idle until the first sync -- and this panel says so plainly instead of
// looking broken.

import { parseClockTime } from '../protocol.js';
import { createTimePicker } from './time-picker.js';

export function setupNight({ view, send, sendNow }) {
  const toggle = document.querySelector('#night-on');
  const times = document.querySelector('#night-times');
  const from = createTimePicker(document.querySelector('#night-from'), {
    value: view.night.from,
    label: 'od',
  });
  const to = createTimePicker(document.querySelector('#night-to'), {
    value: view.night.to,
    label: 'do',
  });
  const note = document.querySelector('#night-note');

  function payload() {
    return {
      c: 'night',
      on: view.night.on,
      from: view.night.from,
      to: view.night.to,
      level: view.night.level ?? 0,
    };
  }

  function showNote() {
    if (view.night.on && !view.clock) {
      note.hidden = false;
      note.textContent =
        'Tabliczka pozna godzinę, gdy się połączysz — do tego czasu nie gaśnie.';
    } else {
      note.hidden = true;
    }
  }

  toggle.addEventListener('change', () => {
    view.night.on = toggle.checked;
    times.hidden = !toggle.checked;
    showNote();
    sendNow(payload());
  });

  for (const [element, key] of [[from, 'from'], [to, 'to']]) {
    element.addEventListener('change', () => {
      // An empty or half-typed time input would otherwise send "" and be
      // silently ignored by the device, leaving the UI out of step.
      if (parseClockTime(element.value) === null) {
        element.value = view.night[key];
        return;
      }
      view.night[key] = element.value;
      send(payload(), 300);
    });
  }

  return {
    hydrate() {
      toggle.checked = view.night.on;
      times.hidden = !view.night.on;
      // `from`/`to` are compound elements now (an hour <select> and a minute
      // <select>), so the focused node is one of their children, never the
      // container itself -- .contains() is what generalises the original
      // "don't overwrite what she's mid-edit" check to that shape.
      if (!from.contains(document.activeElement)) from.value = view.night.from;
      if (!to.contains(document.activeElement)) to.value = view.night.to;
      showNote();
    },
  };
}
