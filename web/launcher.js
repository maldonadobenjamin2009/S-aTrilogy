(function () {
  'use strict';

  const romNames = [
    'sonicadvance.gba',
    'sonicadvance2.gba',
    'sonicadvance3.gba'
  ];
  const status = document.getElementById('status');
  const startButton = document.getElementById('start-app');
  const fullscreenButton = document.getElementById('fullscreen');
  const canvas = document.getElementById('canvas');
  const overlay = document.getElementById('screen-overlay');
  let runtimeReady = false;
  let storageReady = false;
  let storagePersistent = false;
  let started = false;
  let importsPending = 0;
  let syncPending = false;
  const romInputs = Array.from(document.querySelectorAll('input[data-rom]'));
  romInputs.forEach(input => { input.disabled = true; });

  function setStatus(message, kind) {
    status.textContent = message;
    status.classList.remove('error', 'ok');
    if (kind) status.classList.add(kind);
  }

  function mkdirp(fs, path) {
    const parts = path.split('/').filter(Boolean);
    let current = '';
    for (const part of parts) {
      current += '/' + part;
      try { fs.mkdir(current); } catch (error) {
        if (!error || error.name !== 'ErrnoError') throw error;
        try { fs.stat(current); } catch (_) { throw error; }
      }
    }
  }

  function syncFilesystem(populate) {
    return new Promise((resolve, reject) => {
      const fs = window.Module && Module.FS;
      if (!fs || !storagePersistent) return resolve();
      fs.syncfs(Boolean(populate), error => error ? reject(error) : resolve());
    });
  }

  function refreshStartButton() {
    startButton.disabled = !runtimeReady || !storageReady || importsPending > 0 || started;
    romInputs.forEach(input => { input.disabled = !storageReady; });
  }

  function markRomStatus(name, message) {
    const node = document.querySelector('[data-status-for="' + name + '"]');
    if (node) node.textContent = message;
  }

  function copyStoredRomsToWorkingFS() {
    const fs = Module.FS;
    mkdirp(fs, '/main/roms');
    mkdirp(fs, '/persistent/roms');
    for (const name of romNames) {
      const stored = '/persistent/roms/' + name;
      try {
        const data = fs.readFile(stored);
        fs.writeFile('/main/roms/' + name, data);
        markRomStatus(name, 'Guardada en este navegador');
      } catch (_) {
        // Es normal si la persona todavía no importó esta ROM.
      }
    }
  }

  function prepareRuntime() {
    if (runtimeReady) return;
    runtimeReady = true;
    fullscreenButton.disabled = false;
    overlay.classList.add('hidden');
    const fs = Module.FS;
    try {
      mkdirp(fs, '/main/roms');
      mkdirp(fs, '/persistent');
      const idbfs = Module.IDBFS || (fs.filesystems && fs.filesystems.IDBFS);
      if (!idbfs) throw new Error('Este runtime no expuso IDBFS.');
      fs.mount(idbfs, { autoPersist: false }, '/persistent');
      storagePersistent = true;
      fs.syncfs(true, error => {
        if (error) {
          storagePersistent = false;
          storageReady = true;
          setStatus('El almacenamiento del navegador no se pudo leer. El juego puede abrirse, pero los guardados podrían no persistir.', 'error');
        } else {
          try { copyStoredRomsToWorkingFS(); } catch (copyError) { console.warn(copyError); }
          storageReady = true;
          setStatus('Motor listo. Importá tus ROMs y pulsá «Iniciar hub».', 'ok');
        }
        refreshStartButton();
      });
      return;
    } catch (error) {
      console.warn('IDBFS no disponible; se usará memoria temporal.', error);
      storagePersistent = false;
      storageReady = true;
      setStatus('Motor listo, pero el almacenamiento persistente no está disponible; las partidas podrían perderse al recargar.', 'error');
      refreshStartButton();
    }
  }

  for (const input of document.querySelectorAll('input[data-rom]')) {
    input.addEventListener('change', async event => {
      const file = event.target.files && event.target.files[0];
      const name = event.target.dataset.rom;
      if (!file) return;
      if (file.size < 1024 || file.size > 64 * 1024 * 1024) {
        markRomStatus(name, 'Tamaño de archivo no válido');
        setStatus('El archivo seleccionado no parece tener un tamaño válido para una ROM de GBA.', 'error');
        input.value = '';
        return;
      }
      importsPending++;
      refreshStartButton();
      markRomStatus(name, 'Importando ' + file.name + '…');
      setStatus('Importando ' + file.name + ' en el almacenamiento local…');
      try {
        const buffer = await file.arrayBuffer();
        const bytes = new Uint8Array(buffer);
        const fs = Module.FS;
        mkdirp(fs, '/main/roms');
        fs.writeFile('/main/roms/' + name, bytes);
        if (storagePersistent) {
          mkdirp(fs, '/persistent/roms');
          fs.writeFile('/persistent/roms/' + name, bytes);
          await syncFilesystem(false);
        }
        markRomStatus(name, file.name + ' · ' + (file.size / (1024 * 1024)).toFixed(1) + ' MB');
        setStatus('ROM importada. El launcher C la detectará automáticamente.', 'ok');
      } catch (error) {
        console.error(error);
        markRomStatus(name, 'Error al importar');
        setStatus('No se pudo importar la ROM: ' + (error && error.message ? error.message : error), 'error');
      } finally {
        importsPending--;
        refreshStartButton();
      }
    });
  }

  startButton.addEventListener('click', () => {
    if (!runtimeReady || !storageReady || importsPending > 0 || started) return;
    try {
      // Importante: se llama sin await dentro del clic para que Chrome pueda
      // habilitar el audio WebAudio durante la activación del usuario.
      started = true;
      Module.callMain([]);
      startButton.textContent = 'Hub iniciado';
      refreshStartButton();
      canvas.focus({ preventScroll: true });
      setStatus('Hub C iniciado. Los archivos importados están disponibles en el menú original.', 'ok');
    } catch (error) {
      started = false;
      console.error(error);
      setStatus('No se pudo iniciar el programa C: ' + (error && error.message ? error.message : error), 'error');
      refreshStartButton();
    }
  });

  fullscreenButton.addEventListener('click', async () => {
    try {
      if (document.fullscreenElement) await document.exitFullscreen();
      else if (canvas.requestFullscreen) await canvas.requestFullscreen();
      canvas.focus({ preventScroll: true });
    } catch (error) {
      setStatus('Chrome no permitió la pantalla completa: ' + error.message, 'error');
    }
  });

  canvas.addEventListener('pointerdown', () => canvas.focus({ preventScroll: true }));
  window.addEventListener('sat-module-ready', prepareRuntime, { once: true });
  window.addEventListener('sat-runtime-error', event => {
    overlay.classList.remove('hidden');
    overlay.querySelector('p').textContent = 'Error al cargar WebAssembly';
    setStatus('No se pudo iniciar el runtime: ' + (event.detail || 'error desconocido'), 'error');
  });
  if (window.SATModuleInitialized) prepareRuntime();

  // Las partidas/ajustes usan IDBFS. Sincronización periódica para que el progreso
  // no dependa únicamente del evento pagehide (que puede no dar tiempo a terminar).
  window.setInterval(() => {
    if (!storagePersistent || !storageReady || syncPending) return;
    syncPending = true;
    syncFilesystem(false).catch(error => console.warn('No se pudo guardar IDBFS', error)).finally(() => { syncPending = false; });
  }, 5000);
  window.addEventListener('pagehide', () => {
    if (storagePersistent && storageReady) {
      try { Module.FS.syncfs(false, error => { if (error) console.warn(error); }); } catch (_) {}
    }
  });

  refreshStartButton();
})();
