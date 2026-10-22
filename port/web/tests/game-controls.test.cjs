/* Browser game controls must remain outside the canvas fullscreen surface. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const html = fs.readFileSync(path.resolve(__dirname, '../site/engine-shell.html'), 'utf8');

test('game controls are siblings of the canvas inside the fullscreen container', () => {
  assert.match(html, /<div id="game-container" hidden>\s*<canvas[^>]*><\/canvas>\s*<div id="early-map-loading"[^>]*>[\s\S]*?<\/div>\s*<div id="game-controls"/);
  assert.match(html, /id="game-escape"/);
  assert.match(html, /id="game-fullscreen"/);
  assert.match(html, /id="game-shortcut-menu"/);
  assert.match(html, /id="game-shortcut-groups"/);
  assert.match(html, /data-game-key="F9"/);
  assert.match(html, /data-game-key="F12"/);
  for (let digit = 1; digit <= 9; digit++) {
    assert.match(html, new RegExp('data-game-key="' + digit + '" aria-label="Recall group ' + digit));
    assert.match(html, new RegExp('data-game-key="' + digit + '" data-game-modifier="Control"'));
    assert.match(html, new RegExp('data-game-key="' + digit + '" data-game-modifier="Shift"'));
  }
});

test('control bar remains above the canvas and fullscreen targets the wrapper', () => {
  assert.match(html, /#game-container \{[^}]*isolation: isolate/);
  assert.match(html, /#game-controls \{[^}]*z-index: 2147483647/);
  assert.match(html, /src="fullscreen-controls.js"/);
  assert.match(html, /openrealmInstallFullscreenControls\(window, document, gameContainer, gameCanvas/);
  assert.match(html, /#game-container\.is-fullscreen canvas \{[^}]*width:\s*100% !important/);
});

test('menus use the existing canvas keyboard path and ship in the web build', () => {
  const build = fs.readFileSync(path.resolve(__dirname, '../build-engine.py'), 'utf8');
  const config = fs.readFileSync(path.resolve(__dirname, '../../../games/warcraft-3/share/config.cfg'), 'utf8');
  assert.match(html, /src="game-shortcut-menus.js"/);
  assert.match(html, /openrealmInstallGameShortcutMenus\(window, document, gameCanvas/);
  assert.match(html, /grid-template-columns:\s*2fr 1fr 1fr/);
  assert.match(build, /"game-shortcut-menus.js"/);
  assert.match(config, /bind F12 "screenshot"/);
  assert.match(build, /bind F12 "cmd log"/);
  assert.match(html, />Message Log <span>F12<\/span>/);
});
