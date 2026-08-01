// The drawing pad: a 32x8 grid she paints with a finger.
//
// Dragging keeps whatever the first touched pixel decided -- start on an unlit
// pixel and the drag draws, start on a lit one and it erases. Painting over
// your own line by accident is the fastest way to make a pixel editor
// infuriating.

import { encodeColumns } from '../protocol.js';
import { paint } from '../preview.js';

const WIDTH = 32;
const HEIGHT = 8;

// The same heart the firmware falls back to, so "Serduszko" here and a
// factory-fresh board agree.
const HEART = [0x0c, 0x1e, 0x3e, 0x7c, 0x3e, 0x1e, 0x0c];

export function setupDrawing({ view, sendNow, refresh }) {
  const pad = document.querySelector('#pad');
  let painting = false;
  let paintValue = true;

  function cellFrom(event) {
    const rect = pad.getBoundingClientRect();
    const col = Math.floor(((event.clientX - rect.left) / rect.width) * WIDTH);
    const row = Math.floor(((event.clientY - rect.top) / rect.height) * HEIGHT);
    if (col < 0 || col >= WIDTH || row < 0 || row >= HEIGHT) return null;
    return { col, row };
  }

  function isLit(col, row) {
    return ((view.draw[col] >> row) & 1) === 1;
  }

  function set(col, row, lit) {
    if (lit) {
      view.draw[col] |= 1 << row;
    } else {
      view.draw[col] &= ~(1 << row) & 0xff;
    }
  }

  function redraw() {
    paint(pad, view.draw);
    refresh();
  }

  function commit() {
    sendNow({ c: 'draw', v: encodeColumns(view.draw) });
  }

  pad.addEventListener('pointerdown', (event) => {
    const cell = cellFrom(event);
    if (!cell) return;
    pad.setPointerCapture(event.pointerId);
    painting = true;
    paintValue = !isLit(cell.col, cell.row);
    set(cell.col, cell.row, paintValue);
    redraw();
  });

  pad.addEventListener('pointermove', (event) => {
    if (!painting) return;
    const cell = cellFrom(event);
    if (!cell) return;
    if (isLit(cell.col, cell.row) === paintValue) return;
    set(cell.col, cell.row, paintValue);
    redraw();
  });

  const stop = () => {
    if (!painting) return;
    painting = false;
    commit();
  };
  pad.addEventListener('pointerup', stop);
  pad.addEventListener('pointercancel', stop);
  pad.addEventListener('pointerleave', stop);

  document.querySelector('#pad-clear').addEventListener('click', () => {
    view.draw.fill(0);
    view.mode = 'draw';
    redraw();
    commit();
  });

  document.querySelector('#pad-heart').addEventListener('click', () => {
    view.draw.fill(0);
    const left = Math.floor((WIDTH - HEART.length) / 2);
    for (let i = 0; i < HEART.length; i++) {
      view.draw[left + i] = HEART[i];
    }
    view.mode = 'draw';
    redraw();
    commit();
  });

  return {
    hydrate() {
      paint(pad, view.draw);
    },
  };
}
