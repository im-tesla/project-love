// The opening moment.
//
// Plays once per visit rather than on every reload, so it stays a small event
// instead of an obstacle between her and the controls. Tapping skips it.

const TOTAL_MS = 3400;
const SEEN_KEY = 'love.intro.seen';

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

  // Already seen this session -- go straight to the point.
  if (sessionStorage.getItem(SEEN_KEY)) {
    intro.remove();
    onDone();
    return;
  }
  sessionStorage.setItem(SEEN_KEY, '1');

  intro.addEventListener('click', finish);
  setTimeout(finish, TOTAL_MS);
}
