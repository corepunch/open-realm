/* Retail 1.27.1.7085 only; the controller verifies the on-disk SHA256. */
'use strict';
let game = null, sequence = 0, installed = false, probe = false, aliasProbe = 0;
const labelCalls = new Map(), callbackCalls = new Map(), activeUsers = new Set();
let probePreemption = false, preemptionProbed = false;
function read(fn) { try { return fn(); } catch (e) { return {error: String(e)}; } }
function emit(event, fields = {}) { send({event, seq: ++sequence, ticks: Date.now(), thread: Process.getCurrentThreadId(), ...fields}); }
function va(address) { return game.base.add(address - 0x6f000000); }
function row(p) { return read(() => ({address: String(p), count: p.add(0x20).readU32(), channel: p.add(0x3c).readU32(), priority: p.add(0x38).readU32(), flags: p.add(0x40).readU32(), variant: p.add(0x70).readS32()})); }
function sound(p) { return read(() => ({address: String(p), file: p.add(0x34).readCString(), flags: p.add(0x13c).readU32(), priority: p.add(0x140).readU32(), user: p.add(0x148).readU32(), channel: p.add(0x178).readU32(), started: p.add(0x17c).readU32(), state: p.add(0x1ac).readU32()})); }
function hook(address, callbacks) { Interceptor.attach(va(address), callbacks); }
function install(m) {
    if (installed || m.name.toLowerCase() !== 'game.dll') return;
    if (m.path.toLowerCase() !== expectedModulePath.toLowerCase()) throw Error('Loaded game.dll differs from hash-checked path: ' + m.path);
    game = m;
    if (Process.arch !== 'ia32') throw Error('Expected Windows ia32 Frida agent');
    if (va(0x6f0af5e0).readU8() !== 0x55 || va(0x6f0af5e1).readU16() !== 0xec8b) throw Error('Admission prologue mismatch');
    hook(0x6f3593e0, {onEnter() { emit('unit-response', {unit: String(this.context.ecx), kind: this.context.edx.toUInt32()}); }, onLeave(ret) { emit('unit-response-result', {result: ret.toInt32()}); }});
    hook(0x6f34b150, {onEnter() {
        const original = read(() => this.context.ecx.readCString());
        if (probe && aliasProbe < 2 && typeof original === 'string' && original.endsWith('What')) {
            const label = ['SludgeMonsterWhat', 'SludgeMonsterReady'][aliasProbe++];
            this.labelMemory = Memory.allocUtf8String(label);
            this.context.ecx = this.labelMemory;
            this.context.esp.add(4).writeU32(1); // explicit variant mode
            this.context.esp.add(12).writeU32(0); // same file for both labels
            emit('probe-label-replacement', {original, label});
        }
        this.call = {id: sequence + 1, label: read(() => this.context.ecx.readCString()), row: null};
        const id = this.threadId, stack = labelCalls.get(id) || []; stack.push(this.call); labelCalls.set(id, stack);
        emit('label-request', {id: this.call.id, label: this.call.label});
    }, onLeave(ret) {
        emit('label-result', {id: this.call.id, result: ret.toInt32(), row: this.call.row ? row(this.call.row) : null});
        labelCalls.get(this.threadId).pop();
    }});
    hook(0x6f34fee0, {onEnter() {
        this.row = this.context.ecx;
        const stack = labelCalls.get(this.threadId) || []; if (stack.length) stack[stack.length - 1].row = this.row;
        emit('variant-before', {row: row(this.row)});
    }, onLeave() { emit('variant-after', {row: row(this.row)}); }});
    hook(0x6f0af5e0, {onEnter() {
        this.sound = this.context.ecx;
        const request = sound(this.sound), calls = labelCalls.get(this.threadId) || [];
        if (probePreemption && !preemptionProbed && request.state === 0 &&
            activeUsers.has(request.user) && calls.length && /(?:What|Pissed)$/.test(calls[calls.length - 1].label)) {
            preemptionProbed = true;
            this.sound.add(0x13c).writeU32(request.flags | 2048);
            emit('probe-preemption-policy', {sound: request, flags: request.flags | 2048});
        }
        emit('admit', {sound: sound(this.sound)});
    }, onLeave(ret) { emit('admit-result', {result: ret.toInt32(), sound: sound(this.sound)}); }});
    hook(0x6f0abcf0, {onEnter() {
        const stack = callbackCalls.get(this.threadId) || [];
        const callback = {sound: sound(this.context.ecx), slot: this.context.esp.add(4).readU32()};
        stack.push(callback); callbackCalls.set(this.threadId, stack);
        emit('playback-callback', callback);
    }, onLeave() { callbackCalls.get(this.threadId).pop(); }});
    for (const [address, event] of [[0x6f35b400, 'portrait-start'], [0x6f35b500, 'portrait-end']]) {
        hook(address, {onEnter() {
            this.unit = this.context.ecx.add(8).readPointer();
            if (event === 'portrait-start') activeUsers.add(this.unit.toUInt32());
            else activeUsers.delete(this.unit.toUInt32());
            const callbacks = callbackCalls.get(this.threadId) || [];
            emit(event, {callback: callbacks.length ? callbacks[callbacks.length - 1] : null, notification: read(() => ({address: String(this.context.ecx), unit: String(this.unit), animation: this.context.ecx.add(12).readS32()}))});
        }});
    }
    hook(0x6f353220, {onEnter() { emit('cooldown-set', {unit: String(this.context.ecx)}); }});
    hook(0x6f35b0b0, {onEnter() { this.unit = String(this.context.ecx); }, onLeave(ret) { emit('cooldown-check', {unit: this.unit, allowed: ret.toInt32()}); }});
    for (const [address, event] of [[0x6f690ea0, 'what'], [0x6f690dd0, 'pissed'], [0x6f690d70, 'yes'], [0x6f690cf0, 'yes-attack']]) {
        hook(address, {onEnter() { this.unit = String(this.context.ecx); emit(event, {unit: this.unit, count: va(0x6fd707f0).readU32()}); }, onLeave() { emit(event + '-done', {unit: this.unit, count: va(0x6fd707f0).readU32()});
         }});
    }
    installed = true;
    emit('installed', {base: String(m.base), path: m.path, size: m.size, arch: Process.arch, pid: Process.id});
}
Process.attachModuleObserver({onAdded: install});
function win(name, ret, args) { return new NativeFunction(Process.getModuleByName('user32.dll').getExportByName(name), ret, args, 'stdcall'); }
function windows() {
    const getPid = win('GetWindowThreadProcessId', 'uint', ['pointer','pointer']);
    const getRect = win('GetClientRect', 'int', ['pointer','pointer']);
    const visible = win('IsWindowVisible','int',['pointer']);
    const result = [], storage = Memory.alloc(4), rect = Memory.alloc(16);
    const cb = new NativeCallback((h, p) => {
        getPid(h, storage);
        if (storage.readU32() === Process.id && visible(h)) {
            getRect(h, rect); result.push({handle: String(h), width: rect.add(8).readS32(), height: rect.add(12).readS32()});
        }
        return 1;
    }, 'int', ['pointer','pointer'], 'stdcall');
    win('EnumWindows','int',['pointer','pointer'])(cb, NULL); return result;
}
rpc.exports = {
    enablepreemption() { probePreemption = true; emit('preemption-probe-enabled'); },
    enableprobe() { probe = true; emit("probe-enabled"); },
    status() { return {installed, windows: windows()}; },
    key(handle, key) {
        const post = win('PostMessageW','int',['pointer','uint','uint','uint']);
        const scan = win('MapVirtualKeyW','uint',['uint','uint'])(key, 0);
        post(ptr(handle), 0x1c, 1, 0); // WM_ACTIVATEAPP for an unattended Wine window
        post(ptr(handle), 0x6, 1, 0); // WM_ACTIVATE
        const delivered = post(ptr(handle), 0x100, key, 1 | (scan << 16));
        post(ptr(handle), 0x101, key, 0xc0000001 | (scan << 16));
        emit('input-key', {key, scan, delivered});
    },
    click(handle, x, y) {
        const post = win('PostMessageW','int',['pointer','uint','uint','uint']);
        const pos = (y << 16) | (x & 65535);
        post(ptr(handle),0x200,0,pos); post(ptr(handle),0x201,1,pos); post(ptr(handle),0x202,0,pos);
        emit('input-click', {x,y});
    }
};
