/*
 * Read-only OpenGL observer for the Prologue01 RLhr rain path.
 * The companion Python controller checks the retail executable hash and bounds
 * the attach interval. No game state or OpenGL state is changed.
 */
'use strict';

const gl = Process.getModuleByName('opengl32.dll');
const getInteger = new NativeFunction(gl.getExportByName('glGetIntegerv'), 'void', ['uint', 'pointer'], 'stdcall');
const getFloat = new NativeFunction(gl.getExportByName('glGetFloatv'), 'void', ['uint', 'pointer'], 'stdcall');
const getTexEnv = new NativeFunction(gl.getExportByName('glGetTexEnviv'), 'void', ['uint', 'uint', 'pointer'], 'stdcall');
const getTexLevel = new NativeFunction(gl.getExportByName('glGetTexLevelParameteriv'), 'void', ['uint', 'int', 'uint', 'pointer'], 'stdcall');
const isEnabled = new NativeFunction(gl.getExportByName('glIsEnabled'), 'uchar', ['uint'], 'stdcall');

const GL_TEXTURE_2D = 0x0de1;
const GL_TEXTURE_BINDING_2D = 0x8069;
const GL_TEXTURE_WIDTH = 0x1000;
const GL_TEXTURE_HEIGHT = 0x1001;
const GL_TEXTURE_ALPHA_SIZE = 0x805f;
const GL_TEXTURE_ENV = 0x2300;
const GL_TEXTURE_ENV_MODE = 0x2200;
const GL_MODULATE = 0x2100;
const GL_BLEND = 0x0be2;
const GL_SRC_ALPHA = 0x0302;
const GL_ONE_MINUS_SRC_ALPHA = 0x0303;
const GL_ALPHA_TEST = 0x0bc0;
const GL_ALPHA_TEST_FUNC = 0x0bc1;
const GL_ALPHA_TEST_REF = 0x0bc2;
const GL_TRIANGLES = 0x0004;
const GL_UNSIGNED_BYTE = 0x1401;

const threads = new Map();
const textureInfo = new Map();
const lastSent = new Map();
let eventCount = 0;

function emit(row) {
    send(row);
}

function threadState() {
    const id = Process.getCurrentThreadId();
    if (!threads.has(id)) {
        threads.set(id, { color: null });
    }
    return threads.get(id);
}

function queryInt(pname) {
    const out = Memory.alloc(4);
    getInteger(pname, out);
    return out.readS32();
}

function queryFloat(pname) {
    const out = Memory.alloc(4);
    getFloat(pname, out);
    return out.readFloat();
}

function queryTexEnv(pname) {
    const out = Memory.alloc(4);
    getTexEnv(GL_TEXTURE_ENV, pname, out);
    return out.readS32();
}

function boundTextureInfo(id) {
    if (id === 0) return null;
    const known = textureInfo.get(id);
    if (known && known.width > 0 && known.height > 0) return known;

    const out = Memory.alloc(4);
    const info = { width: 0, height: 0, alphaBits: 0 };
    getTexLevel(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, out);
    info.width = out.readS32();
    getTexLevel(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, out);
    info.height = out.readS32();
    getTexLevel(GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, out);
    info.alphaBits = out.readS32();
    if (info.width > 0 && info.height > 0) textureInfo.set(id, info);
    return info;
}

function colorSample(state) {
    try {
        const color = state.color;
        if (!color || color.buffer !== 0 || color.type !== GL_UNSIGNED_BYTE || color.size !== 4) return null;
        const stride = color.stride || 4;
        const rows = [];
        for (let vertex = 0; vertex < 4; ++vertex) {
            const p = color.pointer.add(vertex * stride);
            rows.push([p.readU8(), p.add(1).readU8(), p.add(2).readU8(), p.add(3).readU8()]);
        }
        return rows;
    } catch (error) {
        return { error: String(error) };
    }
}

function attach(name, callbacks) {
    Interceptor.attach(gl.getExportByName(name), callbacks);
}

attach('glColorPointer', {
    onEnter(args) {
        threadState().color = {
            size: args[0].toInt32(),
            type: args[1].toUInt32(),
            stride: args[2].toInt32(),
            pointer: args[3],
            buffer: 0
        };
    }
});

attach('glDrawElements', {
    onEnter(args) {
        if (eventCount >= 30 || args[0].toUInt32() !== GL_TRIANGLES) return;
        const texture = queryInt(GL_TEXTURE_BINDING_2D) >>> 0;
        const info = boundTextureInfo(texture);
        if (!info || info.width !== 32 || info.height !== 16) return;

        const state = threadState();
        const sample = colorSample(state);
        if (!sample || !sample.some(row => row[3] === 150)) return;

        const now = Date.now();
        if (now - (lastSent.get(texture) || 0) < 500) return;
        lastSent.set(texture, now);
        eventCount++;

        emit({
            event: 'rain-draw',
            timeMs: now,
            texture,
            textureInfo: info,
            indexCount: args[1].toInt32(),
            blendEnabled: !!isEnabled(GL_BLEND),
            blendSrc: queryInt(0x0be1),
            blendDst: queryInt(0x0be0),
            textureEnvMode: queryTexEnv(GL_TEXTURE_ENV_MODE),
            alphaTestEnabled: !!isEnabled(GL_ALPHA_TEST),
            alphaTestFunc: queryInt(GL_ALPHA_TEST_FUNC),
            alphaTestRef: queryFloat(GL_ALPHA_TEST_REF),
            colorArray: {
                size: state.color.size,
                type: state.color.type,
                stride: state.color.stride,
                sample
            }
        });
    }
});

emit({ event: 'weather-alpha-probe-ready', arch: Process.arch, module: gl.path, pid: Process.id });
rpc.exports = { eventcount: () => eventCount };
