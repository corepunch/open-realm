// WC3 1.27.1.7085 only. See cursor-rendering.md for ABI and evidence.
// No target writes or gameplay calls. D3D getters only run inside cursor draws.
let base = null, frame = ptr(0), sprite = ptr(0), drawDepth = 0;
let nextSample = 0, samples = 0, draws = 0;
let d3dHooked = false;
let d3dDevice = ptr(0), captured = false;
const seen = new Set();
const emit = (event, data = {}) => send({event, ms: Date.now(), ...data});
const words = (p, n) => Array.from({length: n}, (_, i) => p.add(i * 4).readU32());
const floats = (p, n) => Array.from({length: n}, (_, i) => p.add(i * 4).readFloat());

function com(object, slot, result, args) {
    return new NativeFunction(object.readPointer().add(slot * 4).readPointer(), result, ['pointer', ...args], 'stdcall');
}

function framebuffer(stage) {
    const out = Memory.alloc(4), desc = Memory.alloc(32), locked = Memory.alloc(8);
    const check = result => {if (result < 0) throw new Error('Framebuffer COM call failed: ' + result);};
    check(com(d3dDevice, 38, 'int', ['uint', 'pointer'])(d3dDevice, 0, out));
    const source = out.readPointer();
    try {
        check(com(source, 12, 'int', ['pointer'])(source, desc));
        const format = desc.readU32(), width = desc.add(24).readU32(), height = desc.add(28).readU32();
        if (format !== 21 && format !== 22) throw new Error('Expected A8R8G8B8/X8R8G8B8, got ' + format);
        check(com(d3dDevice, 36, 'int', ['uint', 'uint', 'uint', 'uint', 'pointer', 'pointer'])(d3dDevice, width, height, format, 2, out, ptr(0)));
        const copy = out.readPointer();
        try {
            check(com(d3dDevice, 32, 'int', ['pointer', 'pointer'])(d3dDevice, source, copy));
            check(com(copy, 13, 'int', ['pointer', 'pointer', 'uint'])(copy, locked, ptr(0), 0x10));
            try {
                const pitch = locked.readS32();
                if (pitch < width * 4) throw new Error('Unexpected surface pitch');
                const model = sprite.add(0x20).readPointer(), anim = model.add(0x98).readPointer();
                const index = anim.add(0x58).readU8();
                send({event: 'framebuffer', stage, width, height, pitch, format, position: floats(sprite.add(0xc0), 3),
                    animIndex: index, animFrame: anim.add(8).readPointer().add(index * 16).readU32()},
                    locked.add(4).readPointer().readByteArray(pitch * height));
            } finally {check(com(copy, 14, 'int', [])(copy));}
        } finally {com(copy, 2, 'uint', [])(copy);}
    } finally {com(source, 2, 'uint', [])(source);}
}

