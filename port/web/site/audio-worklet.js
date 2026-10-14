/* Browser playback consumes the S16 44.1 kHz mixer output from shared WASM memory. */
class OpenRealmAudioProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = options.processorOptions;
    this.samples = new Int16Array(o.memory, o.ring, o.frames * 2);
    this.indices = new Int32Array(o.memory);
    this.write = o.write >>> 2;
    this.read = o.read >>> 2;
    this.mask = o.frames - 1;
    this.step = o.rate / sampleRate;
    this.fraction = 0;
  }
  process(inputs, outputs) {
    const channels = outputs[0];
    if (!channels || !channels.length) return true;
    const left = channels[0], right = channels[1] || left;
    const write = Atomics.load(this.indices, this.write);
    let read = Atomics.load(this.indices, this.read);
    let fraction = this.fraction;
    const samples = this.samples;
    const available = (write - read) >>> 0;
    const needed = Math.floor(fraction + (left.length - 1) * this.step) + 2;
    if (available < needed) {
      left.fill(0);
      if (right !== left) right.fill(0);
      return true;
    }
    for (let i = 0; i < left.length; ++i) {
      const whole = Math.floor(fraction);
      const t = fraction - whole;
      const a = ((read + whole) & this.mask) * 2;
      const b = ((read + whole + 1) & this.mask) * 2;
      left[i] = (samples[a] + (samples[b] - samples[a]) * t) / 32768;
      right[i] = (samples[a + 1] + (samples[b + 1] - samples[a + 1]) * t) / 32768;
      fraction += this.step;
    }
    const consumed = Math.floor(fraction);
    read += consumed;
    this.fraction = fraction - consumed;
    Atomics.store(this.indices, this.read, read);
    return true;
  }
}
registerProcessor('openrealm-audio', OpenRealmAudioProcessor);
