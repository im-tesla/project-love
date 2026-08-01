// Brightness and speed.
//
// Speed is presented inverted. The firmware wants milliseconds per column, so
// a smaller number is faster -- but a slider pushed right should mean "more",
// and "more" here means quicker. The flip happens at this boundary and nowhere
// else.

const SPEED_MIN = 15;
const SPEED_MAX = 160;

const toSliderSpeed = (ms) => SPEED_MIN + SPEED_MAX - ms;
const fromSliderSpeed = (value) => SPEED_MIN + SPEED_MAX - value;

export function setupDials({ view, send, refresh }) {
  const brightness = document.querySelector('#brightness');
  const brightnessValue = document.querySelector('#brightness-value');
  const speed = document.querySelector('#speed');
  const speedValue = document.querySelector('#speed-value');

  function showBrightness() {
    brightnessValue.textContent = `${Math.round((view.bright / 15) * 100)}%`;
  }

  function showSpeed() {
    // A plain percentage reads better than "45 ms" for someone who is just
    // trying to make the words go slower.
    const fraction = (SPEED_MAX - view.speed) / (SPEED_MAX - SPEED_MIN);
    speedValue.textContent = `${Math.round(fraction * 100)}%`;
  }

  brightness.addEventListener('input', () => {
    view.bright = Number(brightness.value);
    showBrightness();
    send({ c: 'bright', v: view.bright }, 150);
  });

  speed.addEventListener('input', () => {
    view.speed = fromSliderSpeed(Number(speed.value));
    showSpeed();
    refresh();   // the preview scrolls at the new rate straight away
    send({ c: 'speed', v: view.speed }, 150);
  });

  return {
    hydrate() {
      if (document.activeElement !== brightness) {
        brightness.value = String(view.bright);
      }
      if (document.activeElement !== speed) {
        speed.value = String(toSliderSpeed(view.speed));
      }
      showBrightness();
      showSpeed();
    },
  };
}
