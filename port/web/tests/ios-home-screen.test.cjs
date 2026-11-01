/* iPhone/iPad Safari install guide, without a real browser or game data. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {isIOSDevice, isIOSSafari, isStandalone, installIOSHomeScreen} =
  require('../site/ios-home-screen.js');

const IPHONE = 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1';
const IPAD_DESKTOP = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15';
const CHROME_IOS = 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) CriOS/117.0 Mobile/15E148 Safari/604.1';
const MAC_SAFARI = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 13_6) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15';

function fixture(ua, platform, touches, {standalone = false, mode = ''} = {}) {
  const events = {};
  const panel = {hidden: true, scrolls: 0, scrollIntoView() {this.scrolls++;}};
  const dismiss = {addEventListener(type, fn) {events[type] = fn;}};
  const classes = new Set();
  const doc = {
    body: {classList: {toggle(key, present) {
      if (present) classes.add(key); else classes.delete(key);
    }}},
    getElementById(id) {
      return id === 'ios-app-guide' ? panel :
        id === 'ios-app-guide-dismiss' ? dismiss : null;
    }
  };
  const win = {
    navigator: {userAgent: ua, platform, maxTouchPoints: touches, standalone},
    matchMedia(query) {return {matches: query === `(display-mode: ${mode})`};}
  };
  return {win, doc, panel, classes, events};
}

test('Safari on iPhone shows instructions; fullscreen button can reopen them', () => {
  const f = fixture(IPHONE, 'iPhone', 5);
  const result = installIOSHomeScreen(f.win, f.doc);
  assert.equal(result.safari, true);
  assert.equal(f.panel.hidden, false);
  assert.equal(f.classes.has('ios-web-app'), false);
  f.events.click();
  assert.equal(f.panel.hidden, true);
  assert.equal(f.win.openrealmShowIOSInstallGuide(), true);
  assert.equal(f.panel.hidden, false);
  assert.equal(f.panel.scrolls, 1);
});

test('desktop-UA iPad Safari detected using touch capability', () => {
  const f = fixture(IPAD_DESKTOP, 'MacIntel', 5);
  assert.equal(isIOSDevice(f.win.navigator), true);
  assert.equal(isIOSSafari(f.win.navigator), true);
  installIOSHomeScreen(f.win, f.doc);
  assert.equal(f.panel.hidden, false);
});

test('installed Home Screen app hides prompt and selects viewport mode', () => {
  for (const option of [{standalone: true}, {mode: 'standalone'}, {mode: 'fullscreen'}]) {
    const f = fixture(IPHONE, 'iPhone', 5, option);
    assert.equal(isStandalone(f.win), true);
    const result = installIOSHomeScreen(f.win, f.doc);
    assert.equal(result.installed, true);
    assert.equal(f.panel.hidden, true);
    assert.equal(f.classes.has('ios-web-app'), true);
    assert.equal(f.win.openrealmShowIOSInstallGuide(), false);
  }
});

test('desktop Safari and iOS Chrome do not show Safari-specific instructions', () => {
  for (const [ua, platform, touches] of [[MAC_SAFARI, 'MacIntel', 0],
                                       [CHROME_IOS, 'iPhone', 5]]) {
    const f = fixture(ua, platform, touches);
    assert.equal(isIOSSafari(f.win.navigator), false);
    installIOSHomeScreen(f.win, f.doc);
    assert.equal(f.panel.hidden, true);
    assert.equal(f.win.openrealmShowIOSInstallGuide(), false);
  }
});

test('web app metadata, icon paths and installed canvas sizing are shipped in the shell', () => {
  const shell = fs.readFileSync(path.resolve(__dirname, '../site/engine-shell.html'), 'utf8');
  const manifest = JSON.parse(fs.readFileSync(
    path.resolve(__dirname, '../site/manifest.webmanifest'), 'utf8'));
  assert.match(shell, /name="apple-mobile-web-app-capable" content="yes"/);
  assert.match(shell, /rel="apple-touch-icon" href="icons\/openrealm-180\.png"/);
  assert.match(shell, /rel="manifest" href="manifest\.webmanifest"/);
  assert.match(shell, /body\.ios-web-app #game-container:not\(\[hidden\]\)/);
  assert.match(shell, /height: 100dvh/);
  assert.match(shell, /id="ios-app-guide"/);
  assert.equal(manifest.display, 'fullscreen');
  assert.equal(manifest.start_url, './');
  assert.equal(manifest.scope, './');
  assert.equal(manifest.orientation, 'landscape');
});
