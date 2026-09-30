// WC3 1.27.1.7085. Entry/exit observers; no gameplay calls or target data writes.
let installed = false, samples = 0, serial = 0, rebuildSamples = 0;
let widgetScenario = false;
let numericCase = null;
const counts = {}, active = new Map();
const headingActive = new Map();
const emit = (event, data = {}) => send({event, ms: Date.now(), ...data});
const bump = kind => {counts[kind] = (counts[kind] || 0) + 1;};
const ints = (p, n) => Array.from({length: n}, (_, i) => p.add(i * 4).readS32());

// Preserve arbitrary byte strings; UTF-8 readCString would reject some public inputs.
function cbytes(pointer) {
    const result = [];
    for (let i = 0; i < 256; i++) {
        const byte = pointer.add(i).readU8();
        if (byte === 0) return result.map(v => v.toString(16).padStart(2,'0')).join('');
        result.push(byte);
    }
    throw new Error('Byte parser input exceeds bounded observer length');
}

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp ||
        pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString(), path: module.path});
    const hook = (rva, callbacks) => Interceptor.attach(base.add(rva), callbacks);
    if (config.numericEvents) {
        for (const [name, rva] of [['S2R',0x211080], ['I2R',0x204c80], ['R2I',0x2103a0],
                                  ['Sin',0x215d00], ['Cos',0x1f9580], ['Acos',0x1f75d0],
                                  ['SquareRoot',0x215d30], ['Asin',0x1f8250], ['Atan',0x1f8310],
                                  ['Tan',0x216750], ['Atan2',0x1f8290], ['Deg2Rad',0x1fcda0],
                                  ['Rad2Deg',0x210480], ['Pow',0x20f990]]) {
            hook(rva, {
                onEnter(args) {
                    this.numeric = numericCase && numericCase.native === name ? {...numericCase} : null;
                    if (!this.numeric) return;
                    if (name !== 'S2R')
                        this.numeric.input = name === 'I2R' ? args[0].toUInt32() : (name === 'Atan2' || name === 'Pow') ?
                            [args[0].readU32(),args[1].readU32()] : args[0].readU32();
                },
                onLeave(result) {
                    if (this.numeric) {
                        bump('numeric-native');
                        emit('numeric-native', {...this.numeric, output:result.toUInt32()});
                    }
                }
            });
        }
        hook(0x070de0, {
            onEnter() {
                this.numeric = numericCase && numericCase.native === 'S2R' ? {...numericCase} : null;
                if (!this.numeric) return;
                this.output = this.context.ecx;
                if (config.byteEvents) this.numeric.text_hex = cbytes(this.context.edx);
                else this.numeric.text = this.context.edx.readCString();
            },
            onLeave() {
                if (this.numeric) {
                    bump('numeric-parser');
                    emit(config.byteEvents ? 'numeric-byte-parser' : 'numeric-parser', {...this.numeric, output:this.output.readU32()});
                }
            }
        });
    }
    if (config.byteEvents) {
        const crt = Process.enumerateModules().find(m => m.name.toLowerCase() === 'msvcr120.dll');
        if (!crt || crt.path.toLowerCase() !== config.crt.path.toLowerCase())
            throw new Error('Byte capture did not load the hash-checked sibling CRT');
        const crtpe = crt.base.add(crt.base.add(0x3c).readU32());
        if (crtpe.add(8).readU32() !== config.crt.timestamp || crtpe.add(80).readU32() !== config.crt.imageSize)
            throw new Error('Loaded CRT header differs from the pinned sibling: timestamp=' + crtpe.add(8).readU32() + ' size=' + crtpe.add(80).readU32());
        const defaultLocale = crt.base.add(0xdfa84).readPointer();
        emit('crt-module', {sha256:config.crt.sha256, path:crt.path,
            locale_ever_changed:crt.base.add(0xdf7c4).readU32(),
            ctype_rva:crt.base.add(0xdf858).readPointer().sub(crt.base).toUInt32(),
            default_mb_cur_max:defaultLocale.add(0x74).readU32(),
            default_ctype_rva:defaultLocale.add(0x90).readPointer().sub(crt.base).toUInt32()});
        Interceptor.attach(crt.base.add(0xf1d5), {
            onEnter(args) {
                this.digit = numericCase && numericCase.native === 'S2R' ? {...numericCase} : null;
                if (!this.digit) return;
                const input = args[0].toInt32();
                if (input < -128 || input > 255) throw new Error('Public byte classification outside char domain');
                const table = crt.base.add(0xdf858).readPointer();
                this.digit.input = input;
                this.digit.locale_ever_changed = crt.base.add(0xdf7c4).readU32();
                this.digit.ctype_rva = table.sub(crt.base).toUInt32();
                this.digit.table_word = table.add(input * 2).readU16();
            },
            onLeave(result) {
                if (!this.digit) return;
                bump('numeric-digit');
                if (counts['numeric-digit'] <= config.samples)
                    emit('numeric-digit', {...this.digit, output:result.toUInt32()});
            }
        });
    }
    if (config.literalTexts && config.literalTexts.length) {
        const literals = new Set(config.literalTexts);
        hook(0x925260, {
            onEnter() {
                this.lexer = this.context.ecx;
                const text = this.lexer.add(0x98).readPointer().readCString();
                this.literal = literals.has(text) ? {text, caller:this.returnAddress.sub(base).toUInt32()} : null;
            },
            onLeave(result) {
                if (!this.literal) return;
                bump('numeric-literal');
                if (counts['numeric-literal'] <= config.samples)
                    emit('numeric-literal', {...this.literal, token:result.toUInt32(), output:this.lexer.add(0x24).readU32()});
            }
        });
    }
    if (config.integerTexts && config.integerTexts.length) {
        const literals = new Set(config.integerTexts);
        for (const [rva, radix, prefix] of [[0x925210,10,0], [0x925490,8,1], [0x925350,16,null]]) {
            hook(rva, {
                onEnter() {
                    this.lexer = this.context.ecx;
                    const text = this.lexer.add(0x98).readPointer().readCString();
                    this.literal = literals.has(text) ? {text, radix,
                        prefix:prefix === null ? this.context.esp.add(4).readU32() : prefix,
                        caller:this.returnAddress.sub(base).toUInt32()} : null;
                },
                onLeave(result) {
                    if (!this.literal) return;
                    bump('numeric-integer-literal');
                    if (counts['numeric-integer-literal'] <= config.samples)
                        emit('numeric-integer-literal', {...this.literal, token:result.toUInt32(), output:this.lexer.add(0x24).readU32()});
                }
            });
        }
    }
    if (config.profileEvents) {
        for (const [rva, kind] of [[0x690c20, 'query-mask'], [0x690c80, 'category']]) {
            hook(rva, {
                onEnter() { this.rawcode = this.context.ecx.toUInt32(); },
                onLeave(result) {
                    bump('profile-' + kind);
                    if (counts['profile-' + kind] <= config.samples)
                        emit('movement-profile', {kind, rawcode:this.rawcode, value:result.toUInt32()});
                }
            });
        }
        hook(0x05c7e0, {
            onEnter(args) {
                this.bridge = this.context.ecx;
                this.row = {rawcode:this.bridge.sub(0x164).add(0x30).readU32(),
                    category:args[0].toUInt32(), queryMask:args[1].toUInt32(),
                    identity:ints(this.bridge.add(8),2)};
            },
            onLeave() {
                bump('movement-mask-publication');
                if (counts['movement-mask-publication'] > config.samples) return;
                const id = this.row.identity[0] >>> 0;
                if (id === 0xffffffff) { emit('movement-mask-publication', {...this.row, mover:null}); return; }
                const registry = base.add(0xd68610).readPointer(), alternate = (id & 0x80000000) !== 0;
                const index = id & 0x7fffffff, limit = registry.add(alternate ? 0x3c : 0x1c).readU32();
                if (index >= limit) throw new Error('movement bridge identity outside registry');
                const slot = registry.add(alternate ? 0x2c : 0xc).readPointer().add(index*8);
                if (slot.readS32() !== -2) throw new Error('movement bridge identity is not live');
                const mover = slot.add(4).readPointer();
                if (mover.add(0x18).readS32() !== this.row.identity[1]) throw new Error('movement bridge epoch differs');
                const region = mover.add(0x98).readPointer(), path = mover.add(0xa8).readPointer();
                emit('movement-mask-publication', {...this.row, mover:mover.toString(),
                    objectCategory:region.add(0x34).readU32(),
                    pathMask:path.isNull() ? null : path.add(0x9c).readU32()});
            }
        });
    }
    if (config.headingEvents) {
        // 16f630: ECX=output*, EDX=current heading*, one vector* stack argument.
        hook(0x16f630, {
            onEnter(args) {
                this.output = this.context.ecx;
                this.previous = headingActive.get(this.threadId);
                this.sequence = ++serial;
                this.observe = (counts['heading-error'] || 0) < config.samples;
                headingActive.set(this.threadId, {sequence:this.sequence, observe:this.observe});
                this.row = {vector:[args[0].readU32(),args[0].add(4).readU32()],
                    heading:this.context.edx.readU32(), sequence:this.sequence,
                    outputPointer:this.output.toString(), vectorPointer:args[0].toString(),
                    headingPointer:this.context.edx.toString()};
            },
            onLeave() {
                bump('heading-error');
                if (this.observe)
                    emit('heading-error', {...this.row,error:this.output.readU32()});
                if (this.previous) headingActive.set(this.threadId, this.previous);
                else headingActive.delete(this.threadId);
            }
        });
        // Observe only the nested movement producer; public and unrelated raw Acos calls are separate evidence.
        hook(0x1d4c80, {
            onEnter(args) {
                this.heading = headingActive.get(this.threadId);
                if (!this.heading || !this.heading.observe) return;
                this.output = args[0];
                this.row = {sequence:this.heading.sequence, caller:this.returnAddress.sub(base).toUInt32(),
                    vectorPointer:this.context.ecx.toString(), outputPointer:this.output.toString(),
                    lengthPointer:args[1].toString(), length:args[1].readU32(),
                    vector:[this.context.ecx.readU32(),this.context.ecx.add(4).readU32()]};
            },
            onLeave() {
                if (!this.row) return;
                bump('heading-vector-alias');
                emit('heading-vector-alias', {...this.row, output:this.output.readU32()});
            }
        });
        hook(0x06ffa0, {
            onEnter() {
                this.heading = headingActive.get(this.threadId);
                if (!this.heading || !this.heading.observe) return;
                this.output = this.context.ecx;
                this.row = {sequence:this.heading.sequence, caller:this.returnAddress.sub(base).toUInt32(),
                    outputPointer:this.output.toString(), inputPointer:this.context.edx.toString(),
                    stackPointer:this.context.esp.toString(), input:this.context.edx.readU32()};
            },
            onLeave() {
                if (!this.row) return;
                bump('heading-acos-alias');
                emit('heading-acos-alias', {...this.row, output:this.output.readU32()});
            }
        });
    }
    if (config.velocityEvents) {
        // 16fe20 is thiscall(speed*,heading*); original integration precedes the velocity change.
        hook(0x16fe20, {
            onEnter(args) {
                this.mover = this.context.ecx;
                const mover = this.mover, owner = base.add(0xd53a48).readPointer();
                const clock = owner.add(mover.add(0x14).readU32() & 0x80000000 ? 0x68 : 0x14);
                const words = (p, n) => Array.from({length:n}, (_,i) => p.add(i*4).readU32());
                this.row = {mover:mover.toString(), speed:args[0].readU32(), heading:args[1].readU32(),
                    before:words(mover.add(0x70),8), clock:words(clock.add(0x40),3),
                    fineObject:mover.add(0x98).readPointer().toString(),
                    fineFlagsBefore:mover.add(0x98).readPointer().isNull() ? null : mover.add(0x98).readPointer().add(0x40).readU32()};
            },
            onLeave() {
                bump('velocity-commit');
                if (counts['velocity-commit'] <= config.samples) {
                    const words = (p, n) => Array.from({length:n}, (_,i) => p.add(i*4).readU32());
                    emit('velocity-commit', {...this.row, after:words(this.mover.add(0x70),8),
                        requested:words(this.mover.add(0xc0),2),
                        fineFlagsAfter:this.mover.add(0x98).readPointer().isNull() ? null : this.mover.add(0x98).readPointer().add(0x40).readU32()});
                }
            }
        });
    }
    if (config.motionEvents) {
        // Verified1710a0: ECX mover, one range* stack argument, RET4.
        hook(0x1710a0, {
            onEnter(args) {
                this.mover = this.context.ecx;
                this.row = {mover:this.mover.toString(), value:args[0].readU32(),
                    before:this.mover.add(0xb0).readU32(), callerRva:this.returnAddress.sub(base).toUInt32()};
            },
            onLeave() {
                bump('arrival-range');
                if (counts['arrival-range'] <= config.samples)
                    emit('arrival-range', {...this.row, after:this.mover.add(0xb0).readU32()});
            }
        });
        // Both bridge point producers normalize world range and clamp to0.49.
        for (const [rva, rangeArg, kind] of [[0x05c410, 0, 'set'], [0x05b970, 7, 'point']]) {
            hook(rva, {
                onEnter(args) {
                    bump('arrival-input');
                    if (counts['arrival-input'] <= config.samples)
                        emit('arrival-input', {kind, bridge:this.context.ecx.toString(),
                            rawcode:this.context.ecx.sub(0x164).add(0x30).readU32(),
                            identity:ints(this.context.ecx.add(8),2), worldRange:args[rangeArg].readU32(),
                            callerRva:this.returnAddress.sub(base).toUInt32()});
                }
            });
        }
        // 170880 is thiscall: speed*, heading*, error*, stop are four stack arguments.
        hook(0x170880, {
            onEnter(args) {
                this.speed = args[0]; this.heading = args[1];
                const mover = this.context.ecx;
                this.row = {mover: mover.toString(), speed: this.speed.readU32(),
                    heading: this.heading.readU32(), error: args[2].readU32(), stop: args[3].toUInt32(),
                    increment: mover.add(0xb4).readU32(), turn: mover.add(0xb8).readU32(),
                    window: mover.add(0xbc).readU32()};
            },
            onLeave() {
                bump('motion-decision');
                if (counts['motion-decision'] <= config.samples)
                    emit('motion-decision', {...this.row, nextSpeed: this.speed.readU32(), nextHeading: this.heading.readU32()});
            }
        });
    }
    if (config.widgetEvents) {
        const placements = new Map();
        let placementSerial = 0;
        // The registered build native admits placement statuses 0/45, then
        // requires order validation0. Observe which original gate rejects it.
        hook(0x66f050, {
            onEnter(args) {
                this.recordPlacement = widgetScenario;
                if (!this.recordPlacement) return;
                this.placementId = ++placementSerial;
                placements.set(this.threadId, this.placementId);
                this.row = {placementId:this.placementId, unitId:this.context.ecx.toUInt32(),
                    x:this.context.edx.readU32(), y:args[0].readU32(), builder:args[3].toString()};
            },
            onLeave(result) {
                if (!this.recordPlacement) return;
                bump('widget-placement');
                if (counts['widget-placement'] <= config.samples)
                    emit('widget-placement', {...this.row, result:result.toUInt32()});
                placements.delete(this.threadId);
            }
        });
        // These four instructions assign status 0x44. Keep their distinct
        // sites observable until the caller's rejection policy is recovered.
        for (const rva of [0x66f452, 0x66f475, 0x66f4c1, 0x66fb1d]) {
            hook(rva, {
                onEnter() {
                    const placementId = placements.get(this.threadId);
                    if (!placementId) return;
                    bump('widget-placement-branch');
                    if (counts['widget-placement-branch'] <= config.samples)
                        emit('widget-placement-branch', {placementId, site:rva,
                            eax:this.context.eax.toUInt32(), ecx:this.context.ecx.toUInt32(),
                            esi:this.context.esi.toUInt32(), edi:this.context.edi.toUInt32(),
                            ebx:this.context.ebx.toUInt32()});
                }
            });
        }
        hook(0x68f700, {
            onEnter(args) {
                this.placementId = placements.get(this.threadId);
                if (!this.placementId) return;
                this.placementContext = args[1];
                this.before = ints(this.placementContext, 23);
            },
            onLeave(result) {
                if (!this.placementId) return;
                bump('widget-footprint-check');
                if (counts['widget-footprint-check'] <= config.samples)
                    emit('widget-footprint-check', {placementId:this.placementId,
                        before:this.before, after:ints(this.placementContext,23), result:result.toUInt32()});
            }
        });
        hook(0x6800f0, {
            onEnter(args) {
                this.placementId = placements.get(this.threadId);
                if (!this.placementId) return;
                this.placementContext = args[3];
                this.row = {placementId:this.placementId, point:ints(this.context.ecx,2),
                    cellFlags:args[2].toUInt32(), masks:this.placementContext.add(4).readU16(),
                    before:ints(this.placementContext.add(0x2c),4)};
            },
            onLeave(result) {
                if (!this.placementId) return;
                bump('widget-placement-cell');
                if (counts['widget-placement-cell'] <= config.samples)
                    emit('widget-placement-cell', {...this.row,
                        after:ints(this.placementContext.add(0x2c),4), result:result.toUInt32()});
            }
        });
        hook(0x04e060, {
            onEnter(args) {
                this.placementId = placements.get(this.threadId);
                if (!this.placementId) return;
                this.row = {placementId:this.placementId,
                    point:[this.context.ecx.readU32(),this.context.edx.readU32()],
                    query:args[0].toUInt32(), mode:args[1].toUInt32()};
            },
            onLeave(result) {
                if (!this.placementId) return;
                bump('widget-placement-query');
                if (counts['widget-placement-query'] <= config.samples)
                    emit('widget-placement-query', {...this.row, result:result.toUInt32()});
            }
        });
        hook(0x69dd60, {
            onEnter(args) {
                this.recordCheck = widgetScenario;
                if (!this.recordCheck) return;
                this.row = {unit:this.context.ecx.toString(), order:args[0].toUInt32()};
            },
            onLeave(result) {
                if (!this.recordCheck) return;
                bump('widget-order-check');
                if (counts['widget-order-check'] <= config.samples)
                    emit('widget-order-check', {...this.row, result:result.toUInt32()});
            }
        });
        // Read canonical mover identity without calling retail or mutating it.
        const escapeState = unit => {
            const identity = ints(unit.add(0x16c), 2), id = identity[0] >>> 0;
            const row = {unit:unit.toString(), rawcode:unit.add(0x30).readU32(), identity,
                flags:unit.add(0x5c).readU32(), world:ints(unit.add(0x284),2),
                taskHead:ints(unit.add(0x174),2), orderHead:ints(unit.add(0x19c),2)};
            if (id === 0xffffffff) return {...row, mover:null};
            const registry = base.add(0xd68610).readPointer(), alternate = (id & 0x80000000) !== 0;
            const index = id & 0x7fffffff, limit = registry.add(alternate ? 0x3c : 0x1c).readU32();
            if (index >= limit) throw new Error('widget occupant mover outside registry');
            const slot = registry.add(alternate ? 0x2c : 0xc).readPointer().add(index*8);
            if (slot.readS32() !== -2) throw new Error('widget occupant mover is not live');
            const mover = slot.add(4).readPointer();
            if (mover.add(0x18).readS32() !== identity[1]) throw new Error('widget occupant mover epoch differs');
            const path = mover.add(0xa8).readPointer(), region = mover.add(0x98).readPointer();
            return {...row, mover:mover.toString(), pose:ints(mover.add(0x78),4),
                pathMask:path.isNull() ? null : path.add(0x9c).readU32(),
                regionMask:region.isNull() ? null : region.add(0x34).readU32()};
        };
        hook(0x654090, {
            onEnter() {
                this.recordEscape = widgetScenario;
                if (!this.recordEscape) return;
                this.unit = this.context.ecx;
                this.row = {context:ints(this.context.edx,8), before:escapeState(this.unit)};
            },
            onLeave(result) {
                if (!this.recordEscape) return;
                bump('widget-escape');
                if (counts['widget-escape'] <= config.samples)
                    emit('widget-escape', {...this.row, result:result.toUInt32(), after:escapeState(this.unit)});
            }
        });
        for (const [rva, method] of [[0x6501a0, 'create'], [0x650c00, 'destroy'],
                                     [0x6514d0, 'remove-mask'], [0x6544f0, 'reapply']]) {
            hook(rva, {
                onEnter() {
                    this.recordWidget = widgetScenario;
                    if (!this.recordWidget) return;
                    this.widget = this.context.ecx;
                    this.row = {method, widget: this.widget.toString(),
                        vtable: this.widget.readPointer().sub(base).toString(),
                        beforeCollection: this.widget.add(0x34).readPointer().toString(),
                        gate: base.add(0xce5f10).readU32()};
                },
                onLeave() {
                    if (!this.recordWidget) return;
                    bump('widget-' + method);
                    if (counts['widget-' + method] <= config.samples)
                        emit('widget-method', {...this.row,
                            afterCollection: this.widget.add(0x34).readPointer().toString()});
                }
            });
        }
    }
    if (config.taskEvents) {
        const taskState = ability => {
            const unit = ability.add(0x30).readPointer();
            return {ability: ability.toString(), unit: unit.toString(),
                abilityFlags: ability.add(0x20).readU32(),
                taskHead: unit.isNull() ? null : ints(unit.add(0x174), 2),
                orderHead: unit.isNull() ? null : ints(unit.add(0x19c), 2),
                unitFlags: unit.isNull() ? null : unit.add(0x5c).readU32()};
        };
        hook(0x5ffb60, {
            onEnter(args) {
                this.ability = this.context.ecx;
                const packet = args[0], task = packet.add(0xc).readPointer();
                this.row = {before: taskState(this.ability), eventCode: packet.add(8).readU32(),
                    task: task.toString(), taskVtable: task.readPointer().sub(base).toString(),
                    taskIdentity: ints(task.add(0xc), 2), successor: ints(task.add(0x24), 2),
                    destination: [task.add(0x38).readFloat(), task.add(0x40).readFloat()],
                    range: task.add(0x48).readFloat()};
            },
            onLeave() {
                bump('point-task');
                if (counts['point-task'] <= config.samples)
                    emit('point-task', {...this.row, after: taskState(this.ability)});
            }
        });
        for (const [rva, kind] of [[0x603110, 'task-cant-path'],
                                  [0x5fb190, 'task-recovery'], [0x600340, 'task-cleanup']]) {
            hook(rva, {
                onEnter() { this.ability = this.context.ecx; this.before = taskState(this.ability); },
                onLeave() {
                    bump(kind);
                    if (counts[kind] <= config.samples)
                        emit(kind, {before: this.before, after: taskState(this.ability)});
                }
            });
        }
        hook(0x691e60, {
            onEnter(args) {
                this.unit = this.context.ecx; this.task = args[0];
                this.row = {unit: this.unit.toString(), task: this.task.toString(),
                    taskIdentity: ints(this.task.add(0xc), 2), eventCode: this.task.add(0x30).readU32(),
                    beforeHead: ints(this.unit.add(0x174), 2)};
            },
            onLeave() {
                bump('task-prepend');
                if (counts['task-prepend'] <= config.samples)
                    emit('task-prepend', {...this.row, successor: ints(this.task.add(0x24), 2),
                        afterHead: ints(this.unit.add(0x174), 2)});
            }
        });
        hook(0x5fa7a0, {
            onEnter() { this.ability = this.context.ecx; this.before = taskState(this.ability); },
            onLeave() {
                bump('task-arrival');
                if (counts['task-arrival'] <= config.samples)
                    emit('task-arrival', {before: this.before, after: taskState(this.ability)});
            }
        });
    }
    if (config.blockers) {
        hook(0x1489a0, {
            onEnter(args) {
                this.request = active.get(this.threadId);
                if (!this.request || this.request.kind !== 'fine') return;
                this.system = this.context.ecx;
                this.x = args[0].toInt32(); this.y = args[1].toInt32();
            },
            onLeave(ret) {
                if (!this.request || this.request.kind !== 'fine' || ret.toInt32() !== 0) return;
                const system = this.system, map = system.add(0x1c).readPointer(), stats = this.request.blockers;
                const [width, height] = ints(map.add(0x3c), 2);
                if (this.x < 0 || this.y < 0 || this.x >= width || this.y >= height) { stats.boundsHits++; return; }
                const word = map.add(0x28).readPointer().add(4 * (this.y * width + this.x)).readU32();
                const mask = system.add(0xa4).readU32(), mode = system.add(0xd4).readU32();
                if ((word & mask & 0xff000000) !== 0) { stats.terrainHits++; return; }
                const links = map.add(0x78).readPointer(), seen = new Set();
                let index = word & 0xffffff;
                for (let n = 0; index !== 0xffffff && n < 4096; n++) {
                    const link = links.add(index * 8), code = link.readU32(), kind = code >>> 24;
                    index = code & 0xffffff;
                    if (kind === 2) continue;
                    const object = link.add(4).readPointer(), key = object.toString();
                    if (seen.has(key)) continue;
                    seen.add(key);
                    const objectMask = object.add(0x34).readU32(), flags = object.add(0x40).readU32();
                    if (kind !== 1 || object.add(0x38).readU32() === 0xffffffff || !(objectMask & 0x01000000) ||
                        (flags & 0x8fffffff) || (!mode && (flags & 0x60000000)) || !(mask & objectMask & 0xffffff)) continue;
                    stats.objectHits++;
                    if (!stats.objects[key] && Object.keys(stats.objects).length < 32) {
                        const payload = object.add(0x30).readPointer();
                        const isMover = !payload.isNull() && payload.add(0x10).readU32() === 0x60706375;
                        stats.objects[key] = {hits: 0, object: key, payload: payload.toString(), isMover,
                            position: isMover ? [payload.add(0x78).readFloat(), payload.add(0x7c).readFloat()] : null,
                            flags, objectMask, queryMask: mask, mode, cell: [this.x, this.y]};
                    }
                    if (stats.objects[key]) stats.objects[key].hits++; else stats.omittedHits++;
                    return;
                }
                stats.unclassifiedHits++;
            }
        });
    }
    const separationStates = new Map();
    hook(0x1702f0, {
        onEnter() {
            this.sep = this.context.ecx;
            this.mover = this.sep.add(0x14).readPointer();
            const packed = this.sep.add(0x20).readU32();
            const vtable = this.mover.readPointer();
            this.row = {separation: this.sep.toString(), mover: this.mover.toString(), packed,
                selector: (packed >>> 16) & 15, category: (packed >>> 20) & 255, rank: packed >>> 28,
                cooldown: packed & 65535, vector: [this.sep.add(0x18).readFloat(), this.sep.add(0x1c).readFloat()],
                position: [this.mover.add(0x78).readFloat(), this.mover.add(0x7c).readFloat()],
                radius: this.mover.add(0x90).readFloat(), vtable: vtable.sub(base).toString(),
                positionCallback: vtable.add(0x54).readPointer().sub(base).toString()};
        },
        onLeave() {
            bump('separation-update');
            const key = this.row.separation;
            const signature = [this.row.selector, this.row.category, this.row.rank, this.row.positionCallback].join(':');
            if (separationStates.get(key) !== signature) {
                separationStates.set(key, signature);
                bump('separation-state');
                if (counts['separation-state'] <= config.samples) emit('separation-state', this.row);
            }
            if (this.row.cooldown === 0 && (this.row.vector.some(v => v !== 0) ||
                    this.sep.add(0x18).readFloat() !== 0 || this.sep.add(0x1c).readFloat() !== 0)) {
                bump('separation-active');
                if (counts['separation-active'] <= config.samples)
                    emit('separation-active', {...this.row,
                        afterVector: [this.sep.add(0x18).readFloat(), this.sep.add(0x1c).readFloat()],
                        afterPosition: [this.mover.add(0x78).readFloat(), this.mover.add(0x7c).readFloat()],
                        afterPacked: this.sep.add(0x20).readU32(),
                        counter: base.add(0xd53a48).readPointer().add(0x538).readU32()});
            }
        }
    });
    for (const [rva, kind] of [[0x14df20, 'spatial-clean-dirty'], [0x14dfc0, 'spatial-clean-all'], [0x14e180, 'spatial-clean-sampled']]) {
        hook(rva, {onEnter() {
            bump(kind);
            if (counts[kind] <= 16) emit(kind, {map: this.context.ecx.toString(), caller: this.returnAddress.sub(base).toString()});
        }});
    }
    hook(0x1689d0, {
        onEnter(args) {
            this.path = this.context.ecx;
            this.row = {path: this.path.toString(), position: [args[0].readFloat(), args[0].add(4).readFloat()],
                adjusted: [this.path.add(0x24).readFloat(), this.path.add(0x28).readFloat()],
                thresholdSquared: base.add(0xd54190).readFloat()};
        },
        onLeave() {
            bump('retry-init');
            if (counts['retry-init'] <= config.samples)
                emit('retry-init', {...this.row, count: this.path.add(0x98).readU32()});
        }
    });
    hook(0x167290, {
        onEnter() {
            this.path = this.context.ecx;
            this.before = this.path.add(0x98).readU32();
        },
        onLeave(ret) {
            bump('retry-result');
            if (counts['retry-result'] <= config.samples)
                emit('retry-result', {path: this.path.toString(), before: this.before,
                    after: this.path.add(0x98).readU32(), result: ret.toInt32(),
                    counter: base.add(0xd53a48).readPointer().add(0x538).readU32()});
        }
    });
    const targetRings = new Map();
    hook(0x148790, {
        onEnter(args) {
            this.row = {center: [args[0].toInt32(), args[1].toInt32()],
                target: args[2].toString(), offset: args[3].toInt32(), width: args[4].toInt32(),
                caller: this.returnAddress.sub(base).toString(), matched: null};
            targetRings.set(this.threadId, this.row);
        },
        onLeave(ret) {
            targetRings.delete(this.threadId);
            if (ret.toInt32()) {
                bump('target-perimeter-hit');
                if (counts['target-perimeter-hit'] <= config.samples)
                    emit('target-perimeter-hit', {...this.row,
                        counter: base.add(0xd53a48).readPointer().add(0x538).readU32()});
            }
        }
    });
    hook(0x14a710, {
        onEnter(args) { this.cell = [args[0].toInt32(), args[1].toInt32()]; },
        onLeave(ret) {
            const row = targetRings.get(this.threadId);
            if (row && ret.toInt32()) row.matched = this.cell;
        }
    });
    hook(0x168070, {
        onEnter(args) {
            this.path = this.context.ecx;
            const blocker = args[0];
            this.row = {path: this.path.toString(), blocker: blocker.toString(), requested: args[1].toUInt32(),
                identity: blocker.isNull() ? [-1, -1] : ints(blocker.add(0x14), 2),
                before: this.path.add(0x94).readU32(), caller: this.returnAddress.sub(base).toString(),
                counter: base.add(0xd53a48).readPointer().add(0x538).readU32()};
        },
        onLeave() {
            bump('yield-set');
            if (counts['yield-set'] <= config.samples) emit('yield-set', {...this.row,
                after: this.path.add(0x94).readU32(), stored: ints(this.path.add(0xa8), 2)});
        }
    });
    const advances = new Map();
    hook(0x165ae0, {
        onEnter() { this.path = this.context.ecx; this.delay = this.path.add(0x94).readU32(); this.disabled = !!(this.path.add(0x88).readU32() & 0x100000); },
        onLeave(ret) {
            if (this.delay) {
                bump('path-delay');
                if (counts['path-delay'] <= config.samples) emit('path-delay', {path: this.path.toString(), before: this.delay, after: this.path.add(0x94).readU32(), disabled: this.disabled, result: ret.toInt32(), counter: base.add(0xd53a48).readPointer().add(0x538).readU32()});
            }
            const row = {path: this.path.toString(), result: ret.toInt32(),
                flags: this.path.add(0x88).readU32(), target: this.path.add(0xa4).readPointer().toString(), indices: ints(this.path.add(0x74), 2),
                destination: [this.path.add(0x1c).readFloat(), this.path.add(0x20).readFloat()],
                counter: base.add(0xd53a48).readPointer().add(0x538).readU32()};
            advances.set(row.path, row);
        }
    });
    hook(0x171060, {
        onEnter() {
            bump('force-arrival');
            this.mover = this.context.ecx;
            const path = this.mover.add(0xa8).readPointer();
            this.row = {mover: this.mover.toString(), path: path.toString(),
                caller: this.returnAddress.sub(base).toString(), before: this.mover.add(0xd8).readU32(),
                advance: advances.get(path.toString()) || null,
                counter: base.add(0xd53a48).readPointer().add(0x538).readU32()};
        },
        onLeave() {
            if (counts['force-arrival'] <= config.samples)
                emit('force-arrival', {...this.row, after: this.mover.add(0xd8).readU32()});
        }
    });
    hook(0x651010, {onEnter(args) {
        bump('target-lost-dispatch');
        if (counts['target-lost-dispatch'] <= config.samples)
            emit('target-lost-dispatch', {targetUnit: this.context.ecx.toString(),
                players: [args[0].toInt32(), args[1].toInt32()],
                caller: this.returnAddress.sub(base).toString()});
    }});
    hook(0x5ff490, {onEnter(args) {
        bump('move-target-lost');
        if (counts['move-target-lost'] <= config.samples)
            emit('move-target-lost', {ability: this.context.ecx.toString(),
                ownerUnit: this.context.ecx.add(0x30).readPointer().toString(),
                eventCode: args[0].add(8).readU32(),
                targetUnit: args[0].add(0xc).readPointer().toString()});
    }});
    hook(0x5fb940, {
        onEnter(args) {
            bump('move-target-validation');
            this.row = {ability: this.context.ecx.toString(), target: args[0].toString()};
        },
        onLeave(ret) {
            if (counts['move-target-validation'] <= config.samples)
                emit('move-target-validation', {...this.row, result: ret.toUInt32()});
        }
    });
    hook(0x171340, {onEnter() {
        bump('mover-stop');
        if (counts['mover-stop'] <= config.samples)
            emit('mover-stop', {mover: this.context.ecx.toString(),
                caller: this.returnAddress.sub(base).toString(),
                stack: Thread.backtrace(this.context, Backtracer.ACCURATE)
                    .filter(p => p.compare(base) >= 0 && p.compare(base.add(config.imageSize)) < 0)
                    .map(p => p.sub(base).toString())});
    }});
    const completionStates = new Map();
    hook(0x16c390, {onEnter() {
        bump('group-completion-test');
        const group = this.context.ecx, count = group.add(0x38).readU32();
        const members = group.add(0x28).readPointer();
        const flags = group.add(0x80).readU32(), missed = group.add(0x6c).readU32();
        const row = {group: group.toString(), flags, missed, count,
            gateOpen: !(flags & 1) || missed >= 33,
            counter: base.add(0xd53a48).readPointer().add(0x538).readU32(),
            members: Array.from({length: Math.min(count, 16)}, (_, i) => ({
                mover: members.add(i * 0x2c + 0x14).readPointer().toString(),
                flags: members.add(i * 0x2c + 0x28).readU32()}))};
        const key = JSON.stringify([flags, row.gateOpen, row.members]);
        if (completionStates.get(row.group) !== key) {
            bump('group-completion');
            if (counts['group-completion'] <= config.samples) emit('group-completion', row);
        }
        completionStates.set(row.group, key);
    }});
    const visibilityStates = new Map();
    hook(0x23a760, {
        onEnter() {
            bump('target-visibility-test');
            this.group = this.context.edx;
            this.row = {group: this.group.toString(), target: this.context.ecx.toString(),
                path: this.group.add(0x3c).readPointer().toString(),
                countdown: this.group.add(0x64).readS32(), missed: this.group.add(0x6c).readU32(),
                counter: base.add(0xd53a48).readPointer().add(0x538).readU32()};
        },
        onLeave(ret) {
            const row = {...this.row, blocked: ret.toInt32()};
            const key = JSON.stringify([row.target, row.blocked]);
            if (visibilityStates.get(row.group) !== key) {
                bump('target-visibility');
                if (counts['target-visibility'] <= config.samples) emit('target-visibility', row);
            }
            visibilityStates.set(row.group, key);
        }
    });
    const refreshPending = new Map();
    hook(0x169680, {
        onEnter() {
            bump('target-refresh-update');
            this.group = this.context.ecx;
            this.key = this.group.toString();
            this.refresh = this.group.add(0x64).readS32() === -1;
            if (this.refresh) refreshPending.set(this.key, {
                group: this.key, path: this.group.add(0x3c).readPointer().toString(),
                flags: this.group.add(0x80).readU32(), coefficient: base.add(0xd541b8).readFloat(),
                counter: base.add(0xd53a48).readPointer().add(0x538).readU32()});
        },
        onLeave() {
            if (!this.refresh) return;
            const row = refreshPending.get(this.key);
            refreshPending.delete(this.key);
            bump('target-refresh');
            if (counts['target-refresh'] <= config.samples)
                emit('target-refresh', {...row, reload: this.group.add(0x64).readS32()});
        }
    });
    hook(0x169727, {onEnter() {
        const row = refreshPending.get(this.context.ebx.toString());
        if (row) row.distanceFine = this.context.ebp.sub(8).readFloat();
    }});
    hook(0x16974d, {onEnter() {
        const row = refreshPending.get(this.context.ebx.toString());
        if (row) row.unclamped = this.context.eax.toInt32();
    }});
    hook(0x168b80, {
        onEnter(args) {
            bump('path-destination');
            this.path = this.context.ecx;
            this.row = {path: this.path.toString(), caller: this.returnAddress.sub(base).toString(),
                destination: [args[0].readFloat(), args[0].add(4).readFloat()],
                replaceOriginal: args[1].toInt32(), beforeFlags: this.path.add(0x88).readU32()};
        },
        onLeave() {
            if (counts['path-destination'] <= config.samples)
                emit('path-destination', {...this.row, afterFlags: this.path.add(0x88).readU32(),
                    original: [this.path.add(0x2c).readFloat(), this.path.add(0x30).readFloat()],
                    counts: [this.path.add(0x50).readU32(), this.path.add(0x70).readU32()],
                    indices: ints(this.path.add(0x74), 2), timestamps: ints(this.path.add(0x7c), 2)});
        }
    });
    let replanSamples = 0;
    const replanStates = new Map();
    hook(0x167e40, {
        onEnter(args) {
            bump('replan-check');
            this.path = this.context.ecx;
            this.readyOut = args[2];
            this.row = {path: this.path.toString(),
                oldDestination: [this.path.add(0x1c).readFloat(), this.path.add(0x20).readFloat()],
                destination: [args[0].readFloat(), args[0].add(4).readFloat()], shift: args[1].toUInt32(),
                timestamps: [this.path.add(0x7c).readU32(), this.path.add(0x80).readU32()],
                counter: base.add(0xd53a48).readPointer().add(0x538).readU32()};
        },
        onLeave(ret) {
            const row = {...this.row, changed: ret.toInt32(), ready: this.readyOut.readU32()};
            const key = JSON.stringify([row.oldDestination, row.destination, row.changed, row.ready]);
            if (replanSamples < config.samples && replanStates.get(row.path) !== key) {
                emit('replan-check', row);
                replanSamples++;
            }
            replanStates.set(row.path, key);
        }
    });
    const arrivalStates = new Map();
    let arrivalSamples = 0;
    hook(0x16e910, {
        onEnter(args) {
            bump('arrival-test');
            this.mover = this.context.ecx;
            this.angleOut = args[4];
            this.rangeOut = args[5];
            this.words = {source:ints(args[0],2).map(v=>v>>>0), destination:ints(args[3],2).map(v=>v>>>0),
                heading:args[1].readU32(), threshold:args[2].readU32(), footprint:this.mover.add(0x90).readU32(),
                storedRange:this.mover.add(0xb0).readU32(), storedPosition:ints(this.mover.add(0x78),2).map(v=>v>>>0)};
            this.row = {mover: this.mover.toString(),
                source: [args[0].readFloat(), args[0].add(4).readFloat()],
                destination: [args[3].readFloat(), args[3].add(4).readFloat()],
                threshold: args[2].readFloat(), footprint: this.mover.add(0x90).readFloat(),
                flags: this.mover.add(0xd8).readU32()};
        },
        onLeave(ret) {
            const row = {...this.row, result: ret.toInt32(),
                angle: this.angleOut.readFloat(), inRange: this.rangeOut.readU32()};
            if (config.motionEvents) {
                bump('arrival-evaluation');
                if (counts['arrival-evaluation'] <= config.samples)
                    emit('arrival-evaluation', {...this.words, mover:row.mover, flags:row.flags,
                        result:row.result, inRange:row.inRange, angle:this.angleOut.readU32()});
            }
            const previous = arrivalStates.get(row.mover);
            if (arrivalSamples < config.samples && (!previous || row.result !== previous.result ||
                row.inRange !== previous.inRange || row.threshold !== previous.threshold)) {
                emit('arrival-transition', {...row, previous: previous || null});
                arrivalSamples++;
            }
            arrivalStates.set(row.mover, row);
        }
    });
    hook(0x16d9d0, {onEnter(args) {
        completionStates.delete(this.context.ecx.toString());
        visibilityStates.delete(this.context.ecx.toString());
        bump('group-target');
        if (counts['group-target'] <= config.samples)
            emit('group-target', {group: this.context.ecx.toString(),
                path: this.context.ecx.add(0x3c).readPointer().toString(),
                target: args[0].toString(),
                handle: args[0].isNull() ? null : ints(args[0].add(0x14), 2)});
    }});
    hook(0x168ab0, {
        onEnter(args) {
            bump('scheduler-target');
            this.path = this.context.ecx;
            this.before = this.path.add(0x88).readU32();
            this.value = args[0].toUInt32();
        },
        onLeave() {
            if (counts['scheduler-target'] <= config.samples)
                emit('scheduler-target', {path: this.path.toString(), value: this.value,
                    before: this.before, after: this.path.add(0x88).readU32(),
                    accLimit: this.path.add(0x86).readU16()});
        }
    });
    hook(0x168310, {
        onEnter(args) {
            bump('scheduler-admission');
            this.bucket = this.context.ecx;
            this.path = args[0];
            this.request = active.get(this.threadId);
            this.before = ints(this.bucket.add(4), 4);
        },
        onLeave(ret) {
            if (counts['scheduler-admission'] <= config.samples)
                emit('scheduler-admission', {path: this.path.toString(),
                    request: this.request ? this.request.request : null,
                    bucketOffset: this.bucket.sub(base.add(0xd53a90)).toUInt32(),
                    before: this.before, result: ret.toInt32(),
                    after: ints(this.bucket.add(4), 4)});
        }
    });
    hook(0x168a80, {
        onEnter(args) {
            bump('scheduler-class');
            this.path = this.context.ecx;
            this.before = this.path.add(0x88).readU32();
            this.value = args[0].toUInt32();
        },
        onLeave() {
            if (counts['scheduler-class'] <= config.samples)
                emit('scheduler-class', {path: this.path.toString(), value: this.value,
                    before: this.before, after: this.path.add(0x88).readU32()});
        }
    });
    hook(0x168f00, {
        onEnter(args) {
            bump('gate-traversal');
            this.destination = [args[0].readFloat(), args[0].add(4).readFloat()];
        },
        onLeave(ret) {emit('gate-traversal', {result: ret.toInt32(), destination: this.destination,
            destinationSpace: 'fine-grid'});}
    });
    function snapshotCells(marker) {
        if (!config.watchCell) return;
        const owner = base.add(0xd53a48).readPointer(), cells = [];
        if (owner.isNull()) throw new Error('Cell watch without pathing owner');
        for (let level = -1; level < 4; level++) {
            const map = owner.add(level === -1 ? 0x238 : 0x23c + level * 4).readPointer();
            const [width, height] = ints(map.add(0x3c), 2), shift = level + 1;
            const x = config.watchCell[0] >> shift, y = config.watchCell[1] >> shift;
            if (x >= width || y >= height) throw new Error('Watched cell outside map');
            const cell = map.add(0x28).readPointer().add((y * width + x) * (level === -1 ? 4 : 8));
            const word = cell.add(level === -1 ? 0 : 4).readU32();
            cells.push({level, x, y, word, classes: level === -1 ? null :
                [0, 2, 4, 6].map(s => (word >>> (30 - s)) & 3)});
        }
        emit('cell-snapshot', {marker, cells});
    }
    // Preload's string-intern call receives the resolved C string on the stack.
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (config.numericEvents && value.startsWith('PATHNUM ')) {
            const match = /^PATHNUM case=([a-z0-9_]+) native=([A-Za-z0-9]+)$/.exec(value);
            if (match) numericCase = {case:match[1], native:match[2]};
            else if (/^PATHNUM done=/.test(value)) numericCase = null;
            else throw new Error('Malformed numeric marker: ' + value);
            emit('numeric-marker', {value});
        }
        if (value.startsWith('PATHCROWD ')) emit('crowd-marker', {value});
        if (value.startsWith('PATHTARGET ')) emit('target-marker', {value});
        if (value.startsWith('PATHWIDGET ')) emit('widget-marker', {value});
        if (value.startsWith('PATHSTOCK ')) emit('stock-marker', {value});
        if (value.startsWith('PATHHOLD ')) emit('hold-marker', {value});
        if (value.startsWith('PATHTRACE ')) {
            if (value.includes('label=start_widget_lifecycle ') || value.includes('label=start_widget_escape ') || value.includes('label=start_widget_build_escape '))
                widgetScenario = true;
            emit('marker', {value});
            if (!value.includes('label=sample ')) snapshotCells(value);
        }
    }});
    hook(0x2148f0, {onEnter(args) {
        emit('terrain-native', {x: args[0].readFloat(), y: args[1].readFloat(),
            pathingType: args[2].toInt32(), passable: args[3].toInt32()});
    }});
    for (const [kind, rva] of [['fine', 0x166e90], ['acc', 0x166c30]]) {
        hook(rva, {
            onEnter() {
                this.thread = this.threadId;
                this.prev = active.get(this.thread);
                this.row = {request: ++serial, kind, path: this.context.ecx.toString(),
                    footprint: this.context.ecx.add(0xb4).readFloat()};
                if (config.blockers && kind === 'fine') this.row.blockers = {objectHits: 0, terrainHits: 0, boundsHits: 0, omittedHits: 0, unclassifiedHits: 0, objects: {}};
                active.set(this.thread, this.row);
                bump(kind + '-request');
            },
            onLeave(ret) {
                if (this.row.request <= config.samples) {
                    const path = ptr(this.row.path), offset = kind === 'fine' ? 0x34 : 0x54;
                    const count = path.add(offset + 0x1c).readU32();
                    if (count > 65536) throw new Error('Unexpected route count ' + count);
                    const points = [], data = path.add(offset + 0x0c).readPointer();
                    for (let i = 0; i < Math.min(count, 256); i++)
                        points.push([data.add(i * 8).readFloat(), data.add(i * 8 + 4).readFloat()]);
                    emit('route', {...this.row, result: ret.toInt32(), count, points,
                        truncated: count > points.length,
                        indices: ints(path.add(0x74), 2), flags: path.add(0x88).readU32()});
                }
                if (this.prev) active.set(this.thread, this.prev);
                else active.delete(this.thread);
            }
        });
    }
    for (const [kind, rva, nodeoff, totaloff, budgetoff, popoff, goaloff] of [
        ['fine', 0x14a4c0, 0x30, 0x40, 0x68, 0x6c, 0x88],
        ['acc', 0x163f50, 0x5c, 0x6c, 0x98, 0x9c, 0xbc]
    ]) {
        hook(rva, {
            onEnter() {
                bump(kind + '-search');
                this.self = this.context.ecx;
                this.request = active.get(this.threadId);
            },
            onLeave(ret) {
                if (samples >= config.samples) return;
                samples++;
                const p = this.self, count = p.add(totaloff).readU32();
                if (count > 65536) throw new Error('Unexpected node count ' + count);
                const row = {...this.request, kind, system: p.toString(), result: ret.toInt32(),
                    pops: p.add(popoff).readU32(), budget: p.add(budgetoff).readU32(),
                    nodes: count, goal: ints(p.add(goaloff), 2)};
                if (kind === 'acc') {
                    const nodes = p.add(nodeoff).readPointer(), levels = {};
                    for (let i = 0; i < count; i++) {
                        const level = nodes.add(i * 36 + 34).readU8();
                        levels[level] = (levels[level] || 0) + 1;
                    }
                    row.levels = levels;
                    row.sizeClass = p.add(0x90).readU32();
                    row.maskShift = p.add(0xd4).readU32();
                } else row.footprintClass = p.add(0xa0).readU16();
                emit('search', row);
            }
        });
    }
    hook(0x15ab60, {
        onEnter() {this.self = this.context.ecx; bump('maps-create');},
        onLeave() {
            const maps = [];
            for (const offset of [0x234, 0x238, 0x23c, 0x240, 0x244, 0x248]) {
                const p = this.self.add(offset).readPointer();
                maps.push({slot: offset, pointer: p.toString(), dimensions: ints(p.add(0x3c), 2),
                    scale: p.add(0x64).readFloat(), inverseScale: p.add(0x68).readFloat()});
            }
            emit('maps', {owner: this.self.toString(), maps,
                runtimeConstants: Object.fromEntries([0xd3c740, 0xd3c744, 0xd3c748, 0xd53a74]
                    .map(rva => [rva.toString(16), base.add(rva).readFloat()]))});
        }
    });
    hook(0x15d360, {
        onEnter(args) {
            bump('hierarchy-update');
            if (rebuildSamples++ >= 32) return;
            emit('hierarchy-update', {owner: this.context.ecx.toString(),
                rect: args[0].isNull() ? null : ints(args[0], 4), mode: args[1].toInt32(),
                caller: this.returnAddress.sub(base).toString(), request: active.get(this.threadId)});
        }
    });
    for (const [name, rva] of [['base-rebuild', 0x15cf80], ['parent-rebuild', 0x15d470]])
        hook(rva, {onEnter() {bump(name);}});
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {status() {return {installed, samples, counts};}};
