/* Browser game controls must remain outside the canvas fullscreen surface. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const html = fs.readFileSync(path.resolve(__dirname, '../site/engine-shell.html'), 'utf8');

test('game controls are siblings of the canvas inside the fullscreen container', () => {
  assert.match(html, /<div id="game-container">\s*<canvas[^>]*><\/canvas>\s*<div id="game-controls"/);
  assert.match(html, /id="game-escape"/);
  assert.match(html, /id="game-fullscreen"/);
});

test('control bar remains above the canvas and fullscreen targets the wrapper', () => {
  assert.match(html, /#game-container \{[^}]*isolation: isolate/);
  assert.match(html, /#game-controls \{[^}]*z-index: 2147483647/);
  assert.match(html, /gameCanvas\.requestFullscreen = function \(options\)/);
  assert.match(html, /return gameContainer\.requestFullscreen\(options\)/);
  assert.match(html, /gameContainer\.requestFullscreen\(\)\.catch/);
});
