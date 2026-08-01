// The animation gallery.
//
// Each card runs the real animation at its real speed, driven by one shared
// frame loop rather than four. Choosing is a matter of looking rather than
// reading four Polish nouns and guessing.

import { ANIMATIONS, COLUMNS, renderAnimation } from '../animations.js';
import { paint } from '../preview.js';

export function setupAnimations({ view, sendNow, refresh }) {
  const gallery = document.querySelector('#anim-gallery');
  const cards = new Map();
  const frame = new Uint8Array(COLUMNS);

  for (const animation of ANIMATIONS) {
    const card = document.createElement('button');
    card.type = 'button';
    card.className = 'card';
    card.dataset.anim = animation.id;

    const canvas = document.createElement('canvas');
    canvas.className = 'card__canvas';
    canvas.width = 256;
    canvas.height = 64;

    const name = document.createElement('span');
    name.className = 'card__name';
    name.textContent = animation.label;

    card.append(canvas, name);
    gallery.append(card);
    cards.set(animation.id, { card, canvas });

    card.addEventListener('click', () => {
      view.anim = animation.id;
      view.mode = 'anim';
      select();
      refresh();
      sendNow({ c: 'anim', v: animation.id });
    });
  }

  function select() {
    for (const [id, { card }] of cards) {
      card.classList.toggle('is-active', id === view.anim);
    }
  }

  // One loop for every card. Each is a pure function of time, so they simply
  // read the same clock.
  function tick(now) {
    if (document.querySelector('.panel[data-panel="anim"]')?.classList.contains('is-active')) {
      for (const [id, { canvas }] of cards) {
        renderAnimation(id, frame, now);
        paint(canvas, frame);
      }
    }
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);

  return { hydrate: select };
}