function install(module) {
    if (base || module.name.toLowerCase() !== 'game.dll') return;
    const pe = module.base.add(module.base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== expectedPE.timestamp ||
        pe.add(80).readU32() !== expectedPE.imageSize)
        throw new Error('Target PE does not match the supplied, hash-checked game.dll');
    base = module.base;
    emit('module', {base: base.toString(), path: module.path});
    const hook = (rva, callbacks) => Interceptor.attach(base.add(rva), callbacks);

    hook(0x11d520, {onEnter() {
        frame = this.context.ecx;
        sprite = frame.add(0x174).readU32() ? frame.add(0x178).readPointer().readPointer() : ptr(0);
    }});
    hook(0x1a47b0, {onEnter() {
        const path = this.context.edx.readCString();
        if (/cursor/i.test(path)) emit('load', {sprite: this.context.ecx.toString(), path});
    }});
    hook(0x38a9c0, {onEnter(args) {
        emit('mode-request', {mode: args[0].toUInt32(), locked: this.context.ecx.add(0x1c0).readU32(),
            caller: this.returnAddress.sub(base).toString(), restore: args[2].toUInt32()});
    }});
    for (const [rva, event] of [[0x386fa0, 'mode-push'], [0x386f50, 'mode-pop']]) {
        hook(rva, {onEnter(args) {
            const owner = this.context.ecx;
            emit(event, {mode: owner.add(0x1ac).readU32(), count: owner.add(0x1b4).readU32(),
                locked: owner.add(0x1c0).readU32(), caller: this.returnAddress.sub(base).toString(),
                args: rva === 0x386fa0 ? [args[0].toUInt32(), args[1].toUInt32(), args[2].toUInt32()] : []});
        }});
    }
    hook(0x1a42b0, {onEnter(args) {
        if (!this.context.ecx.equals(sprite)) return;
        const count = args[0].toUInt32();
        if (count > 16) throw new Error('Unexpected cursor token count ' + count);
        emit('select', {tokens: words(this.context.edx, count), flags: args[1].toUInt32()});
    }});
    hook(0x1a4c60, {onEnter(args) {
        if (this.context.ecx.equals(sprite))
            emit('replaceable-texture', {slot: args[0].toUInt32(), texture: this.context.edx.toString()});
    }});
    hook(0x1a0010, {
        onEnter() {
            this.match = this.context.ecx.equals(sprite);
            if (this.match) this.dt = this.context.esp.add(4).readFloat();
        },
        onLeave() {
            if (!this.match || Date.now() < nextSample) return;
            nextSample = Date.now() + (typeof sampleInterval === 'undefined' ? 250 : sampleInterval);
            const model = sprite.add(0x20).readPointer();
            if (model.isNull()) return;
            const anim = model.add(0x98).readPointer();
            if (anim.isNull()) return;
            const index = anim.add(0x58).readU8();
            samples++;
            emit('sample', {
                sprite: sprite.toString(), dt: this.dt,
                sequence: sprite.add(0x2c).readS16(),
                position: floats(sprite.add(0xc0), 3), scale: sprite.add(0xe8).readFloat(),
                color: sprite.add(0x148).readU32(), alpha: sprite.add(0x1b0).readU32(),
                animIndex: index, animFlags: anim.add(0x54).readU32(),
                modelTime: anim.add(0x4c).readU32(), step: anim.add(0x50).readS32(),
                spriteTime: sprite.add(0xa0).readFloat(),
                animFrame: anim.add(8).readPointer().add(index * 16).readU32(),
                queue: {count: sprite.add(0x34).readU8(), head: sprite.add(0x35).readU8(), tail: sprite.add(0x36).readU8()},
                camera: frame.add(0x140).readPointer().toString()
            });
        }
    });
    hook(0x8a4eb0, {
        onEnter(args) {
            const model = sprite.isNull() ? ptr(0) : sprite.add(0x20).readPointer();
            this.match = !model.isNull() && this.context.ecx.equals(model.add(0x98).readPointer());
            if (this.match) {
                this.anim = this.context.ecx;
                emit('sequence-switch', {index: args[0].toUInt32(), flags: args[1].toUInt32()});
            }
        },
        onLeave() {
            if (!this.match) return;
            const index = this.anim.add(0x58).readU8();
            emit('sequence-start', {index, frame: this.anim.add(8).readPointer().add(index * 16).readU32()});
        }
    });
    hook(0xf72f0, {
        onEnter() {
            this.match = this.context.ecx.equals(frame);
            if (!this.match) return;
            drawDepth++;
            this.capture = typeof captureFramebuffer !== 'undefined' && captureFramebuffer && !captured && !d3dDevice.isNull();
            if (this.capture) {captured = true; framebuffer('before');}
        },
        onLeave() {
            if (this.capture) framebuffer('after');
            if (this.match) drawDepth--;
        }
    });
    for (const rva of [0x1a2bf0, 0x1a54f0, 0x18c810, 0x18b770, 0x18b8e0,
                       0x18d760, 0x18d800, 0x18da10, 0x18e280, 0x18c5b0,
                       0x136dc0, 0x136db0, 0x1424e0]) {
        hook(rva, {onEnter() {
            if (!drawDepth || seen.has(rva)) return;
            seen.add(rva);
            emit('draw-path', {rva: '0x' + rva.toString(16)});
        }});
    }
    hook(0x1422b0, {onEnter() {
        if (d3dHooked) return;
        const device = this.context.ecx.add(0x584).readPointer();
        d3dDevice = device;
        const vtable = device.readPointer();
        const getState = new NativeFunction(vtable.add(58 * 4).readPointer(),
            'int', ['pointer', 'uint', 'pointer'], 'stdcall');
        const getShader = [93, 108].map(slot => new NativeFunction(vtable.add(slot * 4).readPointer(),
            'int', ['pointer', 'pointer'], 'stdcall'));
        const getSampler = new NativeFunction(vtable.add(68 * 4).readPointer(),
            'int', ['pointer', 'uint', 'uint', 'pointer'], 'stdcall');
        const getMaterial = new NativeFunction(vtable.add(50 * 4).readPointer(),
            'int', ['pointer', 'pointer'], 'stdcall');
        const getStage = new NativeFunction(vtable.add(66 * 4).readPointer(),
            'int', ['pointer', 'uint', 'uint', 'pointer'], 'stdcall');
        const getTexture = new NativeFunction(vtable.add(64 * 4).readPointer(),
            'int', ['pointer', 'uint', 'pointer'], 'stdcall');
        const getTransform = new NativeFunction(vtable.add(45 * 4).readPointer(),
            'int', ['pointer', 'uint', 'pointer'], 'stdcall');
        Interceptor.attach(vtable.add(82 * 4).readPointer(), {onEnter(args) {
        if (!drawDepth) return;
        draws++;
        // Actual COM method entry, after Gx has flushed its cached states.
        const out = Memory.alloc(4), states = {};
        for (const [name, id] of Object.entries({zEnable: 7, zWrite: 14, alphaTest: 15,
            srcBlend: 19, dstBlend: 20, cull: 22, zFunc: 23, alphaRef: 24,
            alphaFunc: 25, alphaBlend: 27, lighting: 137, ambient: 139,
            colorVertex: 141, diffuseSource: 145, ambientSource: 147, emissiveSource: 148})) {
            const result = getState(args[0], id, out);
            if (result < 0) throw new Error('GetRenderState failed: ' + result);
            states[name] = out.readU32();
        }
        const shaders = getShader.map(get => {
            const result = get(args[0], out);
            if (result < 0) throw new Error('GetShader failed: ' + result);
            const shader = out.readPointer();
            if (shader.isNull()) return null;
            const release = new NativeFunction(shader.readPointer().add(8).readPointer(),
                'uint', ['pointer'], 'stdcall');
            release(shader); // COM getter returns an owned reference.
            return shader.toString();
        });
        const sampler = {};
        for (const [name, id] of Object.entries({addressU: 1, addressV: 2, magFilter: 5, minFilter: 6, mipFilter: 7})) {
            const result = getSampler(args[0], 0, id, out);
            if (result < 0) throw new Error('GetSamplerState failed: ' + result);
            sampler[name] = out.readU32();
        }
        const material = Memory.alloc(68), stages = [];
        if (getMaterial(args[0], material) < 0) throw new Error('GetMaterial failed');
        for (let stage = 0; stage < 2; stage++) {
            const state = {};
            for (const [name, id] of Object.entries({colorOp: 1, colorArg1: 2, colorArg2: 3, alphaOp: 4, alphaArg1: 5, alphaArg2: 6})) {
                if (getStage(args[0], stage, id, out) < 0) throw new Error('GetTextureStageState failed');
                state[name] = out.readU32();
            }
            stages.push(state);
        }
        if (getTexture(args[0], 0, out) < 0) throw new Error('GetTexture failed');
        const texture = out.readPointer();
        let textureDesc = null;
        if (!texture.isNull()) {
            try {
                if (com(texture, 10, 'uint', [])(texture) !== 3) throw new Error('Cursor texture is not 2D');
                const desc = Memory.alloc(32);
                if (com(texture, 17, 'int', ['uint', 'pointer'])(texture, 0, desc) < 0) throw new Error('GetLevelDesc failed');
                textureDesc = {format: desc.readU32(), width: desc.add(24).readU32(), height: desc.add(28).readU32(),
                    levels: com(texture, 13, 'uint', [])(texture)};
            } finally {com(texture, 2, 'uint', [])(texture);}
        }
        const transforms = {}, matrix = Memory.alloc(64);
        for (const [name, id] of Object.entries({view: 2, projection: 3, world: 256})) {
            if (getTransform(args[0], id, matrix) < 0) throw new Error('GetTransform failed');
            transforms[name] = floats(matrix, 16);
        }
        const draw = {states, shaders, sampler, texture: texture.toString(), textureDesc, material: floats(material, 17), stages, transforms,
            primitiveType: args[1].toUInt32(), vertices: args[4].toUInt32(), primitives: args[6].toUInt32()};
        const key = JSON.stringify(draw);
        if (!seen.has(key)) {seen.add(key); emit('d3d-draw', draw);}
        }});
        d3dHooked = true;
    }});
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {status() {return {moduleFound: base !== null, samples, draws};}};
