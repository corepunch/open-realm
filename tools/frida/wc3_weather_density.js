/*
 * Read-only observer for retail Warcraft III's weather-particle initializer.
 * The companion Python controller hash-checks the executable and bounds the
 * attach interval. This probe reads object fields and does not call game code.
 */
'use strict';

const module = Process.getModuleByName('Warcraft III.exe');
const weatherSample = module.base.add(0x57f1c0); // VA 0x97f1c0 - PE image base 0x400000
const MAX_EMITTERS = 256; // distinct emitter objects tracked; Prologue01 shows 23, so this only bounds a runaway trace
const MAX_ERRORS = 16; // probe-error rows reported before the hook goes quiet
const calls = new Map();
let targetRate = -1;
let eventCount = 0;
let errorCount = 0;
let emitterOverflowReported = false;

send({ event: 'weather-density-probe-ready', base: module.base.toString(),
       target: weatherSample.toString(), arch: Process.arch, pid: Process.id });

Interceptor.attach(weatherSample, {
    onEnter() {
        try {
            const emitter = this.context.ecx;
            const rate = emitter.add(0xa8).readFloat();
            if (targetRate >= 0 && Math.abs(rate - targetRate) > 0.001) return;

            const key = emitter.toString();
            if (!calls.has(key) && calls.size >= MAX_EMITTERS) {
                if (!emitterOverflowReported) {
                    emitterOverflowReported = true;
                    send({ event: 'probe-error', message: 'emitter table full at ' + MAX_EMITTERS + '; new emitters ignored' });
                }
                return;
            }
            const n = (calls.get(key) || 0) + 1;
            calls.set(key, n);
            if (n <= 8 || n % 100 === 0) {
                const sp = this.context.esp;
                eventCount++;
                send({
                    event: 'weather-sample',
                    object: key,
                    sampleNumber: n,
                    flags: '0x' + emitter.add(0x194).readU32().toString(16),
                    emissionRate: rate,
                    frameDeltaSeconds: sp.add(8).readFloat(),
                    liveParticles: emitter.add(0x88).readS32(),
                    emissionAccumulator: emitter.add(8).readFloat()
                });
            }
        } catch (error) {
            if (errorCount++ < MAX_ERRORS) send({ event: 'probe-error', message: String(error) });
        }
    }
});

rpc.exports = {
    setrate(value) { targetRate = value; },
    eventcount() { return eventCount; }
};
