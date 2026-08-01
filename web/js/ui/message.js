// The message panel: the thing she will use ninety percent of the time.

import { textWidth } from '../preview.js';

export function setupMessage({ view, send, refresh }) {
  const input = document.querySelector('#text-input');
  const count = document.querySelector('#text-count');

  function describe() {
    const width = textWidth(view.text);
    if (view.text.length === 0) {
      count.textContent = 'Puste, tabliczka nic nie pokaże.';
    } else if (width <= 32) {
      count.textContent = 'Zmieści się w całości.';
    } else {
      count.textContent = 'Za długie, będzie się przewijać.';
    }
  }

  function edited() {
    view.text = input.value;
    refresh();       // the preview follows her finger
    describe();
    send({ c: 'text', v: view.text }, 260);
  }

  input.addEventListener('input', edited);

  // Insert at the caret rather than appending, and keep focus so the keyboard
  // does not close between taps.
  for (const key of document.querySelectorAll('.palette__key')) {
    key.addEventListener('click', () => {
      const symbol = key.dataset.insert;
      const start = input.selectionStart ?? input.value.length;
      const end = input.selectionEnd ?? input.value.length;
      input.value = input.value.slice(0, start) + symbol + input.value.slice(end);
      const caret = start + symbol.length;
      input.setSelectionRange(caret, caret);
      input.focus();
      edited();
    });
  }

  return {
    hydrate() {
      // Never overwrite what she is in the middle of typing.
      if (document.activeElement !== input) {
        input.value = view.text ?? '';
      }
      describe();
    },
  };
}
