// The opening moment.
//
// Plays every time the page opens -- it is the point of the thing, not a
// loading screen to get past. Tapping skips it for the times she just wants to
// change a word.

const TOTAL_MS = 3400;

export function setupIntro(onDone) {
  const intro = document.querySelector('#intro');
  let finished = false;

  const finish = () => {
    if (finished) return;
    finished = true;
    intro.classList.add('is-leaving');
    intro.addEventListener('animationend', () => intro.remove(), { once: true });
    // Belt and braces: if the animation never fires (reduced motion cuts it to
    // 0.01ms, and some browsers skip the event) the element still goes.
    setTimeout(() => intro.remove(), 800);
    onDone();
  };

  intro.addEventListener('click', finish);
  setTimeout(finish, TOTAL_MS);
}
