// A minimal custom time control, built to replace <input type="time">
// entirely rather than patch around it.
//
// That native control renders wildly differently across browsers: Chrome
// draws something close to a normal form field, while iOS Safari draws a
// wide native pill with OS-controlled chrome that ignores width, padding and
// border altogether -- there is no CSS override that reliably fixes it,
// because the browser is drawing its own widget, not reading ours.
//
// Two <select> elements size themselves from their own option text, which we
// fully control (two digits each), so their closed/inline width is small and
// identical on every platform. Tapping one still opens that platform's own
// picker UI -- a wheel on iOS, a dropdown elsewhere -- but that popup has no
// effect on the inline layout the way input[type=time] does.
//
// Exposes the same shape the rest of the app already expects from a plain
// input, so callers do not need to change: a `.value` "HH:MM" string
// (get/set) and a `change` event that bubbles.

const HOURS = Array.from({ length: 24 }, (_, i) => String(i).padStart(2, '0'));
const MINUTES = Array.from({ length: 60 }, (_, i) => String(i).padStart(2, '0'));

function buildSelect(options, ariaLabel) {
  const select = document.createElement('select');
  select.className = 'time-picker__part';
  select.setAttribute('aria-label', ariaLabel);
  for (const value of options) {
    const option = document.createElement('option');
    option.value = value;
    option.textContent = value;
    select.appendChild(option);
  }
  return select;
}

/**
 * @param {HTMLElement} container an empty element to turn into the picker
 * @param {{value?: string, label?: string}} [options]
 *   `value` is an initial "HH:MM" string. `label` (e.g. "od", "do") is folded
 *   into each select's aria-label so screen readers say which field this is.
 * @returns {HTMLElement} the same container, now populated and interactive
 */
export function createTimePicker(container, { value, label } = {}) {
  container.classList.add('time-picker');
  container.innerHTML = '';

  const hour = buildSelect(HOURS, label ? `Godzina, ${label}` : 'Godzina');
  const minute = buildSelect(MINUTES, label ? `Minuta, ${label}` : 'Minuta');

  const colon = document.createElement('span');
  colon.className = 'time-picker__colon';
  colon.setAttribute('aria-hidden', 'true');
  colon.textContent = ':';

  container.append(hour, colon, minute);

  function setValue(text) {
    const match = /^(\d{2}):(\d{2})$/.exec(text ?? '');
    if (!match) return;
    hour.value = match[1];
    minute.value = match[2];
  }
  setValue(value);

  Object.defineProperty(container, 'value', {
    get: () => `${hour.value}:${minute.value}`,
    set: setValue,
  });

  const notify = () => container.dispatchEvent(new Event('change', { bubbles: true }));
  hour.addEventListener('change', notify);
  minute.addEventListener('change', notify);

  return container;
}
