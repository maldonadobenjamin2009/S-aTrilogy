(() => {
  'use strict';

  const modal = document.getElementById('player-modal');
  const playerFrame = document.getElementById('player-frame');
  const playerTitle = document.getElementById('player-title');
  const closeButton = document.getElementById('close-player');
  let activeRomUrl = null;
  let previouslyFocused = null;

  const titles = {
    'sonic-advance': 'Sonic Advance',
    'sonic-advance-2': 'Sonic Advance 2',
    'sonic-advance-3': 'Sonic Advance 3'
  };

  document.querySelectorAll('[data-pick]').forEach((button) => {
    button.addEventListener('click', () => {
      const gameId = button.dataset.pick;
      const input = document.querySelector(`[data-rom-input="${gameId}"]`);
      if (input) {
        input.value = '';
        input.click();
      }
    });
  });

  document.querySelectorAll('[data-rom-input]').forEach((input) => {
    input.addEventListener('change', () => {
      const file = input.files && input.files[0];
      if (!file) return;

      const gameId = input.dataset.romInput;
      const gameTitle = titles[gameId] || 'Sonic Advance Trilogy';
      const fileLabel = document.querySelector(`[data-file-label="${gameId}"]`);
      if (fileLabel) fileLabel.textContent = `Seleccionada: ${file.name}`;

      launchGame(file, gameTitle);
    });
  });

  function launchGame(file, gameTitle) {
    closeCurrentRom();
    activeRomUrl = URL.createObjectURL(file);
    previouslyFocused = document.activeElement;
    playerTitle.textContent = gameTitle;

    // URLSearchParams codifica de forma segura la URL blob del archivo local.
    // player.html y esta página tienen el mismo origen en GitHub Pages.
    const playerUrl = new URL('./player.html', window.location.href);
    playerUrl.searchParams.set('rom', activeRomUrl);
    playerUrl.searchParams.set('name', gameTitle);

    modal.hidden = false;
    document.body.classList.add('modal-open');
    playerFrame.src = playerUrl.toString();
    closeButton.focus({ preventScroll: true });
  }

  function closeCurrentRom() {
    if (playerFrame.getAttribute('src') && playerFrame.getAttribute('src') !== 'about:blank') {
      playerFrame.src = 'about:blank';
    }
    if (activeRomUrl) {
      const oldUrl = activeRomUrl;
      activeRomUrl = null;
      window.setTimeout(() => URL.revokeObjectURL(oldUrl), 300);
    }
  }

  function closePlayer() {
    if (modal.hidden) return;
    modal.hidden = true;
    document.body.classList.remove('modal-open');
    closeCurrentRom();
    if (previouslyFocused && typeof previouslyFocused.focus === 'function') {
      previouslyFocused.focus({ preventScroll: true });
    }
  }

  closeButton.addEventListener('click', closePlayer);
  modal.addEventListener('click', (event) => {
    if (event.target === modal) closePlayer();
  });
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && !modal.hidden) closePlayer();
  });
})();
