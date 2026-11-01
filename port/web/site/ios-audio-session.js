/* Give game Web Audio a media playback session on iOS, including silent mode.
 * Safari with AudioSession uses playback; older versions use a silent media
 * element while the AudioWorklet continues to supply the actual game audio. */
(function (root) {
  'use strict';

  function createIOSAudioSession(win, doc) {
    const nav = win.navigator || {};
    const ios = /iPhone|iPad|iPod/i.test(nav.userAgent || '') ||
      (nav.platform === 'MacIntel' && nav.maxTouchPoints > 1);
    let silentAudio = null;

    function unlock() {
      if (!ios || doc.hidden) return;
      try {
        if (nav.audioSession) {
          nav.audioSession.type = 'playback';
          if (nav.audioSession.type === 'playback') return;
        }
      } catch (_) { /* Use the older iOS media-element workaround. */ }

      if (!silentAudio) {
        // 0.5 s of 8-bit PCM silence at 8 kHz (silence is 128, not 0).
        const samples = 4000;
        const bytes = new Uint8Array(44 + samples);
        const view = new DataView(bytes.buffer);
        function tag(offset, value) {
          for (let i = 0; i < value.length; i++) bytes[offset + i] = value.charCodeAt(i);
        }
        tag(0, 'RIFF'); view.setUint32(4, 36 + samples, true);
        tag(8, 'WAVE'); tag(12, 'fmt ');
        view.setUint32(16, 16, true);
        view.setUint16(20, 1, true); view.setUint16(22, 1, true);
        view.setUint32(24, 8000, true); view.setUint32(28, 8000, true);
        view.setUint16(32, 1, true); view.setUint16(34, 8, true);
        tag(36, 'data'); view.setUint32(40, samples, true);
        bytes.fill(128, 44);
        silentAudio = doc.createElement('audio');
        silentAudio.loop = true;
        silentAudio.setAttribute('playsinline', '');
        silentAudio.src = win.URL.createObjectURL(new win.Blob([bytes], {type: 'audio/wav'}));
      }
      // Called directly from touch/keyboard, so iOS allows media playback.
      const play = silentAudio.play();
      if (play && typeof play.catch === 'function') play.catch(function () {});
    }

    function visibilityChanged() {
      if (!ios) return;
      if (doc.hidden) {
        if (silentAudio) silentAudio.pause();
      } else {
        // Safari can reject playback without a gesture; a later tap retries.
        unlock();
      }
    }

    return {unlock, visibilityChanged};
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = {createIOSAudioSession};
  } else {
    root.openrealmCreateIOSAudioSession = createIOSAudioSession;
  }
})(globalThis);
