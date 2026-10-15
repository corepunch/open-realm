/* Browser-local data installer; engine data selection is validated by filesystem-bridge.js. */
(function () {
  'use strict';
  const fileInput = document.getElementById('game-files');
  const install = document.getElementById('install-data');
  const cancel = document.getElementById('cancel-install');
  const remove = document.getElementById('remove-data');
  const status = document.getElementById('data-status');
  const progress = document.getElementById('install-progress');
  const selection = document.getElementById('selection-summary');
  const discovered = document.getElementById('discovered-files');
  const archives = document.getElementById('archive-list');
  const editionNotice = document.getElementById('installation-edition');
  if (!navigator.storage?.getDirectory || !window.Worker || !navigator.locks?.request) {
    status.textContent = 'OPFS, Web Workers and Web Locks are required to install files safely in this browser.';
    install.disabled = true;
    fileInput.disabled = true;
    remove.disabled = true;
    return;
  }
  const worker = new Worker('install-worker.js');
  let operation = null;
  let warnings = [];
  const sizeLabel = bytes => {
    if (bytes < 1024) return bytes + ' B';
    const unit = bytes < 1024 * 1024 ? 'KiB' : bytes < 1024 ** 3 ? 'MiB' : 'GiB';
    const divisor = unit === 'KiB' ? 1024 : unit === 'MiB' ? 1024 ** 2 : 1024 ** 3;
    return (bytes / divisor).toFixed(1) + ' ' + unit;
  };
  function setOperation(value) {
    operation = value;
    const busy = value !== null;
    fileInput.disabled = busy;
    install.disabled = busy;
    cancel.disabled = value !== 'import';
    remove.disabled = busy;
  }
  function clearDiscovery() {
    archives.replaceChildren();
    discovered.hidden = true;
  }
  function showEdition(manifest) {
    if (!editionNotice) return;
    const isDemo = manifest && manifest.edition === 'demo';
    editionNotice.hidden = !isDemo;
    editionNotice.textContent = isDemo
      ? 'Warcraft III Demo — limited demo content only, not the full Reign of Chaos or Frozen Throne game.'
      : '';
  }
  function showSelection() {
    const files = Array.from(fileInput.files || []);
    const isos = files.filter(file => /\.iso$/i.test(file.name)).length;
    const direct = files.length - isos;
    selection.textContent = files.length
      ? `Selected ${isos} ISO image(s) and ${direct} standalone archive/map file(s). ` +
        'The selected files will replace the active installation.'
      : 'No files selected.';
  }
  worker.onmessage = ({data}) => {
    if (data.type === 'notice' && operation === 'import') {
      warnings.push(data.message);
      status.textContent = data.message;
    } else if (data.type === 'phase' && operation === 'import') {
      if (data.phase === 'scanning') status.textContent = 'Scanning ISO: ' + data.source;
      else if (data.phase === 'extracting') status.textContent = 'Extracting supported maps from Setup.mpq…';
      else if (data.phase === 'validating') status.textContent = 'Validating game archives…';
      else if (data.phase === 'storage') status.textContent = 'Checking available browser storage…';
      else if (data.phase === 'copying') status.textContent = 'Copying detected game archives…';
      else if (data.phase === 'committing') status.textContent = 'Saving installation manifest…';
    } else if (data.type === 'discovered' && operation === 'import') {
      // Names come from the selected ISO: never parse them as HTML.
      clearDiscovery();
      for (const file of data.files) {
        const item = document.createElement('li');
        item.textContent = `${file.name} (${sizeLabel(file.size)}) — ${file.source}`;
        archives.appendChild(item);
      }
      discovered.hidden = false;
      status.textContent = `Found ${data.files.length} game file(s), ${sizeLabel(data.bytes)} to copy.`;
    } else if (data.type === 'progress' && operation === 'import') {
      progress.value = data.total ? data.copied / data.total : 0;
      const fileNumber = data.index ? ` (${data.index}/${data.count})` : '';
      status.textContent = `Copying ${data.file}${fileNumber}: ` +
        `${sizeLabel(data.copied)} / ${sizeLabel(data.total)} (${Math.round(100 * progress.value)}%).`;
    } else if (data.type === 'done' && operation === 'import') {
      setOperation(null);
      progress.value = 1;
      showEdition(data.manifest);
      status.textContent = (data.manifest.edition === 'demo'
        ? 'Warcraft III Demo installed (limited content). ' :
        data.manifest.completeness === 'partial-expansion'
        ? 'Partial Frozen Throne installation: expansion localization archives are missing. ' : '') +
        `Stored ${data.manifest.files.length} game file(s) locally ` +
        `(${sizeLabel(data.manifest.bytes)}). Reload this page to use the new data.` +
        (warnings.length ? ' Note: ' + warnings.join(' | ') : '');
      fileInput.value = '';
      showSelection();
    } else if (data.type === 'list') {
      if (operation === 'import') return; // Ignore a delayed initial list response.
      setOperation(null);
      const manifest = data.manifest;
      showEdition(manifest);
      status.textContent = manifest
        ? (manifest.edition === 'demo' ? 'Warcraft III Demo installed (limited content). ' :
            manifest.completeness === 'partial-expansion' ? 'Partial expansion installation. ' : '') +
          'Local files: ' + manifest.files.map(file => file.name).join(', ')
        : 'No game data installed.';
      if (!manifest) clearDiscovery();
    } else if (data.type === 'error') {
      setOperation(null);
      status.textContent = data.message === 'Import cancelled'
        ? 'Import cancelled. Previous installed data (if any) was retained.'
        : 'Import error: ' + data.message;
    }
  };
  worker.onerror = () => {
    setOperation(null);
    status.textContent = 'Game-data worker failed. Reload the page before retrying.';
  };
  fileInput.addEventListener('change', showSelection);
  install.addEventListener('click', () => {
    if (operation) return;
    const files = Array.from(fileInput.files || []);
    if (!files.length) { status.textContent = 'Choose Warcraft III ISO or game archive files first.'; return; }
    clearDiscovery();
    warnings = [];
    progress.removeAttribute('value'); // Unknown copy size while discs are scanned.
    setOperation('import');
    status.textContent = 'Inspecting selected game files…';
    worker.postMessage({type:'import', files});
  });
  cancel.addEventListener('click', () => {
    if (operation !== 'import') return;
    cancel.disabled = true;
    status.textContent = 'Cancelling import…';
    worker.postMessage({type:'cancel'});
  });
  remove.addEventListener('click', () => {
    if (operation || !confirm('Delete all imported Warcraft III files from this browser?')) return;
    setOperation('remove');
    status.textContent = 'Removing local Warcraft III data…';
    worker.postMessage({type:'remove'});
  });
  showSelection();
  worker.postMessage({type:'list'});
})();
