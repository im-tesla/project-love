// A handful of messages, shown one after another.

const MAX_SLOTS = 6;   // matches LOVE_PLAYLIST_SLOTS
const MIN_DWELL = 5;
const MAX_DWELL = 300;

export function setupPlaylist({ view, send, sendNow, refresh }) {
  const list = document.querySelector('#playlist');
  const addButton = document.querySelector('#playlist-add');
  const dwellValue = document.querySelector('#dwell-value');

  function commit(delay = 400) {
    send({ c: 'playlist', v: view.playlist, dwell: view.dwell }, delay);
    refresh();
  }

  function renderDwell() {
    dwellValue.textContent = `${view.dwell} s`;
  }

  function renderList() {
    list.replaceChildren();

    if (view.playlist.length === 0) {
      const empty = document.createElement('li');
      empty.className = 'list__empty';
      empty.textContent = 'Jeszcze nic tu nie ma. Dodaj pierwszą wiadomość.';
      list.append(empty);
    }

    view.playlist.forEach((message, index) => {
      const item = document.createElement('li');
      item.className = 'list__item';

      const number = document.createElement('span');
      number.className = 'list__index';
      number.textContent = `${index + 1}.`;

      const input = document.createElement('input');
      input.className = 'list__input';
      input.type = 'text';
      input.value = message;
      input.maxLength = 120;
      input.placeholder = 'Napisz coś…';
      input.addEventListener('input', () => {
        view.playlist[index] = input.value;
        commit();
      });

      const remove = document.createElement('button');
      remove.type = 'button';
      remove.className = 'btn btn--ghost';
      remove.setAttribute('aria-label', `Usuń wiadomość ${index + 1}`);
      remove.textContent = '×';
      remove.addEventListener('click', () => {
        view.playlist.splice(index, 1);
        renderList();
        commit(0);
      });

      item.append(number, input, remove);
      list.append(item);
    });

    addButton.disabled = view.playlist.length >= MAX_SLOTS;
    addButton.textContent = addButton.disabled
      ? `Więcej się nie zmieści (${MAX_SLOTS})`
      : 'Dodaj wiadomość';
  }

  addButton.addEventListener('click', () => {
    if (view.playlist.length >= MAX_SLOTS) return;
    view.playlist.push('');
    view.mode = 'playlist';
    renderList();
    // Put the cursor straight in the box she just created.
    list.querySelector('.list__item:last-child .list__input')?.focus();
    commit(0);
  });

  for (const button of document.querySelectorAll('[data-dwell]')) {
    button.addEventListener('click', () => {
      const step = Number(button.dataset.dwell);
      view.dwell = Math.min(MAX_DWELL, Math.max(MIN_DWELL, view.dwell + step));
      renderDwell();
      sendNow({ c: 'playlist', v: view.playlist, dwell: view.dwell });
      refresh();
    });
  }

  return {
    hydrate() {
      // Rebuilding the list steals focus, so leave it alone while she types.
      if (!list.contains(document.activeElement)) {
        renderList();
      }
      renderDwell();
    },
  };
}
