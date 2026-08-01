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
  //
  // Wrapped because storage is not always available: iOS private browsing and
  // sandboxed frames both throw on access, and losing the intro is a far
  // better outcome than losing the whole app to an exception here.
  try {
    if (sessionStorage.getItem(SEEN_KEY)) {
      intro.remove();
      onDone();
      return;
    }
    sessionStorage.setItem(SEEN_KEY, '1');
  } catch {
    // Storage unavailable -- play it every time.
  }

  intro.addEventListener('click', finish);
  setTimeout(finish, TOTAL_MS);
}
