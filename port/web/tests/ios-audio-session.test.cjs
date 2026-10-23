/* Browser-session regression without browser, Emscripten or Warcraft assets. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {createIOSAudioSession} = require('../site/ios-audio-session.js');

const IPHONE = 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 Safari/604.1';

function fixture({ios = true, session = undefined, ipad = false} = {}) {
  const media = [];
  let createCount = 0;
  const doc = {
    hidden: false,
    createElement(tag) {
      assert.equal(tag, 'audio');
      const audio = {loop: false, paused: true, src: '', attributes: {}, plays: 0,
        setAttribute(k,v) {this.attributes[k] = v;},
        play() {this.paused = false; this.plays++; return Promise.resolve();},
        pause() {this.paused = true;}};
      media.push(audio);
      return audio;
    }
  };
  const nav = {
    userAgent: ios ? (ipad ? 'Mozilla/5.0 Macintosh Safari/605.1.15' : IPHONE) : 'Mozilla/5.0 Macintosh Safari/605.1.15',
    platform: ipad ? 'MacIntel' : ios ? 'iPhone' : 'MacIntel',
    maxTouchPoints: ios ? 5 : 0
  };
  if (session !== undefined) nav.audioSession = session;
  const win = {navigator: nav, Blob, URL: {createObjectURL(blob) {createCount++; return `blob:test-${blob.size}`;}}};
  return {doc, win, media, created: () => createCount};
}

test('modern iOS Safari selects media playback without synthetic media', () => {
  const f = fixture({session: {type: 'auto'}});
  const session = createIOSAudioSession(f.win, f.doc);
  session.unlock();
  assert.equal(f.win.navigator.audioSession.type, 'playback');
  assert.equal(f.media.length, 0);
});

test('older iPhone Safari starts one looping, valid WAV with every gesture retry', () => {
  const f = fixture();
  const session = createIOSAudioSession(f.win, f.doc);
  session.unlock();
  session.unlock();
  assert.equal(f.media.length, 1);
  assert.equal(f.created(), 1);
  assert.equal(f.media[0].loop, true);
  assert.ok('playsinline' in f.media[0].attributes);
  assert.equal(f.media[0].plays, 2);
  assert.equal(f.media[0].src, 'blob:test-4044');
  f.doc.hidden = true;
  session.visibilityChanged();
  assert.equal(f.media[0].paused, true);
  const plays = f.media[0].plays;
  session.unlock();
  assert.equal(f.media[0].plays, plays);
  f.doc.hidden = false;
  session.visibilityChanged();
  assert.equal(f.media[0].plays, plays + 1);
});

test('iPad desktop Safari user agent is recognized', () => {
  const f = fixture({ipad: true, session: {type: 'auto'}});
  createIOSAudioSession(f.win, f.doc).unlock();
  assert.equal(f.win.navigator.audioSession.type, 'playback');
});

test('non-iOS Safari leaves other application audio session alone', () => {
  const f = fixture({ios: false, session: {type: 'auto'}});
  createIOSAudioSession(f.win, f.doc).unlock();
  assert.equal(f.win.navigator.audioSession.type, 'auto');
  assert.equal(f.media.length, 0);
});

test('unusable AudioSession falls back to the media element', () => {
  const f = fixture({session: {set type(value) {throw Error('denied');}}});
  createIOSAudioSession(f.win, f.doc).unlock();
  assert.equal(f.media.length, 1);
});

test('engine loads session before its audio initialization and publishes it', () => {
  const root = path.resolve(__dirname, '..');
  const shell = fs.readFileSync(path.join(root, 'site/engine-shell.html'), 'utf8');
  const build = fs.readFileSync(path.join(root, 'build-engine.py'), 'utf8');
  assert.ok(shell.includes('src="ios-audio-session.js"'));
  assert.ok(shell.indexOf('src="ios-audio-session.js"') < shell.indexOf('var openrealmAudio ='));
  assert.match(shell, /openrealmIOSAudio\.unlock\(\)/);
  assert.match(shell, /addEventListener\('touchstart', openrealmAudioUnlock/);
  assert.match(shell, /addEventListener\('visibilitychange'/);
  assert.match(shell, /openrealmAudio\.pending !== descriptor/);
  assert.match(build, /"ios-audio-session\.js"/);
});

// Execute only the audio bridge part of the embedded shell with a mock audio
// context; actual Web Audio and game content are not required for this test.
function bridgeFixture({failFirstModule = false} = {}) {
  const shell = fs.readFileSync(path.resolve(__dirname, '../site/engine-shell.html'), 'utf8');
  const start = shell.indexOf('/* Start Web Audio on a direct user gesture');
  const end = shell.indexOf('window.openrealmStartEngine = function', start);
  assert.ok(start > 0 && end > start);
  const vm = require('node:vm');
  const events = new Map();
  const warnings = [];
  let attempts = 0;
  const nodes = [];
  const audio = {
    state: 'suspended', destination: {},
    async resume() {this.state = 'running';},
    async suspend() {this.state = 'suspended';},
    audioWorklet: {async addModule(name) {
      assert.equal(name, 'audio-worklet.js');
      attempts++;
      if (failFirstModule && attempts === 1) throw Error('worklet load failed');
    }}
  };
  function WorkletNode(context, name, options) {
    assert.equal(context, audio);
    assert.equal(name, 'openrealm-audio');
    this.options = options;
    this.connected = false;
    this.connect = dest => {assert.equal(dest, audio.destination); this.connected = true;};
    this.disconnect = () => {this.connected = false;};
    nodes.push(this);
  }
  const win = {AudioContext: function () {return audio;}};
  const doc = {hidden: false, addEventListener(name, handler) {events.set(name, handler);}};
  vm.runInNewContext(shell.slice(start, end), {
    window: win, document: doc, AudioWorkletNode: WorkletNode,
    openrealmCreateIOSAudioSession: () => ({unlock() {}, visibilityChanged() {}}),
    console: {warn(...args) {warnings.push(args);}}
  });
  return {win, doc, events, audio, nodes, warnings, attempts: () => attempts};
}

const flush = () => new Promise(resolve => setImmediate(resolve));

test('worklet load failure keeps the ring and retries on next touch', async () => {
  const f = bridgeFixture({failFirstModule: true});
  f.win.openrealmAudioAttach(new SharedArrayBuffer(128), 0, 8, 64, 68, 44100);
  f.events.get('pointerdown')();
  await flush();
  assert.equal(f.nodes.length, 0);
  assert.equal(f.warnings.length, 1);
  f.events.get('touchstart')();
  await flush();
  assert.equal(f.nodes.length, 1);
  assert.equal(f.nodes[0].connected, true);
  assert.equal(f.nodes[0].options.processorOptions.rate, 44100);
  assert.equal(f.attempts(), 2);
  f.win.openrealmAudioDetach();
  assert.equal(f.nodes[0].connected, false);
});

test('detaching before worklet registration completes does not attach a stale ring', async () => {
  const f = bridgeFixture();
  let release;
  f.audio.audioWorklet.addModule = () => new Promise(resolve => {release = resolve;});
  f.events.get('pointerdown')();
  f.win.openrealmAudioAttach(new SharedArrayBuffer(128), 0, 8, 64, 68, 44100);
  f.win.openrealmAudioDetach();
  release();
  await flush();
  assert.equal(f.nodes.length, 0);
});
