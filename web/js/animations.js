// Port of src/animations.cpp, kept deliberately line-for-line.
//
// Both sides are pure functions of elapsed time, so the preview she sees while
// choosing an animation is the same frame the matrix will draw at that
// millisecond. If you change one file, change the other.

export const COLUMNS = 32;

// Sprites as column bytes, bit 0 = top row. Identical to animations.cpp.

// .##...##. / ######### / ######### / ######### / .#######. / ..#####.. / ...###... / ....#....
const HEART_BIG = [0x0e, 0x1f, 0x3f, 0x7e, 0xfe, 0x7e, 0x3f, 0x1f, 0x0e];

// ....... / .##.##. / ####### / ####### / .#####. / ..###.. / ...#... / .......
const HEART_MID = [0x0c, 0x1e, 0x3e, 0x7c, 0x3e, 0x1e, 0x0c];

// ..... / ..... / .#.#. / ##### / ##### / .###. / ..#.. / .....
const HEART_SMALL = [0x18, 0x3c, 0x78, 0x3c, 0x18];

// ...... / ...... / ....#. / .....# / ###### / .....# / ....#. / ......
const ARROW = [0x10, 0x10, 0x10, 0x10, 0x54, 0x38];

/** Ids as sent over BLE, with the Polish labels shown in the gallery. */
export const ANIMATIONS = [
  { id: 'heartbeat', label: 'Bicie serca', interval: 40 },
  { id: 'hearts', label: 'Serduszka', interval: 55 },
  { id: 'sparkle', label: 'Gwiazdki', interval: 110 },
  { id: 'arrow', label: 'Strzała', interval: 40 },
];

export function frameInterval(id) {
  return ANIMATIONS.find((a) => a.id === id)?.interval ?? 40;
}

function clear(frame) {
  frame.fill(0);
}

/** Draws a sprite with its left edge at column x, clipped at both ends. */
function blit(frame, sprite, x) {
  for (let i = 0; i < sprite.length; i++) {
    const col = x + i;
    if (col >= 0 && col < COLUMNS) {
      frame[col] |= sprite[i];
    }
  }
}

function blitCentred(frame, sprite) {
  blit(frame, sprite, Math.floor((COLUMNS - sprite.length) / 2));
}

/** Matches hash32() in animations.cpp, so the sparkle pattern is identical. */
function hash32(x) {
  x = Math.imul(x ^ (x >>> 16), 0x7feb352d);
  x = Math.imul(x ^ (x >>> 15), 0x846ca68b);
  return (x ^ (x >>> 16)) >>> 0;
}

// Real hearts go lub-DUB ... pause. Evenly spaced pulses just read as blinking.
function renderHeartbeat(frame, elapsedMs) {
  const t = elapsedMs % 1300;
  clear(frame);
  if (t < 130) blitCentred(frame, HEART_BIG);
  else if (t < 260) blitCentred(frame, HEART_MID);
  else if (t < 400) blitCentred(frame, HEART_BIG);
  else if (t < 520) blitCentred(frame, HEART_MID);
  else blitCentred(frame, HEART_SMALL);
}

function renderHearts(frame, elapsedMs) {
  const spacing = 13;
  const span = spacing * 3;
  clear(frame);
  const drift = Math.floor(elapsedMs / 55) % span;
  for (let i = -1; i < 4; i++) {
    blit(frame, HEART_MID, COLUMNS - drift + i * spacing);
  }
}

function renderSparkle(frame, elapsedMs) {
  const slot = Math.floor(elapsedMs / 110);
  clear(frame);
  for (let col = 0; col < COLUMNS; col++) {
    for (let row = 0; row < 8; row++) {
      // Each star lives two slots, so they fade rather than strobe.
      const a = hash32((col * 911 + row * 3571 + slot * 7919) >>> 0);
      const b = hash32((col * 911 + row * 3571 + (slot - 1) * 7919) >>> 0);
      if ((a & 0xff) < 14 || (b & 0xff) < 8) {
        frame[col] |= 1 << row;
      }
    }
  }
}

function renderArrow(frame, elapsedMs) {
  const period = 2600;
  const flight = 1400;
  const start = -ARROW.length;
  const end = COLUMNS;

  const t = elapsedMs % period;
  clear(frame);

  const struck = t > (flight * 50) / 100 && t < (flight * 75) / 100;
  if (struck) {
    blitCentred(frame, HEART_BIG);
  } else {
    blit(frame, HEART_MID, Math.floor((COLUMNS - HEART_MID.length) / 2));
  }

  if (t < flight) {
    blit(frame, ARROW, start + Math.floor((t * (end - start)) / flight));
  }
}

/**
 * Renders one frame into a Uint8Array(32). Overwrites every column.
 * Unknown ids fall back to heartbeat, as the firmware does.
 *
 * Named renderAnimation rather than render so it stays unambiguous alongside
 * preview.js's own drawing functions when the modules are flattened into a
 * single file (see tools/build_single_file.py).
 */
export function renderAnimation(id, frame, elapsedMs) {
  switch (id) {
    case 'hearts': return renderHearts(frame, elapsedMs);
    case 'sparkle': return renderSparkle(frame, elapsedMs);
    case 'arrow': return renderArrow(frame, elapsedMs);
    default: return renderHeartbeat(frame, elapsedMs);
  }
}
