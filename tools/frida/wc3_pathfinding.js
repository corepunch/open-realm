// WC3 1.27.1.7085. Entry/exit observers; no gameplay calls or target data writes.
let installed = false, samples = 0, serial = 0, rebuildSamples = 0, recording = true;
let widgetScenario = false;
let numericCase = null;
let randomCase = null;
let speedCase = null;
let positionCase = null;
let pairScenario = false, formationRankScenario = false;
let resizeScenario = false;
const resizeTargets = new Map();
let clockScenario = false, clockSerial = 0;
let schedulerMutationScenario = false, moverRetirementScenario = false;
const retirementActors = [], retirementUnits = new Set();
const counts = {}, active = new Map();
const headingActive = new Map();
const emit = (event, data = {}) => {if (recording) send({event, ms: Date.now(), ...data});};
const bump = kind => {if (!recording) return; counts[kind] = (counts[kind] || 0) + 1;};
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
    if (config.fineResultEvents) {
        hook(0x148100, {
            onEnter(args) {
                this.fine=this.context.ecx; this.route=args[0];
                this.source=ints(args[1],2); this.goal=ints(args[2],2);
                this.limit=args[4].toUInt32(); this.radius=args[5].readFloat();
            },
            onLeave(result) {
                const n=this.route.add(0x1c).readU32(), p=this.route.add(0xc).readPointer();
                if(n>32768)throw new Error('Unexpected complete fine route '+n);
                const words=[];for(let i=0;i<n;i++)words.push(ints(p.add(8*i),2));
                emit('fine-result',{fine:this.fine.toString(),source:this.source,goal:this.goal,
                    limit:this.limit,radius:this.radius,result:result.toInt32(),words,
                    work:this.fine.add(0x6c).readU32(),nodes:this.fine.add(0x40).readU32()});
            }
        });
    }
    if(config.gatePoolEvents) {
        hook(0x04e550,{onEnter(){this.id=this.context.ecx.toUInt32();this.world=ints(this.context.edx,2);},onLeave(){const owner=base.add(0xd53a48).readPointer(),acc=owner.add(0x250).readPointer(),records=acc.add(0x3c).readPointer();emit('gate-destination',{id:this.id,world:this.world,record:ints(records.add(12*this.id),3)});}});
        hook(0x165d10,{onEnter(args){this.path=this.context.ecx;this.execute=args[0].toUInt32();this.io=args[1];const index=this.path.add(0x78).readU32(),points=this.path.add(0x60).readPointer();this.before={execute:this.execute,index,count:this.path.add(0x70).readU32()};if(index>1){this.before.predecessor=ints(points.add(8*(index-1)),2);this.before.cached=ints(points.add(8*(index-2)),2);} },onLeave(ret){emit('gate-consumer',{path:this.path.toString(),before:this.before,index:this.path.add(0x78).readU32(),result:ret.toUInt32(),warped:this.io.readU32()});}});
    }
    if(config.gateMarkerEvents) {
        hook(0x04e360,{onEnter(){this.id=this.context.ecx.toUInt32();this.rectangle=ints(this.context.edx,4);},onLeave(){
            const owner=base.add(0xd53a48).readPointer(),maps=[];
            for(let level=0;level<4;level++){
                const map=owner.add(0x23c+4*level).readPointer(),width=map.add(0x3c).readU32(),height=map.add(0x40).readU32(),data=map.add(0x28).readPointer();
                if(width*height>4096)throw new Error('Unexpected marker map extent');
                const markers=[],classes=[];for(let i=0;i<width*height;i++){markers.push(data.add(8*i+6).readU8());classes.push(data.add(8*i+7).readU8());}
                maps.push({width,height,markers,classes});
            }
            emit('gate-marker-publish',{id:this.id,rectangle:this.rectangle,maps});
        }});


    }
    if(config.gatePoolEvents) {
        const pool=()=>{const owner=base.add(0xd3c82c).readPointer();if(owner.isNull())return null;const n=owner.add(0x4c).readU32(),p=owner.add(0x50).readPointer();if(n!==256)throw new Error('Unexpected warp pool '+n);const used=[];for(let i=0;i<n;i++)used.push(p.add(i).readU8());return {used,active:owner.add(0x54).readU32()};};
        hook(0x04e510,{onEnter(){this.before=pool();},onLeave(ret){emit('gate-pool-allocate',{id:ret.toUInt32(),before:this.before,after:pool()});}});
        hook(0x04e4c0,{onEnter(){this.id=this.context.ecx.toUInt32();this.before=pool();},onLeave(){emit('gate-pool-release',{id:this.id,before:this.before,after:pool()});}});
        hook(0x04e210,{onEnter(){this.id=this.context.ecx.toUInt32();this.enabled=this.context.edx.toUInt32();},onLeave(){emit('gate-pool-activate',{id:this.id,enabled:this.enabled,after:pool()});}});
        hook(0x2194c0,{onEnter(args){this.unit=args[0].toUInt32();},onLeave(ret){emit('gate-pool-active-query',{unit:this.unit,result:ret.toUInt32()});}});
    }
    if (config.adaptiveStorageEvents) {
        const tables = new Map();
        const table = p => ({bytes:p.add(0x10).readU32(),grow:p.add(0x14).readU32(),capacity:p.add(0x18).readU32(),count:p.add(0x1c).readU32()});
        hook(0x14f570,{
            onEnter(){this.search=this.context.ecx;tables.set(this.search.add(0x50).toString(),'nodes');tables.set(this.search.add(0x70).toString(),'heap');},
            onLeave(){emit('adaptive-storage-constructed',{search:this.search.toString(),index:table(this.search.add(0x30)),nodes:table(this.search.add(0x50)),heap:table(this.search.add(0x70))});}
        });
        for (const [kind,rva] of [['nodes',0x148670],['heap',0x1486f0]]) hook(rva,{
            onEnter(args){this.row=null;const p=this.context.ecx;if(tables.get(p.toString())!==kind)return;const before=table(p),amount=args[1].toUInt32();if(before.count+amount<=before.capacity)return;this.p=p;this.row={kind,table:p.toString(),amount,before};},
            onLeave(result){if(this.row)emit('adaptive-storage-growth',{...this.row,result:result.toUInt32(),after:table(this.p)});}
        });
    }
    if (config.fineStorageEvents) {
        const fineTables = new Map();
        const table = p => ({bytes:p.add(0x10).readU32(),grow:p.add(0x14).readU32(),capacity:p.add(0x18).readU32(),count:p.add(0x1c).readU32()});
        hook(0x147600,{onEnter(){this.fine=this.context.ecx;fineTables.set(this.fine.add(0x24).toString(),'nodes');fineTables.set(this.fine.add(0x44).toString(),'heap');},onLeave(){emit('fine-storage-constructed',{fine:this.fine.toString(),nodes:table(this.fine.add(0x24)),heap:table(this.fine.add(0x44))});}});
        for (const [kind,rva] of [['nodes',0x148670],['heap',0x1486f0]]) hook(rva,{
            onEnter(args){this.row=null;const p=this.context.ecx;if(fineTables.get(p.toString())!==kind)return;const before=table(p),amount=args[1].toUInt32();if(before.count+amount<=before.capacity)return;this.p=p;this.row={kind,table:p.toString(),amount,before};},
            onLeave(result){if(this.row)emit('fine-storage-growth',{...this.row,result:result.toUInt32(),after:table(this.p)});}
        });
    }
    hook(0x48e8a0,{onEnter(args){if(positionCase!=='speed_modifiers')return;const b=args[0];if(b.isNull())return;const vt=b.readPointer();emit('modifier-detach',{unit:this.context.ecx.toString(),buff:b.toString(),vtable:vt.sub(base).toUInt32(),methods:[0x1c,0x1d8,0x1dc,0x1e0,0x1e4,0x1e8,0x32c,0x330].map(o=>vt.add(o).readPointer().sub(base).toUInt32()),words:ints(b,32).map(v=>v>>>0)});}});
    const modifierClock=()=>{const owner=base.add(0xd53a48).readPointer();return {primary:ints(owner.add(0x54),4).map(v=>v>>>0),counter:owner.add(0x538).readU32()};};
    hook(0x5fc900,{onEnter(args){this.observe=false;if(positionCase!=='speed_modifiers')return;this.move=this.context.ecx;this.unit=this.move.add(0x30).readPointer();if(this.unit.isNull()||this.unit.add(0x30).readU32()!==0x68563830)return;this.observe=true;this.out=args[0];this.row={unit:this.unit.toString(),move:this.move.toString(),base:this.move.add(0x70).readU32(),multiplier:this.move.add(0x78).readU32(),...modifierClock()};},onLeave(){if(this.observe)emit('modifier-speed-effective',{...this.row,output:this.out.readU32()});}});

    for(const [name,rva,n] of [['UnitRemoveBuffs',0x218d90,3],['UnitRemoveBuffsEx',0x218dc0,8]])hook(rva,{onEnter(args){this.row=positionCase==='speed_modifiers'?{name,args:Array.from({length:n-1},(_,i)=>args[i+1].toUInt32()),...modifierClock()}:null;if(this.row)emit('modifier-remove-begin',this.row);},onLeave(){if(this.row)emit('modifier-remove-end',{...this.row,...modifierClock()});}});
    hook(0x48eb10,{onEnter(args){this.row=positionCase==='speed_modifiers'?{unit:this.context.ecx.toString(),args:Array.from({length:9},(_,i)=>args[i].toUInt32()),...modifierClock()}:null;if(this.row)emit('modifier-filter-begin',this.row);},onLeave(ret){if(this.row)emit('modifier-filter-end',{...this.row,count:ret.toUInt32(),...modifierClock()});}});
    if (config.motionEvents) {
        for (const rva of [0x6b9f70,0x6baaa0,0x6bb050,0x6bb980]) hook(rva, {onEnter() {
            const action=this.context.ecx;
            emit('player-order-variant', {entry:rva,action:action.toString(),
                words:Array.from({length:16},(_,i)=>action.add(i*4).readU32()),
                caller:this.returnAddress.sub(base).toUInt32()});
        }});
        // NetUnit.cpp point action: public player/flags/order and native world coordinates.
        for (const rva of [0x6b98f0,0x6b9f70]) hook(rva, {onEnter() {
            const action=this.context.ecx, owner=base.add(0xd53a48).readPointer();
            this.row={entry:rva,action:action.toString(), player:action.add(0x15).readU8(),
                flags:action.add(0x18).readU16(), order:action.add(0x1c).readU32(),
                point:[action.add(0x28).readU32(),action.add(0x2c).readU32()],
                clock:[owner.add(0x54).readU32(),owner.add(0x58).readU32()],
                counter:owner.add(0x538).readU32()};
            emit('player-point-action-begin',this.row);
        },onLeave() {emit('player-point-action-end',this.row);}});
        hook(0x6b8c10, {onEnter() {
            const ctx=this.context.edx;
            emit('player-point-attach', {unit:this.context.ecx.toString(),
                requests:[ctx.readPointer().toString(),ctx.add(4).readPointer().toString(),ctx.add(8).readPointer().toString()]});
        }});
        hook(0x6ba800, {onEnter() {
            this.ctx=this.context.edx; this.before=this.ctx.add(0x38).readU32();
            this.row={unit:this.context.ecx.toString(),countBefore:this.before};
            emit('player-point-target-admit-begin',this.row);
        },onLeave() {
            const count=this.ctx.add(0x38).readU32();
            emit('player-point-target-admit-end',{...this.row,countAfter:count,
                row:count>this.before && count<=12 ? Array.from({length:9},(_,i)=>this.ctx.add(0x3c+(count-1)*36+i*4).readU32()) : null});
        }});
        hook(0x6b93a0, {onEnter(args) {
            emit('player-order-publish',{unit:this.context.ecx.toString(),order:this.context.edx.toString(),
                flags:args[0].toUInt32()&0xffff,fallback:args[1].toUInt32()});
        }});
        hook(0x6ba590, {onEnter() {
            this.ctx=this.context.edx; this.before=this.ctx.add(0x40).readU32();
            this.row={unit:this.context.ecx.toString(),countBefore:this.before};
            emit('player-point-admit-begin',this.row);
        },onLeave() {
            const count=this.ctx.add(0x40).readU32();
            emit('player-point-admit-end',{...this.row,countAfter:count,
                row:count>this.before && count<=12 ? Array.from({length:9},(_,i)=>this.ctx.add(0x44+(count-1)*36+i*4).readU32()) : null});
        }});


        const previousRequestState = unit => ({unit:unit.toString(),
            previous:ints(unit.add(0x240),2),category:unit.add(0x1fc).readU32(),
            mover:ints(unit.add(0x16c),2)});
        hook(0x5fa950,{onEnter(args) {
            this.unit=this.context.ecx;this.output=args[1];
            const owner=base.add(0xd53a48).readPointer();
            this.row={before:previousRequestState(this.unit),point:[this.context.edx.readU32(),args[0].readU32()],
                clock:owner.add(0x54).readU32(),counter:owner.add(0x538).readU32()};
        },onLeave(ret) {emit('move-previous-cohort-search',{...this.row,result:ret.toUInt32(),
            output:this.output.readPointer().toString(),after:previousRequestState(this.unit)});}});
        hook(0x5faaf0,{onEnter() {
            this.ctx=this.context.edx;
            this.row={candidate:previousRequestState(this.context.ecx),
                source:previousRequestState(this.ctx.readPointer()),context:ints(this.ctx,10)};
        },onLeave(ret) {emit('move-previous-cohort-candidate',{...this.row,result:ret.toUInt32(),
            accepted:this.ctx.add(0x20).readU32(),request:this.ctx.add(0x1c).readPointer().toString()});}});
        // Pending CMoveReq candidates are distinct from active physical group rows.
        const requestState = request => ({request:request.toString(),
            identity:ints(request.add(0x14),2),flags:request.add(0x100).readU32(),
            wrapper:request.add(0xf0).readPointer().toString(),
            candidates:Array.from({length:12},(_,i)=>ints(request.add(0x1c+i*12),3)),
            overrides:ints(request.add(0xac),12)});
        hook(0x16bdb0,{onEnter(args) {
            this.request=this.context.ecx;
            const owner=base.add(0xd53a48).readPointer();
            this.row={before:requestState(this.request),index:args[0].toUInt32(),
                point:ints(args[1],2),clock:owner.add(0x54).readU32(),counter:owner.add(0x538).readU32()};
        },onLeave(ret) {emit('move-request-activate',{...this.row,result:ret.toUInt32(),after:requestState(this.request)});}});
        hook(0x16b7b0,{onEnter(args) {
            this.request=this.context.ecx;
            this.row={before:requestState(this.request),group:args[0].toString(),index:args[1].toUInt32()};
        },onLeave() {emit('move-request-bind-candidate',{...this.row,after:requestState(this.request)});}});
        hook(0x169620,{onEnter(args) {
            this.request=this.context.ecx; this.mover=args[0];
            this.row={mover:this.mover.toString(),override:args[1].toUInt32(),
                before:requestState(this.request),caller:this.returnAddress.sub(base).toUInt32()};
        },onLeave() {emit('move-request-candidate',{...this.row,after:requestState(this.request)});}});
        hook(0x16de20,{onEnter(args) {
            this.request=this.context.ecx;this.wrapper=args[0];
        },onLeave() {emit('move-request-created',{...requestState(this.request),suppliedWrapper:this.wrapper.toString()});}});
        hook(0x170fa0,{onEnter(args) {
            this.mover=this.context.ecx;
            this.row={mover:this.mover.toString(),group:args[0].toString(),
                before:ints(this.mover.add(0x9c),2),caller:this.returnAddress.sub(base).toUInt32()};
        },onLeave() {emit('move-group-bind',{...this.row,after:ints(this.mover.add(0x9c),2)});}});
        hook(0x693490,{onEnter(args) {
            this.unit=this.context.ecx;
            this.row={unit:this.unit.toString(),order:args[0].toString(),
                before:ints(this.unit.add(0x19c),2),countBefore:this.unit.add(0x1b4).readU32()};
        },onLeave() {emit('player-order-queued',{...this.row,after:ints(this.unit.add(0x19c),2),countAfter:this.unit.add(0x1b4).readU32()});}});

        hook(0x2047f0, {onEnter(args) {
            this.row = {handle:args[0].toUInt32(), order:args[1].toUInt32(), point:[args[2].readU32(),args[3].readU32()]};
            emit('group-point-native-begin',this.row);
        },onLeave(ret) { emit('group-point-native-end',{...this.row,accepted:ret.toUInt32()}); }});
        hook(0x23acd0, {onEnter(args) {
            this.row = {group:this.context.ecx.toString(),order:args[0].toUInt32(),flags:args[1].toUInt32(),point:[args[2].readU32(),args[3].readU32()]};
            emit('group-point-request-begin',this.row);
        },onLeave(ret) {emit('group-point-request-end',{...this.row,accepted:ret.toUInt32()});}});
        for (const [name,rva] of [['attach',0x233da0],['admit',0x234160]]) hook(rva,{onEnter(args) {
            this.ctx = args[1];
            this.row = {phase:name,unit:args[0].toString(),context:this.ctx.toString(),order:this.ctx.readU32(),flags:this.ctx.add(4).readU32(),request:this.ctx.add(12).readPointer().toString(),point:[this.ctx.add(32).readU32(),this.ctx.add(36).readU32()],acceptedBefore:this.ctx.add(40).readU32()};
            emit('group-point-member-begin',this.row);
        },onLeave(ret) {emit('group-point-member-end',{...this.row,acceptedAfter:this.ctx.add(40).readU32(),output:ret.toUInt32()});}});
    }
    if (config.randomEvents) {
        const ownerWords = () => ints(base.add(0xd53a48).readPointer(),2).map(v => v >>> 0);
        for (const [name,rva] of [['SetRandomSeed',0x214140],['GetRandomInt',0x201e30],['GetRandomReal',0x201e70]]) {
            hook(rva,{onEnter(args) {
                this.row = randomCase && randomCase.native === name ? {...randomCase,before:ownerWords()} : null;
                if (this.row) this.row.input = name === 'SetRandomSeed' ? [args[0].toUInt32()] :
                    [0,1].map(i => name === 'GetRandomReal' ? args[i].readU32() : args[i].toUInt32());
            },onLeave(ret) {
                if (this.row) emit('random-native',{...this.row,after:ownerWords(),output:ret.toUInt32()});
            }});
        }
    }
    if (config.clockEvents) {
        const words = (p, n) => Array.from({length:n}, (_,i) => p.add(i*4).readU32());
        const clocks = owner => [0x14,0x68].map(offset => words(owner.add(offset+0x40),4));
        for (const [name,rva] of [['subdivide',0x04c0d0],['direct',0x04c1a0]]) hook(rva, {
            onEnter() {
                this.row = null;
                if (!clockScenario) return;
                this.owner = base.add(0xd53a48).readPointer();
                this.row = {serial:++clockSerial, source:name, input:this.context.ecx.readU32(),
                    maximum:base.add(0xd3c844).readU32(), before:clocks(this.owner),
                    caller:this.returnAddress.sub(base).toString()};
                bump('clock-source-begin'); emit('clock-source-begin',this.row);
            },
            onLeave() {
                if (!this.row) return;
                bump('clock-source-end'); emit('clock-source-end',{serial:this.row.serial,source:name,after:clocks(this.owner)});
            }
        });
        hook(0x054190, {
            onEnter() {
                this.row = null;
                if (!clockScenario) return;
                const owner = base.add(0xd53a48).readPointer();
                this.clock = this.context.edx;
                const offset = this.clock.sub(owner).toUInt32();
                if (offset !== 0x14 && offset !== 0x68) throw new Error('Unknown motion clock domain');
                this.row = {serial:++clockSerial,domain:offset,input:this.context.ecx.readU32(),
                    before:words(this.clock.add(0x40),4),caller:this.returnAddress.sub(base).toString()};
                bump('clock-advance-begin'); emit('clock-advance-begin',this.row);
            },
            onLeave(result) {
                if (!this.row) return;
                bump('clock-advance-end'); emit('clock-advance-end',{serial:this.row.serial,domain:this.row.domain,
                    after:words(this.clock.add(0x40),4),output:result.toUInt32()});
            }
        });
        hook(0x15aa80, {
            onEnter() {
                this.row = null;
                if (!clockScenario) return;
                this.owner = this.context.ecx;
                this.row = {serial:++clockSerial,clock:clocks(this.owner),counter:this.owner.add(0x538).readU32()};
                bump('clock-owner-begin'); emit('clock-owner-begin',this.row);
            },
            onLeave() {
                if (!this.row) return;
                bump('clock-owner-end'); emit('clock-owner-end',{serial:this.row.serial,clock:clocks(this.owner),
                    counter:this.owner.add(0x538).readU32()});
            }
        });
    }
    if (config.captainApproachEvents) {
        hook(0x9d86f0, {
            onEnter(args) {
                this.unit=args[1]; this.output=args[0];
                this.row={unit:this.unit.toString(),rawcode:this.unit.add(0x30).readU32(),
                    aiFlags:this.context.ecx.add(0x6c).readU32(),unitFlags:this.unit.add(0x5c).readU32(),
                    attack:this.unit.add(0x1e8).readPointer().toString(),
                    constants:ints(base.add(0xd77fb0),6).map(v=>v>>>0)};
            },
            onLeave() { emit('captain-authored-follow-range',{...this.row,range:this.output.readU32()}); }
        });
        hook(0x4985c0, {
            onEnter(args) { this.output=args[0]; this.attack=this.context.ecx; },
            onLeave() { emit('captain-max-attack-range',{attack:this.attack.toString(),range:this.output.readU32()}); }
        });
    }
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
        for(const [rva,name]of [[0x20f540,'PauseUnit'],[0x206610,'IsUnitPaused']])hook(rva,{
            onEnter(args){this.row={name,handle:args[0].toUInt32(),input:name==='PauseUnit'?args[1].toUInt32():null};},
            onLeave(result){emit('pause-native',{...this.row,output:this.row.name==='IsUnitPaused'?result.toUInt32():null});}
        });
        hook(0x215540, {
            onEnter(args) {
                this.row={handle:args[0].toUInt32(),enabled:args[1].toUInt32()};
                emit('pathing-toggle', {...this.row,phase:'enter'});
            },
            onLeave() { emit('pathing-toggle', {...this.row,phase:'leave'}); }
        });

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
        const positionNatives = new Map();
        const words = (p, n) => Array.from({length:n}, (_,i) => p.add(i*4).readU32());
        // Read the bridge's canonical identity without calling target code.
        const positionMover = bridge => {
            const id = bridge.add(8).readU32(), epoch = bridge.add(12).readU32();
            if (id === 0xffffffff) throw new Error('Position bridge has no mover');
            const registry = base.add(0xd68610).readPointer(), alternate = (id & 0x80000000) !== 0;
            const index = id & 0x7fffffff, limit = registry.add(alternate ? 0x3c : 0x1c).readU32();
            if (index >= limit) throw new Error('Position bridge identity outside registry');
            const mover = registry.add(alternate ? 0x2c : 0xc).readPointer().add(index*8+4).readPointer();
            if (mover.isNull() || mover.add(0x14).readU32() !== id || mover.add(0x18).readU32() !== epoch)
                throw new Error('Position bridge stale mover identity');
            return mover;
        };
        const forcedPositionState = unit => {
            const mover = positionMover(unit.add(0x164));
            return {taskHead:ints(unit.add(0x174),2), orderHead:ints(unit.add(0x19c),2),
                pose:words(mover.add(0x70),8), group:ints(mover.add(0x9c),2),
                hasPath:!mover.add(0xa8).readPointer().isNull()};
        };
        const stopRecoveries = new Map();
        hook(0x170080, {
            onEnter(args) {
                if (!positionCase || !positionCase.startsWith('stop_recovery_')) return;
                this.mover = this.context.ecx; this.previous = stopRecoveries.get(this.threadId);
                this.row = {case:positionCase,mover:this.mover.toString(),mask:args[0].toUInt32(),
                    limit:args[1].toUInt32(),callback:args[2].isNull() ? null : args[2].sub(base).toString(),
                    context:args[3].isNull() ? null : args[3].readU32(),policy:args[4].toUInt32(),
                    before:words(this.mover.add(0x70),8)};
                stopRecoveries.set(this.threadId,this.row);
                bump('stop-recovery-begin'); emit('stop-recovery-begin',this.row);
            },
            onLeave(result) {
                if (!this.row) return;
                bump('stop-recovery-end'); emit('stop-recovery-end',{...this.row,result:result.toUInt32(),
                    after:words(this.mover.add(0x70),8)});
                if (this.previous) stopRecoveries.set(this.threadId,this.previous);
                else stopRecoveries.delete(this.threadId);
            }
        });
        const placementQueries = new Map();
        hook(0x14a1e0, {
            onEnter(args) {
                const native = positionNatives.get(this.threadId);
                const recovery = stopRecoveries.get(this.threadId);
                if ((!native || !['SetUnitPosition','CreateUnit'].includes(native.name)) && !recovery) return;
                this.previous = placementQueries.get(this.threadId);
                this.output = args[0]; this.fine = this.context.ecx;
                this.row = {case:positionCase, unit:native ? native.unit : null, recovery:recovery ? recovery.mover : null, before:words(this.output,2),
                    rect:words(args[1],4), policy:args[2].toUInt32(), radius:args[3].readU32(),
                    mask:args[4].readU32(), limit:args[5].toUInt32(),
                    callback:args[6].isNull() ? null : args[6].sub(base).toString(),
                    context:args[7].isNull() ? null : args[7].readU32(),
                    integerResult:args[8].toUInt32(), savedMode:this.fine.add(0xd4).readU32(), visits:[]};
                placementQueries.set(this.threadId,this.row);
                bump('placement-search-begin'); emit('placement-search-begin',this.row);
            },
            onLeave(result) {
                if (!this.row) return;
                bump('placement-search-end'); emit('placement-search-end', {...this.row,
                    result:result.toUInt32(), after:words(this.output,2), restoredMode:this.fine.add(0xd4).readU32()});
                if (this.previous) placementQueries.set(this.threadId,this.previous);
                else placementQueries.delete(this.threadId);
            }
        });
        hook(0x1492b0, {
            onEnter(args) {
                this.row = placementQueries.get(this.threadId);
                if (!this.row) return;
                this.visit = {point:ints(args[0],2), mask:args[1].readU32(), cls:args[2].toUInt32()};
            },
            onLeave(result) {
                if (this.visit) this.row.visits.push({...this.visit, result:result.toUInt32()});
            }
        });
        hook(0x6803f0, {
            onEnter(args) {
                const native = positionNatives.get(this.threadId);
                if (!native || native.name !== 'SetUnitPosition') return;
                this.unit = this.context.ecx;
                this.row = {case:positionCase, flags:args[0].toUInt32(), before:forcedPositionState(this.unit)};
                bump('forced-position-stop-begin'); emit('forced-position-stop-begin',this.row);
            },
            onLeave() {
                if (!this.row) return;
                bump('forced-position-stop-end');
                emit('forced-position-stop-end',{...this.row, after:forcedPositionState(this.unit)});
            }
        });
        for (const [name, rva] of [['GetUnitX',0x204100], ['GetUnitY',0x204140],
                                  ['SetUnitX',0x215900], ['SetUnitY',0x215960], ['SetUnitPosition',0x2155c0],
                                  ['CreateUnit',0x1fc930]]) {
            hook(rva, {
                onEnter(args) {
                    this.previous = positionNatives.get(this.threadId);
                    this.row = positionCase ? {case:positionCase, name, handle:args[0].toUInt32()} : null;
                    if (this.row && name.startsWith('Set')) this.row.input = name === 'SetUnitPosition'
                        ? [args[1].readU32(), args[2].readU32()] : args[1].readU32();
                    if (this.row && name==='CreateUnit') {
                        this.row.rawcode=args[1].toUInt32();
                        this.row.input=[args[2].readU32(),args[3].readU32(),args[4].readU32()];
                    }
                    positionNatives.set(this.threadId, this.row);
                },
                onLeave(result) {
                    if (this.row) {
                        if (this.row.name === 'SetUnitPosition' && this.row.unit)
                            this.row.after = forcedPositionState(ptr(this.row.unit));
                        bump('position-native');
                        if (counts['position-native'] <= config.samples)
                            emit('position-native', {...this.row, output:this.row.name.startsWith('Set') ? null : result.toUInt32()});
                    }
                    if (this.previous) positionNatives.set(this.threadId, this.previous);
                    else positionNatives.delete(this.threadId);
                }
            });
        }
        hook(0x1eef90, {
            onEnter() {
                const row = positionNatives.get(this.threadId);
                this.row = row && row.handle === this.context.ecx.toUInt32() ? row : null;
            },
            onLeave(result) {
                if (this.row) {
                    this.row.unit = result.toString();
                    this.row.rawcode = result.isNull() ? null : result.add(0x30).readU32();
                    if (this.row.name === 'SetUnitPosition' && !result.isNull())
                        this.row.before = forcedPositionState(result);
                }
            }
        });
        for (const [kind, rva] of [['query',0x058900], ['commit',0x05c200]]) {
            hook(rva, {
                onEnter(args) {
                    const native = positionNatives.get(this.threadId);
                    if (!native) return;
                    this.kind = kind; this.mover = positionMover(this.context.ecx);
                    const owner = base.add(0xd53a48).readPointer();
                    const clock = owner.add(this.mover.add(0x14).readU32() & 0x80000000 ? 0x68 : 0x14);
                    this.point = args[0];
                    this.row = {case:positionCase, native:native.name, unit:native.unit,
                        mover:this.mover.toString(), before:words(this.mover.add(0x70),8),
                        clock:words(clock.add(0x40),3), origin:words(base.add(0xd3c82c).readPointer().add(0x6c),2)};
                    if (kind === 'commit') Object.assign(this.row, {input:words(this.point,3), notify:args[1].toUInt32()});
                },
                onLeave() {
                    if (!this.row) return;
                    bump('position-' + this.kind);
                    if (positionCase==='movement_modes' && this.kind==='query') {
                        const region=this.mover.add(0x98).readPointer(),path=this.mover.add(0xa8).readPointer();
                        const box=ints(region.add(0x1c),4),owner=base.add(0xd53a48).readPointer();
                        const fine=owner.add(0x238).readPointer(),width=fine.add(0x3c).readU32(),height=fine.add(0x40).readU32();
                        if(box[1]<0 || box[0]<0 || box[1]>=width || box[0]>=height)throw new Error('mode own rectangle outside fine map');
                        const links=fine.add(0x78).readPointer(),chain=[];
                        let at=fine.add(0x28).readPointer().add((box[0]*width+box[1])*4).readU32()&0xffffff,n=0;
                        while(at!==0xffffff && n++<512) {
                            const link=links.add(at*8),head=link.readU32(),kind=head>>>24,payload=link.add(4).readPointer();
                            if((kind===0 || kind===1) && payload.add(0x30).readPointer().equals(this.mover))
                                chain.push({kind,rectangle:ints(payload.add(0x1c),4),category:payload.add(0x34).readU32(),
                                    live:payload.add(0x38).readU32(),references:payload.add(0x3c).readU32(),flags:payload.add(0x40).readU32()});
                            at=head&0xffffff;
                        }
                        if(at!==0xffffff)throw new Error('mode own chain truncated');
                        emit('mode-spatial-state',{native:this.row.native,box,category:region.add(0x34).readU32(),
                            live:region.add(0x38).readU32(),references:region.add(0x3c).readU32(),flags:region.add(0x40).readU32(),
                            pathFlags:path.isNull()?null:path.add(0x88).readU32(),pathMask:path.isNull()?null:path.add(0x9c).readU32(),chain});
                    }
                    if (counts['position-' + this.kind] <= config.samples)
                        emit('position-' + this.kind, {...this.row, after:words(this.mover.add(0x70),8),
                            output:this.kind === 'query' ? words(this.point,3) : null});
                }
            });
        }
    }
    if (config.motionEvents) {
        // 15ff40 thiscall(cap*), RET4. Lower caps integrate old velocity before clamping it.
        hook(0x15ff40, {
            onEnter(args) {
                this.mover = this.context.ecx;
                if (!speedCase) return;
                const words = (p, n) => Array.from({length:n}, (_,i) => p.add(i*4).readU32());
                const owner = base.add(0xd53a48).readPointer();
                const clock = owner.add(this.mover.add(0x14).readU32() & 0x80000000 ? 0x68 : 0x14);
                this.row = {case:speedCase, mover:this.mover.toString(), value:args[0].readU32(),
                    before:words(this.mover.add(0x70),8), clock:words(clock.add(0x40),3),
                    fineFlagsBefore:this.mover.add(0x98).readPointer().add(0x40).readU32()};
            },
            onLeave() {
                if (!this.row) return;
                bump('speed-cap-change');
                const words = (p, n) => Array.from({length:n}, (_,i) => p.add(i*4).readU32());
                emit('speed-cap-change', {...this.row, after:words(this.mover.add(0x70),8),
                    fineFlagsAfter:this.mover.add(0x98).readPointer().add(0x40).readU32()});
            }
        });
        hook(0x48f410, {
            onEnter(args) {
                this.output = args[0];
                this.row = speedCase ? {case:speedCase, unit:this.context.ecx.toString()} : null;
            },
            onLeave() {
                if (!this.row) return;
                bump('speed-flat-maximum');
                emit('speed-flat-maximum', {...this.row, output:this.output.readU32()});
            }
        });
        hook(0x569830, {
            onEnter(args) {
                this.output = args[0];
                this.row = speedCase ? {case:speedCase, ability:this.context.ecx.toString(),
                    authored:this.context.ecx.add(0x88).readU32()} : null;
            },
            onLeave() {
                if (!this.row) return;
                bump('speed-flat-bonus');
                emit('speed-flat-bonus', {...this.row, output:this.output.readU32()});
            }
        });
        hook(0x5fc900, {
            onEnter(args) {
                this.output = args[0];
                this.row = speedCase ? {case:speedCase, ability:this.context.ecx.toString(),
                    base:this.context.ecx.add(0x70).readU32(), multiplier:this.context.ecx.add(0x78).readU32()} : null;
            },
            onLeave() {
                if (!this.row) return;
                bump('speed-composition');
                emit('speed-composition', {...this.row, output:this.output.readU32()});
            }
        });
        const speedNatives = new Map();
        for (const [name, rva] of [['GetUnitMoveSpeed',0x203d30],
                                  ['GetUnitDefaultMoveSpeed',0x203a90], ['SetUnitMoveSpeed',0x2154e0]]) {
            hook(rva, {
                onEnter(args) {
                    this.previous = speedNatives.get(this.threadId);
                    this.row = speedCase ? {case:speedCase, name, handle:args[0].toUInt32()} : null;
                    if (this.row && name === 'SetUnitMoveSpeed') this.row.input = args[1].readU32();
                    speedNatives.set(this.threadId, this.row);
                },
                onLeave(result) {
                    if (this.row) {
                        bump('speed-native');
                        if (counts['speed-native'] <= config.samples)
                            emit('speed-native', {...this.row,
                                output:this.row.name === 'SetUnitMoveSpeed' ? null : result.toUInt32(),
                                bounds:ints(base.add(0xd709b8),8).map(v=>v>>>0)});
                    }
                    if (this.previous) speedNatives.set(this.threadId, this.previous);
                    else speedNatives.delete(this.threadId);
                }
            });
        }
        hook(0x1eef90, {
            onEnter() {
                const row = speedNatives.get(this.threadId);
                this.row = row && row.handle === this.context.ecx.toUInt32() ? row : null;
            },
            onLeave(result) {
                if (this.row) {
                    this.row.unit = result.toString();
                    this.row.rawcode = result.isNull() ? null : result.add(0x30).readU32();
                    if (this.row.name === 'SetUnitPosition' && !result.isNull())
                        this.row.before = forcedPositionState(result);
                }
            }
        });
        hook(0x05c5c0, {
            onEnter(args) {
                this.bridge = this.context.ecx;
                this.row = speedCase ? {case:speedCase, input:args[0].readU32(),
                    rawcode:this.bridge.sub(0x164).add(0x30).readU32(), identity:ints(this.bridge.add(8),2),
                    globalCap:base.add(0xd3c82c).readPointer().add(0x80).readU32()} : null;
            },
            onLeave() {
                if (!this.row) return;
                bump('speed-publication');
                if (counts['speed-publication'] > config.samples) return;
                const id = this.row.identity[0] >>> 0;
                const registry = base.add(0xd68610).readPointer(), alternate = (id & 0x80000000) !== 0;
                const index = id & 0x7fffffff, limit = registry.add(alternate ? 0x3c : 0x1c).readU32();
                if (index >= limit) throw new Error('speed bridge identity outside registry');
                const slot = registry.add(alternate ? 0x2c : 0xc).readPointer().add(index*8);
                if (slot.readS32() !== -2) throw new Error('speed bridge identity is not live');
                const mover = slot.add(4).readPointer();
                if (mover.add(0x18).readS32() !== this.row.identity[1]) throw new Error('speed bridge epoch differs');
                emit('speed-publication', {...this.row, mover:mover.toString(),
                    limit:mover.add(0x88).readU32(), increment:mover.add(0xb4).readU32()});
            }
        });
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
    /* Read the same registered group used by1689d0 without calling any game
     * routine. Raw native inputs and owner words permit exact C replay. */
    const retryInput = (path, source) => {
        const mover=base.add(0xd53a8c).readPointer(), id=mover.add(0x9c).readU32();
        const generation=mover.add(0xa0).readU32(), registry=base.add(0xd68610).readPointer();
        const alternate=!!(id&0x80000000), index=id&0x7fffffff;
        if (index>=registry.add(alternate?0x3c:0x1c).readU32()) throw new Error('retry group outside registry');
        const group=registry.add(alternate?0x2c:0xc).readPointer().add(index*8+4).readPointer();
        if (group.isNull() || group.add(0x14).readU32()!==id || group.add(0x18).readU32()!==generation)
            throw new Error('retry group identity mismatch');
        return {nativeSource:ints(source,2).map(v=>v>>>0),
            nativeGoal:ints(path.add(0x24),2).map(v=>v>>>0),
            members:group.add(0x38).readU32(), group:group.toString(), mover:mover.toString(),
            ownerBefore:ints(base.add(0xd53a48).readPointer(),2).map(v=>v>>>0)};
    };
    hook(0x1689d0, {
        onEnter(args) {
            this.path = this.context.ecx;
            this.row = {path: this.path.toString(), position: [args[0].readFloat(), args[0].add(4).readFloat()],
                adjusted: [this.path.add(0x24).readFloat(), this.path.add(0x28).readFloat()],
                thresholdSquared: base.add(0xd54190).readFloat(), ...retryInput(this.path,args[0])};
        },
        onLeave() {
            bump('retry-init');
            if (counts['retry-init'] <= config.samples)
                emit('retry-init', {...this.row, count: this.path.add(0x98).readU32(),
                    ownerAfter:ints(base.add(0xd53a48).readPointer(),2).map(v=>v>>>0)});
        }
    });
    hook(0x167290, {
        onEnter(args) {
            this.path = this.context.ecx;
            this.before = this.path.add(0x98).readU32();
            this.row={...retryInput(this.path,args[0]),target:ints(this.path.add(0xa4),2).map(v=>v>>>0)};
        },
        onLeave(ret) {
            bump('retry-result');
            if (counts['retry-result'] <= config.samples)
                emit('retry-result', {...this.row,path: this.path.toString(), before: this.before,
                    after: this.path.add(0x98).readU32(), result: ret.toInt32(),
                    ownerAfter:ints(base.add(0xd53a48).readPointer(),2).map(v=>v>>>0),
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
    const waitingSteps = new Map();
    if (config.yieldEvents) {
        let waitSerial = 0;
        hook(0x16fbd0, {
            onEnter(args) {
                this.mover=this.context.ecx; this.path=this.mover.add(0xa8).readPointer();
                this.args=Array.from({length:7},(_,i)=>args[i]);
                const raw=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
                this.row={mover:this.mover.toString(),path:this.path.toString(),
                    source:raw(args[0],2),destination:raw(args[1],2),
                    before:this.path.isNull()?null:raw(this.path.add(0x74),10),
                    counter:base.add(0xd53a48).readPointer().add(0x538).readU32()};
            },
            onLeave() {
                const raw=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
                const row=this.row;
                row.sourceAfter=raw(this.args[0],2); row.destinationAfter=raw(this.args[1],2);
                row.outputs=this.args.slice(2).map(p=>p.readU32());
                row.after=this.path.isNull()?null:raw(this.path.add(0x74),10);
                bump('route-step');
                if(counts['route-step']<=config.samples) emit('route-step',row);
            }
        });
        const raw = (p,n) => Array.from({length:n},(_,i)=>p.add(i*4).readU32());
        hook(0x16fbd0, {
            onEnter(args) {
                this.row = null;
                const mover = this.context.ecx, path = mover.add(0xa8).readPointer();
                if (path.isNull() || !path.add(0x94).readU32()) return;
                this.source=args[0]; this.destination=args[1]; this.speed=args[2]; this.heading=args[3];
                this.arrived=args[4]; this.held=args[5]; this.changed=args[6];
                this.row = {sequence:++waitSerial, mover:mover.toString(), path:path.toString(),
                    source:raw(this.source,2), destination:raw(this.destination,2),
                    speed:this.speed.readU32(), heading:this.heading.readU32(),
                    parameters:raw(mover.add(0xb0),4), before:path.add(0x94).readU32(), gate:null,
                    query:args[7].toUInt32(), refresh:args[8].toUInt32(),
                    counter:base.add(0xd53a48).readPointer().add(0x538).readU32()};
                if (waitingSteps.has(this.threadId)) throw new Error('Nested waiting Mover_StepRoute');
                waitingSteps.set(this.threadId,this.row);
            },
            onLeave() {
                if (!this.row) return;
                waitingSteps.delete(this.threadId);
                const row=this.row;
                row.after=ptr(row.path).add(0x94).readU32();
                row.nextSpeed=this.speed.readU32(); row.nextHeading=this.heading.readU32();
                row.arrived=this.arrived.readU32(); row.held=this.held.readU32(); row.changed=this.changed.readU32();
                row.sourceAfter=raw(this.source,2); row.destinationAfter=raw(this.destination,2);
                bump('waiting-step');
                if (counts['waiting-step']<=config.samples) emit('waiting-step',row);
            }
        });
        const decisions = new Map();
        const words = (p,n) => Array.from({length:n},(_,i)=>p.add(i*4).readU32());
        const snapshot = mover => {
            if (mover.isNull()) return null;
            const path=mover.add(0xa8).readPointer();
            return {mover:mover.toString(),identity:words(mover.add(0x14),2),
                velocity:words(mover.add(0x80),2),path:path.toString(),
                player:(path.add(0x88).readU32()>>>16)&15,
                before:{delay:path.add(0x94).readU32(),blocker:words(path.add(0xa8),2)}};
        };
        hook(0x168360, {
            onEnter(args) {
                const vector=args[0], count=vector.add(0x1c).readU32();
                if (count>32) throw new Error('Yield candidate capacity exceeded');
                const current=base.add(0xd53a8c).readPointer();
                this.row={sequence:++serial,self:snapshot(current),candidates:[],groups:{},blocked:{}};
                const entries=vector.add(0xc).readPointer();
                for(let i=0;i<count;i++) this.row.candidates.push(snapshot(entries.add(i*4).readPointer()));
                decisions.set(this.threadId,this.row);
            },
            onLeave() {
                const row=this.row;
                for(const actor of [row.self,...row.candidates]) if(actor) {
                    const path=ptr(actor.path);
                    actor.after={delay:path.add(0x94).readU32(),blocker:words(path.add(0xa8),2)};
                }
                bump('yield-decision');
                if(counts['yield-decision']<=config.samples) emit('yield-decision',row);
                decisions.delete(this.threadId);
            }
        });
        hook(0x1702a0, {
            onEnter() {this.actor=this.context.ecx.toString();},
            onLeave(ret) {
                const row=decisions.get(this.threadId);
                if(row) row.groups[this.actor]=ret.isNull()?null:{pointer:ret.toString(),flags:ret.add(0x80).readU32()};
            }
        });
        hook(0x1680f0, {
            onEnter() {this.path=this.context.ecx.toString();},
            onLeave(ret) {
                const row=decisions.get(this.threadId);
                if(row) (row.blocked[this.path]||(row.blocked[this.path]=[])).push(!ret.isNull());
            }
        });
    }
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
        onEnter(args) {
            this.path = this.context.ecx; this.delay = this.path.add(0x94).readU32();
            this.disabled = !!(this.path.add(0x88).readU32() & 0x100000);
            this.waiting=waitingSteps.get(this.threadId);
            this.waitSource=args[0]; this.waitDestination=args[1];
            if (this.waiting) this.gate={source:ints(args[0],2).map(v=>v>>>0),
                destination:ints(args[1],2).map(v=>v>>>0),before:this.delay,disabled:this.disabled};
        },
        onLeave(ret) {
            if (this.waiting) {
                if (this.waiting.path!==this.path.toString()) throw new Error('Waiting caller path changed');
                this.waiting.gate={...this.gate,after:this.path.add(0x94).readU32(),result:ret.toUInt32(),
                    sourceAfter:ints(this.waitSource,2).map(v=>v>>>0),
                    destinationAfter:ints(this.waitDestination,2).map(v=>v>>>0)};
            }
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
    hook(0x171340, {
        onEnter() {
            bump('mover-stop');
            this.observe = counts['mover-stop'] <= config.samples;
            if (!this.observe) return;
            this.mover = this.context.ecx;
            this.row = {mover:this.mover.toString(),
                before:Array.from({length:8}, (_,i) => this.mover.add(0x70+i*4).readU32()),
                requestedBefore:Array.from({length:2}, (_,i) => this.mover.add(0xc0+i*4).readU32()),
                caller:this.returnAddress.sub(base).toString(),
                stack:Thread.backtrace(this.context, Backtracer.ACCURATE)
                    .filter(p => p.compare(base) >= 0 && p.compare(base.add(config.imageSize)) < 0)
                    .map(p => p.sub(base).toString())};
        },
        onLeave() {
            if (!this.observe) return;
            emit('mover-stop', {...this.row,
                after:Array.from({length:8}, (_,i) => this.mover.add(0x70+i*4).readU32()),
                requestedAfter:Array.from({length:2}, (_,i) => this.mover.add(0xc0+i*4).readU32())});
        }
    });
    for(const [name,rva] of [['chaos-enabled',0x4d8dc0],['chaos-research',0x4d8d40],['chaos-commit',0x4d8850],['unit-type-rebind',0x670950],['move-task-reissue',0x5fd270],['move-point-task',0x5ffb60],['radius-publication',0x15fef0]])hook(rva,{onEnter(){
        const owner=base.add(0xd53a48).readPointer();
        emit('chaos-clock',{phase:name,context:this.context.ecx.toString(),primary:ints(owner.add(0x54),4).map(v=>v>>>0),secondary:ints(owner.add(0xa8),4).map(v=>v>>>0),counter:owner.add(0x538).readU32()});
    }});
    hook(0x053630,{
        onEnter(args){
            this.clock=this.context.ecx;this.request=args[0];
            this.observe=pairScenario && [0x3dcccccc,0x3dcccccd,0x3cf5c28e,0x3cf5c28f,0x3cf5c290].includes(this.request.add(8).readU32());
            if(!this.observe)return;
            const owner=base.add(0xd53a48).readPointer();
            this.row={request:ints(this.request,7).map(v=>v>>>0),timerClock:ints(this.clock.add(0x40),4).map(v=>v>>>0),primary:ints(owner.add(0x54),4).map(v=>v>>>0),counter:owner.add(0x538).readU32()};
        },
        onLeave(){if(this.observe)emit('public-timer-rearm',{...this.row,after:ints(this.request,7).map(v=>v>>>0)});}
    });
    let captainScenario=false;
    let captainRangeDepth=0;
    const captainRanges=new Set();
    const captainClock=()=>{const o=base.add(0xd53a48).readPointer();return {clock:ints(o.add(0x54),4).map(v=>v>>>0),counter:o.add(0x538).readU32()};};
    const captainRegion=wrapper=>{
        const id=wrapper.add(8).readU32(),epoch=wrapper.add(12).readU32(),alt=(id&0x80000000)!==0;
        if(id===0xffffffff)throw new Error('captain range null identity');
        const reg=base.add(0xd68610).readPointer(),index=id&0x7fffffff;
        if(index>=reg.add(alt?0x3c:0x1c).readU32())throw new Error('captain range out of bounds');
        const slot=reg.add(alt?0x2c:0xc).readPointer().add(index*8);
        if(slot.readS32()!==-2)throw new Error('captain range not live');
        const p=slot.add(4).readPointer();
        if(p.isNull()||p.add(0x18).readU32()!==epoch)throw new Error('captain range stale identity');
        return p;
    };
    const captainRangeState=p=>({region:p.toString(),identity:ints(p.add(0x14),2),radius:p.add(0x50).readU32(),flags:p.add(0x54).readU32(),eventCode:p.add(0x48).readU32(),policy:p.add(0x4c).readU32(),period:p.add(0x44).readU32(),request:p.add(0x1c).readPointer().isNull()?null:ints(p.add(0x1c).readPointer(),7),owner:p.add(0x40).readPointer().toString(),count:p.add(0x74).readU32()});
    hook(0x9d0650,{onEnter(args){this.observe=captainScenario;if(!this.observe)return;captainRangeDepth++;this.p=this.context.ecx;emit('captain-roster-ranges-begin',{captain:this.p.toString(),counts:ints(this.p.add(0xb8),6),delta:args[1].toInt32(),globals:[0xd77f7c,0xd3c7f0,0xd3c7c8,0xd3c7dc,0xd77f84].map(rva=>base.add(rva).readU32()),...captainClock()});},onLeave(){if(this.observe){captainRangeDepth--;emit('captain-roster-ranges-end',{counts:ints(this.p.add(0xb8),6),...captainClock()});}}});
    hook(0x063560,{onEnter(args){this.observe=captainRangeDepth>0;if(!this.observe)return;this.p=captainRegion(this.context.ecx);captainRanges.add(this.p.toString());this.row={requested:args[0].readU32(),before:captainRangeState(this.p),caller:this.returnAddress.sub(base).toUInt32(),...captainClock()};},onLeave(){if(this.observe)emit('captain-range-publish',{...this.row,after:captainRangeState(this.p)});}});
    hook(0x15f210,{onEnter(args){this.p=this.context.ecx;this.observe=captainScenario&&captainRanges.has(this.p.toString());if(!this.observe)return;this.row={before:captainRangeState(this.p),flag:args[0].toUInt32(),stack:Thread.backtrace(this.context,Backtracer.ACCURATE).map(v=>v.sub(base).toString()),...captainClock()};},onLeave(){if(this.observe)emit('captain-range-update',{...this.row,after:captainRangeState(this.p)});}});
    hook(0x9d9020,{onEnter(args){this.observe=captainScenario;if(!this.observe)return;this.p=this.context.ecx;this.row={captain:this.p.toString(),counts:ints(this.p.add(0xb8),6),packet:ints(args[0],6),stack:Thread.backtrace(this.context,Backtracer.ACCURATE).map(v=>v.sub(base).toString()),...captainClock()};emit('captain-range-enter-begin',this.row);},onLeave(){if(this.observe)emit('captain-range-enter-end',{...this.row,countsAfter:ints(this.p.add(0xb8),6)});}});
    if(config.captainMembershipEvents) {
        for(const [name,rva,mode] of [['outer-leave',0x9d8d50,false],['member-notify',0x9d8eb0,true]])
            hook(rva,{onEnter(args){this.observe=captainScenario;if(!this.observe)return;this.p=this.context.ecx;
                const packet=args[mode ? 1 : 0];
                this.row={name,captain:this.p.toString(),mode:mode ? args[0].toInt32() : null,
                    counts:ints(this.p.add(0xb8),6),packet:ints(packet,6),
                    unit:packet.add(0x10).readPointer().toString(),...captainClock()};
                emit('captain-membership-begin',this.row);
            },onLeave(){if(this.observe)emit('captain-membership-end',{...this.row,countsAfter:ints(this.p.add(0xb8),6)});}});
        hook(0x9d05e0,{onEnter(args){this.observe=captainScenario;if(!this.observe)return;this.p=this.context.ecx;
            this.row={captain:this.p.toString(),mode:args[0].toInt32(),unit:args[1].toString(),delta:args[2].toInt32(),
                counts:ints(this.p.add(0xb8),6),...captainClock()};
        },onLeave(result){if(this.observe)emit('captain-membership-counter',{...this.row,result:result.toUInt32(),countsAfter:ints(this.p.add(0xb8),6)});}});
        hook(0x9d87d0,{onEnter(args){if(!captainScenario)return;
            emit('captain-member-reissue',{captain:this.context.ecx.toString(),unit:args[0].toString(),
                order:args[1].toUInt32(),target:args[2].toString(),point:[args[3].readU32(),args[4].readU32()],...captainClock()});
        }});
    }
    for(const [name,rva] of [['start-wrapper',0x215d90],['load',0x9c0140],['compile',0x9cbc00],['create',0x9b9630],['init',0x9bc0f0],['add',0x9b79b0],['attack',0x9b88f0]])hook(rva,{onEnter(args){this.observe=captainScenario;if(this.observe)emit('captain-native-begin',{name,words:[args[0].toUInt32(),args[1].toUInt32()]});},onLeave(retval){if(this.observe)emit('captain-native-end',{name,result:retval.toUInt32()});}});
    hook(0x9d16c0,{onEnter(){if(captainScenario)emit('captain-update',{captain:this.context.ecx.toString(),state:this.context.ecx.add(0x64).readU32(),flags:this.context.ecx.add(0x6c).readU32(),order:this.context.ecx.add(0x70).readU32(),counts:ints(this.context.ecx.add(0xbc),5).map(v=>v>>>0),target:ints(this.context.ecx.add(0x114),2),roster:ints(this.context.ecx.add(0xac),2)});}});
    hook(0x9d1040,{onEnter(args){this.observe=captainScenario;if(this.observe){this.wrapper=args[1];this.count=args[3];this.index=args[2];emit('captain-prepare-begin',{unit:this.context.ecx.toString(),target:this.context.edx.toString(),point:args[0].isNull()?null:ints(args[0],2).map(v=>v>>>0),index:this.index.readU32(),count:this.count.readU32(),policy:args[4].toUInt32(),bindShared:args[5].toUInt32(),sharedWrapper:args[6].toString(),sharedIdentity:args[6].isNull()?null:ints(args[6].add(8),2)});}},onLeave(){if(this.observe){const p=this.wrapper.readPointer();emit('captain-prepare-end',{wrapper:p.toString(),identity:p.isNull()?null:ints(p.add(8),2),index:this.index.readU32(),count:this.count.readU32()});}}});
    hook(0x16d8b0,{onEnter(args){if(captainScenario)emit('captain-shared-request',{request:this.context.ecx.toString(),shared:args[0].toString()});}});
    hook(0x16d890,{onEnter(args){if(captainScenario)emit('captain-shared-group',{group:this.context.ecx.toString(),shared:args[0].toString()});}});
    hook(0x16c220,{onEnter(){this.observe=captainScenario;if(this.observe){this.p=this.context.ecx;this.before=ints(this.p.add(0x1c),4).map(v=>v>>>0);}},onLeave(){if(this.observe)emit('captain-shared-publish',{identity:ints(this.p.add(0x14),2),before:this.before,after:ints(this.p.add(0x1c),4).map(v=>v>>>0)});}});
    const radiusGroupSnapshot=group=>{
        const count=group.add(0x38).readU32(),data=group.add(0x28).readPointer(),shared=group.add(0x7c).readPointer();
        if(count>12)throw new Error('radius observer group extent differs');
        return {group:group.toString(),identity:ints(group.add(0x14),2),shared:shared.toString(),
            sharedIdentity:shared.isNull()?null:ints(shared.add(0x14),2),
            sharedRadius:shared.isNull()?null:shared.add(0x28).readU32(),
            counter:base.add(0xd53a48).readPointer().add(0x538).readU32(),
            members:Array.from({length:count},(_,i)=>{
                const p=data.add(i*0x2c),mover=p.add(0x14).readPointer();
                return {identity:ints(p,2),resolved:mover.toString(),radius:mover.isNull()?null:mover.add(0x90).readU32(),owner:mover.isNull()?null:ints(mover.add(0x9c),2)};
            })};
    };
    hook(0x16e1f0,{onEnter(){this.observe=pairScenario;if(this.observe){this.group=this.context.ecx;this.before=radiusGroupSnapshot(this.group);}},onLeave(){if(this.observe)emit('group-radius-accumulate',{before:this.before,after:radiusGroupSnapshot(this.group)});}});
    hook(0x05b970,{onEnter(args){
        this.observe=pairScenario;
        if(this.observe){
            const host=base.add(0xd3c82c).readPointer();
            emit('point-bound-admission',{input:[args[0].readU32(),args[1].readU32()],
                bounds:[host.add(0x6c).readU32(),host.add(0x70).readU32(),host.add(0x74).readU32(),host.add(0x78).readU32()],
                cell:base.add(0xd3c7a8).readU32(),margin:base.add(0xd3c754).readU32()});
        }
    }});
    hook(0x16c940,{onEnter(args){this.observe=pairScenario;if(this.observe){this.group=this.context.ecx;this.out=args[0];this.before=radiusGroupSnapshot(this.group);}},onLeave(){if(this.observe)emit('group-routing-radius',{...this.before,result:this.out.readU32()});}});
    const snapshotPairGroup = group => {
        const count = group.add(0x38).readU32(), data = group.add(0x28).readPointer();
        if (count > 12) throw new Error('Public pair observer found an oversized group');
        return {group:group.toString(),identity:ints(group.add(0x14),2).map(v => v>>>0),
            flags:group.add(0x80).readU32(),age:group.add(0x5c).readU32(),
            completion:group.add(0x60).readU32(),formation:ints(group.add(0x54),2).map(v => v>>>0),
            members:Array.from({length:count},(_,i) => {const p=data.add(i*0x2c), mover=p.add(0x14).readPointer();
                return {mover:mover.toString(),row:ints(p,11).map(v => v>>>0),pose:ints(mover.add(0x70),8).map(v => v>>>0),
                    moverFlags:mover.add(0xd8).readU32()};})};
    };
    if(config.profileEvents) {
        hook(0x680430,{onEnter(){this.observe=formationRankScenario;if(this.observe)this.rawcode=this.context.ecx.toUInt32();},
            onLeave(result){if(this.observe){bump('formation-authored-rank');emit('formation-authored-rank',{rawcode:this.rawcode,rank:result.toUInt32()});}}});
        hook(0x171070,{onEnter(args){this.observe=formationRankScenario;if(this.observe){this.mover=this.context.ecx;
            this.row={mover:this.mover.toString(),identity:ints(this.mover.add(0x14),2),input:args[0].toUInt32(),before:this.mover.add(0xd8).readU32()};}},
            onLeave(){if(this.observe){bump('formation-rank-set');emit('formation-rank-set',{...this.row,after:this.mover.add(0xd8).readU32()});}}});
        hook(0x16cb80,{onEnter(args){this.observe=formationRankScenario;if(this.observe){this.buckets=args[0];this.group=this.context.ecx;this.before=snapshotPairGroup(this.group);}},
            onLeave(result){if(this.observe){const buckets=[];
                for(let rank=0;rank<16;rank++){const p=this.buckets.add(rank*0xa4),count=p.readU32();
                    if(count>12)throw new Error('Formation rank bucket extent differs');
                    if(count)buckets.push({rank,count,members:ints(p.add(4),count),projected:ints(p.add(0x34),2*count).map(v=>v>>>0)});
                }
                bump('formation-rank-buckets');emit('formation-rank-buckets',{...this.before,heading:this.group.add(0x70).readU32(),ranks:result.toUInt32(),buckets});
            }}});
        hook(0x169e10,{onEnter(){this.observe=formationRankScenario;if(this.observe){this.group=this.context.ecx;
            bump('formation-rank-center');emit('formation-rank-center',snapshotPairGroup(this.group));}}});
        hook(0x16bb40,{onEnter(args){this.observe=formationRankScenario;if(this.observe){this.group=this.context.ecx;this.out=args[0];}},
            onLeave(){if(this.observe){bump('formation-rank-mean');emit('formation-rank-mean',{group:this.group.toString(),mean:ints(this.out,2).map(v=>v>>>0)});}}});
        hook(0x071340,{onEnter(args){this.observe=formationRankScenario;if(this.observe){this.angle=this.context.ecx.readU32();this.sine=this.context.edx;this.cosine=args[0];this.caller=this.returnAddress.sub(base).toUInt32();}},
            onLeave(){if(this.observe){bump('formation-rank-trig');emit('formation-rank-trig',{angle:this.angle,caller:this.caller,sine:this.sine.readU32(),cosine:this.cosine.readU32()});}}});
        hook(0x16a5b0,{onEnter(){this.observe=formationRankScenario;if(this.observe){this.group=this.context.ecx;this.before=snapshotPairGroup(this.group);}},
            onLeave(){if(this.observe){bump('formation-rank-layout');emit('formation-rank-layout',{before:this.before,after:snapshotPairGroup(this.group),heading:this.group.add(0x70).readU32()});}}});
    }
    for (const [phase,rva] of [['decide',0x16c250],['commit',0x16c570]]) hook(rva,{onEnter() {
        this.group = pairScenario ? this.context.ecx : null;
        if (this.group) emit('pair-group-phase-begin',{phase,...snapshotPairGroup(this.group)});
        if(this.group && phase==='decide') {
            const path=this.group.add(0x3c).readPointer();
            emit('group-footprint-state',{...radiusGroupSnapshot(this.group),path:path.toString(),pathIdentity:ints(path.add(0x14),2),footprint:path.add(0xb4).readU32()});
            const count=this.group.add(0x38).readU32(),data=this.group.add(0x28).readPointer();
            for(let i=0;i<count;i++) {
                const mover=data.add(i*0x2c+0x14).readPointer(),fine=mover.add(0x98).readPointer(),proximity=mover.add(0x94).readPointer();
                emit('mover-radius-state',{mover:mover.toString(),identity:ints(mover.add(0x14),2),radius:mover.add(0x90).readU32(),fine:fine.toString(),fineRect:ints(fine.add(0x1c),4),fineFlags:fine.add(0x40).readU32(),proximity:proximity.toString(),proximityRect:ints(proximity.add(0x1c),4),group:ints(mover.add(0x9c),2),pose:ints(mover.add(0x70),8).map(v=>v>>>0),counter:base.add(0xd53a48).readPointer().add(0x538).readU32()});
            }
        }
        if (this.group && resizeScenario && phase === 'decide') {
            const target=resizeTargets.get(this.group.toString());
            if (target && !target.isNull()) {
                bump('resize-target-state');
                emit('resize-target-state',{group:this.group.toString(),target:target.toString(),groupHandle:ints(this.group.add(0x40),2),moverHandle:ints(target.add(0x14),2),radius:target.add(0x90).readU32(),pose:ints(target.add(0x70),8).map(v=>v>>>0),counter:base.add(0xd53a48).readPointer().add(0x538).readU32()});
            }
        }
    },onLeave() {if (this.group) emit('pair-group-phase-end',{phase,...snapshotPairGroup(this.group)});}});
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
        if (resizeScenario) resizeTargets.set(this.context.ecx.toString(),args[0]);
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
    const schedulerSnapshot = (bucket, path) => {
        const owner=base.add(0xd53a48).readPointer();
        const count=bucket.add(0x10).readU32(), queue=[];
        if(count>512)throw new Error('Scheduler observer queue exceeds bounded fixture size');
        let entry=bucket.add(0x14).readPointer();
        for(let i=0;i<count;i++) {
            if(entry.isNull() || entry.equals(ptr('0xffffffff')))throw new Error('Truncated scheduler FIFO');
            queue.push(entry.toString());entry=entry.add(0x90).readPointer();
        }
        if(count && !entry.equals(ptr('0xffffffff')))throw new Error('Scheduler FIFO has an unexpected tail');
        return {counter:owner.add(0x538).readU32(),clock:ints(owner.add(0x54),4).map(v=>v>>>0),
            limit:bucket.add(4).readU32(),work:bucket.add(8).readU32(),countdown:bucket.add(12).readU32(),
            queue,head:bucket.add(0x14).readPointer().toString(),tail:bucket.add(0x18).readPointer().toString(),
            links:path ? ints(path.add(0x8c),2).map(v=>v>>>0) : null,
            times:path ? ints(path.add(0x7c),2).map(v=>v>>>0) : null,
            flags:path ? path.add(0x88).readU32() : null};
    };
    if(config.schedulerEvents) {
        hook(0x680320,{onEnter(args){
            if(!schedulerMutationScenario && !moverRetirementScenario)return;
            const unit=this.context.ecx,bridge=unit.add(0x164);
            const id=bridge.add(8).readU32(),epoch=bridge.add(12).readU32();
            const registry=base.add(0xd68610).readPointer(),alternate=(id&0x80000000)!==0;
            const index=id&0x7fffffff,limit=registry.add(alternate?0x3c:0x1c).readU32();
            if(index>=limit)throw new Error('Mutation unit mover outside registry');
            const mover=registry.add(alternate?0x2c:0xc).readPointer().add(index*8+4).readPointer();
            if(mover.isNull() || mover.add(0x14).readU32()!==id || mover.add(0x18).readU32()!==epoch)
                throw new Error('Mutation unit mover identity stale');
            if(moverRetirementScenario && retirementActors.length<96 && !retirementUnits.has(unit.toString())) {
                const path=mover.add(0xa8).readPointer();
                retirementActors.push({unit:unit.toString(),unitIdentity:ints(unit.add(0xc),2),
                    moverIdentity:[id,epoch],path:path.toString(),pathIdentity:ints(path.add(0x14),2)});
                retirementUnits.add(unit.toString());
            }
            bump('scheduler-mutation-actor');emit('scheduler-mutation-actor',{
                unit:unit.toString(),mover:mover.toString(),path:mover.add(0xa8).readPointer().toString(),
                position:ints(mover.add(0x78),2),mode:args[1].toUInt32()});
        }});
        hook(0x167fa0,{onEnter(){
            this.bucket=this.context.ecx;
            this.offset=this.bucket.sub(base.add(0xd53a90)).toUInt32();
            this.observe=this.offset<16*0x70 && this.offset%0x1c===0 &&
                (this.bucket.add(8).readU32() || this.bucket.add(0x10).readU32());
            if(this.observe)this.before=schedulerSnapshot(this.bucket,null);
        },onLeave(){if(this.observe){
            bump('scheduler-update');
            if(counts['scheduler-update']<=config.samples)emit('scheduler-update',{
                bucketOffset:this.offset,before:this.before,after:schedulerSnapshot(this.bucket,null)});
        }}});
        hook(0x166e90,{onEnter(){
            this.path=this.context.ecx;
            this.offset=((this.path.add(0x88).readU32()>>>16)&15)*0x70+0x54;
            this.bucket=base.add(0xd53a90+this.offset);
            this.before=schedulerSnapshot(this.bucket,this.path);
        },onLeave(result){
            bump('scheduler-fine-request');
            if(counts['scheduler-fine-request']<=config.samples)emit('scheduler-fine-request',{
                path:this.path.toString(),bucketOffset:this.offset,result:result.toUInt32(),
                before:this.before,after:schedulerSnapshot(this.bucket,this.path)});
        }});
        hook(0x166c30,{onEnter(){
            this.path=this.context.ecx;
            const flags=this.path.add(0x88).readU32(), limit=this.path.add(0x86).readU16();
            this.offset=((flags>>>16)&15)*0x70+(limit<=400 ? 0x38 : flags&0x4000000 ? 0x1c : 0);
            this.bucket=base.add(0xd53a90+this.offset);
            this.before=schedulerSnapshot(this.bucket,this.path);
        },onLeave(result){
            bump('scheduler-acc-request');
            if(counts['scheduler-acc-request']<=config.samples)emit('scheduler-acc-request',{
                path:this.path.toString(),bucketOffset:this.offset,result:result.toUInt32(),
                before:this.before,after:schedulerSnapshot(this.bucket,this.path)});
        }});
        hook(0x1686a0,{onEnter(args){
            this.bucket=this.context.ecx;this.path=args[0];
            this.offset=this.bucket.sub(base.add(0xd53a90)).toUInt32();
            this.before=schedulerSnapshot(this.bucket,this.path);
        },onLeave(){
            bump('scheduler-unlink');
            if(counts['scheduler-unlink']<=config.samples)emit('scheduler-unlink',{
                path:this.path.toString(),bucketOffset:this.offset,before:this.before,
                after:schedulerSnapshot(this.bucket,this.path)});
        }});
    }
    hook(0x168310, {
        onEnter(args) {
            bump('scheduler-admission');
            this.bucket = this.context.ecx;
            this.path = args[0];
            this.request = active.get(this.threadId);
            this.before = ints(this.bucket.add(4), 4);
            this.schedulerBefore=config.schedulerEvents ? schedulerSnapshot(this.bucket,this.path) : null;
        },
        onLeave(ret) {
            if (counts['scheduler-admission'] <= config.samples)
                emit('scheduler-admission', {path: this.path.toString(),
                    request: this.request ? this.request.request : null,
                    bucketOffset: this.bucket.sub(base.add(0xd53a90)).toUInt32(),
                    before: this.before, result: ret.toInt32(),
                    after: ints(this.bucket.add(4), 4),
                    ...(config.schedulerEvents ? {stateBefore:this.schedulerBefore,stateAfter:schedulerSnapshot(this.bucket,this.path)} : {})});
        }
    });
    if (config.schedulerEvents) {
        for (const [entry, offset] of [[0x6cf5e0,0xf0],[0x6cfe00,0xd8],[0x6d30c0,0x78],[0x6d3190,0x78]]) {
            hook(entry, {onEnter() {
                bump('scheduler-nonunit-producer');
                emit('scheduler-nonunit-producer', {entry, object:this.context.ecx.toString(),
                    vtable:this.context.ecx.readPointer().sub(base).toUInt32(),
                    bridge:this.context.ecx.add(offset).toString(),caller:this.returnAddress.sub(base).toUInt32()});
            }});
        }
        hook(0x05c800, {onEnter(args) {
            if (args[0].toUInt32()!==15)return;
            bump('scheduler-class15-producer');
            emit('scheduler-class15-producer', {bridge:this.context.ecx.toString(),
                caller:this.returnAddress.sub(base).toUInt32(),value:15});
        }});
    }
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
    // Read-only scene45 geometry control, after the old actor is removed and
    // before the new public birth. Decode verified1489a0 static eligibility.
    function snapshotObliqueGeometry() {
        const owner = base.add(0xd53a48).readPointer();
        const map = owner.add(0x238).readPointer();
        const [width, height] = ints(map.add(0x3c), 2);
        const cells = map.add(0x28).readPointer(), links = map.add(0x78).readPointer();
        function runs(values) {
            const out = [];
            for (const value of values) {
                if (out.length && out[out.length-1][1] === value) out[out.length-1][0]++;
                else out.push([1,value]);
            }
            return out;
        }
        const terrain = [], objects = [];
        for (let i=0; i<width*height; i++) {
            const word = cells.add(i*4).readU32();
            terrain.push((word >>> 24) & 2);
            let at = word & 0xffffff, blocked = 0, visited = 0;
            while (at !== 0xffffff) {
                if (++visited > 10000) throw new Error('Oblique snapshot cyclic cell links');
                const link = links.add(at*8), head = link.readU32();
                const kind = head >>> 24;
                if (kind === 1) {
                    const object = link.add(4).readPointer();
                    const category = object.add(0x34).readU32(), occupancy = object.add(0x40).readU32();
                    if (object.add(0x38).readS32() !== -1 && (category & 0x01000000) &&
                        !(occupancy & 0xefffffff) && (category & 2)) blocked = 2;
                }
                at = head & 0xffffff;
            }
            objects.push(blocked);
        }
        const hierarchy = [];
        for (let level=0;level<4;level++) {
            const coarse = owner.add(0x23c+level*4).readPointer();
            const [w,h] = ints(coarse.add(0x3c),2), data = coarse.add(0x28).readPointer();
            const values = [];
            for (let i=0;i<w*h;i++) values.push(data.add(i*8+4).readU32() >>> 30);
            hierarchy.push({width:w,height:h,runs:runs(values)});
        }
        emit('oblique-geometry', {width,height,terrainRuns:runs(terrain),objectRuns:runs(objects),hierarchy});
    }
    function snapshotBlockerGeometry(marker) {
        const owner = base.add(0xd53a48).readPointer();
        if (owner.isNull()) throw new Error('Blocker snapshot without pathing owner');
        const fine = owner.add(0x238).readPointer(), [width,height] = ints(fine.add(0x3c),2);
        const size=config.blockerPatchSize||16;
        const minX=size===32 ? Math.floor((config.watchCell[0]-8)/16)*16 : config.watchCell[0]-8;
        const minY=size===32 ? Math.floor((config.watchCell[1]-8)/16)*16 : config.watchCell[1]-8;
        if (minX<0 || minY<0 || minX+size>width || minY+size>height)
            throw new Error('Blocker snapshot outside fine grid');
        const data=fine.add(0x28).readPointer(), links=fine.add(0x78).readPointer(), masks=[];
        for (let y=minY;y<minY+size;y++) for (let x=minX;x<minX+size;x++) {
            const word=data.add((y*width+x)*4).readU32(), seen=new Set();
            let blocked=(word>>>24)&(size===32 ? 0xc6 : 2), at=word&0xffffff, count=0;
            while (at!==0xffffff) {
                if (++count>4096) throw new Error('Blocker snapshot link chain did not terminate');
                const link=links.add(at*8), head=link.readU32(), kind=head>>>24;
                if (kind!==2) {
                    const object=link.add(4).readPointer(), key=object.toString();
                    const category=object.add(0x34).readU32();
                    if (object.add(0x38).readS32()!==-1 && (category&0x01000000) && !seen.has(key)) {
                        // 1489a0 visits before testing kind: a retired newer link
                        // suppresses older active history for this same region.
                        seen.add(key);
                        if (kind===1 && ((category&255)===0xc2 || (size===32 && (category&255)===4)) &&
                            !(object.add(0x40).readU32()&0xefffffff))
                            blocked|=size===32 ? category&0xc6 : 2;
                    }
                }
                at=head&0xffffff;
            }
            masks.push(blocked);
        }
        const hierarchy=[];
        for (let level=0;level<4;level++) {
            const map=owner.add(0x23c+level*4).readPointer(), [w,h]=ints(map.add(0x3c),2);
            const shift=level+1, box=[minX>>shift,minY>>shift,(minX+size-1)>>shift,(minY+size-1)>>shift];
            const cells=map.add(0x28).readPointer(), values=[];
            for(let y=box[1];y<=box[3];y++) for(let x=box[0];x<=box[2];x++) {
                if(x>=w || y>=h) throw new Error('Blocker snapshot outside hierarchy');
                const word=cells.add((y*w+x)*8+4).readU32();
                values.push([0,2,4,6].map(s=>(word>>>(30-s))&3));
            }
            hierarchy.push({level,box,values});
        }
        emit('blocker-geometry',{marker,width,height,box:[minX,minY,minX+size,minY+size],masks,hierarchy});
    }
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
            if (level===-1 && pairScenario) {
                const links=map.add(0x78).readPointer(), records=[];
                let at=word&0xffffff;
                while(at!==0xffffff && records.length<512) {
                    const link=links.add(at*8), head=link.readU32(), kind=head>>>24;
                    const payload=link.add(4).readPointer();
                    const record={index:at,kind,payload:payload.toString()};
                    if(kind===0 || kind===1) Object.assign(record,{rectangle:ints(payload.add(0x1c),4),
                        mover:payload.add(0x30).readPointer().toString(),category:payload.add(0x34).readU32(),
                        live:payload.add(0x38).readS32(),references:payload.add(0x3c).readU32(),flags:payload.add(0x40).readU32()});
                    records.push(record);at=head&0xffffff;
                }
                emit('cell-links',{marker,x,y,records,truncated:at!==0xffffff});
            }
        }
        emit('cell-snapshot', {marker, cells});
    }
    if (config.numericEvents) hook(0x216c70,{onEnter(args){
        if(pairScenario) emit('expression-timer-start',{handle:args[0].toUInt32(),
            timeout:args[1].readU32(),periodic:args[2].toUInt32(),handler:args[3].toUInt32()});
    }});
    // Preload's string-intern call receives the resolved C string on the stack.
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (config.randomEvents && value.startsWith('PATHRANDOM ')) {
            const match = /^PATHRANDOM case=([a-z0-9_]+) native=([A-Za-z0-9]+)$/.exec(value);
            if (match) randomCase = {case:match[1],native:match[2]};
            else if (value === 'PATHRANDOM done=all') randomCase = null;
            else throw new Error('Malformed random marker: '+value);
            emit('random-marker',{value});
        }
        if (config.numericEvents && value.startsWith('PATHNUM ')) {
            const match = /^PATHNUM case=([a-z0-9_]+) native=([A-Za-z0-9]+)$/.exec(value);
            if (match) numericCase = {case:match[1], native:match[2]};
            else if (/^PATHNUM done=/.test(value)) numericCase = null;
            else throw new Error('Malformed numeric marker: ' + value);
            emit('numeric-marker', {value});
        }
        if(config.schedulerEvents && value.startsWith('PATHRETIRE ')) {
            const match=/^PATHRETIRE label=([a-z_]+) actor=([0-9]+)$/.exec(value);
            if(!match)throw new Error('Malformed retirement marker');
            const actor=Number(match[2]),saved=retirementActors[actor];
            if(!saved)throw new Error('Retirement actor was not bound by public admission');
            const registry=base.add(0xd68610).readPointer();
            const resolve=(identity,offset)=>{
                const id=identity[0]>>>0,epoch=identity[1]>>>0,alternate=(id&0x80000000)!==0,index=id&0x7fffffff;
                if(index>=registry.add(alternate?0x3c:0x1c).readU32())return ptr(0);
                const slot=registry.add(alternate?0x2c:0xc).readPointer().add(index*8);
                if(slot.readS32()!==-2)return ptr(0);
                const object=slot.add(4).readPointer();
                if(object.isNull() || object.add(offset).readU32()!==id || object.add(offset+4).readU32()!==epoch)return ptr(0);
                return object;
            };
            const wrapper=resolve(saved.unitIdentity,0x14);
            const unit=wrapper.isNull() || wrapper.add(0xc).readU32()!==0x2b61676c || wrapper.add(0x20).readU32()!==0 ? ptr(0) : wrapper.add(0x54).readPointer();
            if(!unit.isNull() && unit.toString()!==saved.unit)throw new Error('Retirement agent wrapper resolved another payload');
            const mover=resolve(saved.moverIdentity,0x14),path=resolve(saved.pathIdentity,0x14);
            const row={value,actor,unitLive:!unit.isNull(),moverLive:!mover.isNull(),pathLive:!path.isNull()};
            if(!unit.isNull())Object.assign(row,{unitFlags:unit.add(0x5c).readU32(),
                orderHead:ints(unit.add(0x19c),2),orderCount:unit.add(0x1b4).readU32(),taskHead:ints(unit.add(0x174),2)});
            if(!mover.isNull()) {
                const identity=ints(mover.add(0x9c),2),group=resolve(identity,0x14);
                if(!group.isNull() && !saved.groupIdentity) {
                    saved.groupIdentity=identity;
                    const groupPath=group.add(0x3c).readPointer();
                    saved.groupPathIdentity=groupPath.isNull()?null:ints(groupPath.add(0x14),2);
                }
                Object.assign(row,{groupIdentity:identity,pose:ints(mover.add(0x78),2),velocity:ints(mover.add(0x80),2),
                    group:group.toString(),groupMembers:group.isNull()?0:group.add(0x38).readU32()});
            }
            if(saved.groupIdentity) {
                const retained=resolve(saved.groupIdentity,0x14);
                row.originalGroupLive=!retained.isNull();
                row.originalGroupMembers=retained.isNull()?0:retained.add(0x38).readU32();
                row.originalGroupPathLive=saved.groupPathIdentity!==null && !resolve(saved.groupPathIdentity,0x14).isNull();
            }
            if(!path.isNull())Object.assign(row,{pathFlags:path.add(0x88).readU32(),links:ints(path.add(0x8c),2),
                routeCounts:[path.add(0x50).readU32(),path.add(0x70).readU32()],
                routeIndices:ints(path.add(0x74),2),destination:ints(path.add(0x1c),2)});
            bump('mover-retirement-marker');emit('mover-retirement-marker',row);
        }
        if (config.schedulerEvents && value.startsWith('PATHQUEUE ')) {
            const buckets=[];
            for(let player=0;player<2;player++)for(let kind=0;kind<4;kind++) {
                const offset=player*0x70+kind*0x1c;
                buckets.push({offset,...schedulerSnapshot(base.add(0xd53a90+offset),null)});
            }
            bump('scheduler-mutation-marker');emit('scheduler-mutation-marker',{value,buckets});
        }
        if (value.startsWith('PATHLIFE ')) {
            emit('blocker-lifecycle-marker',{value});
            if (config.watchCell) snapshotBlockerGeometry(value);
        }
        if (value.startsWith('PATHCAPTAIN ')) emit('captain-marker',{value});
        if (value.startsWith('PATHPAIR ')) emit('pair-marker',{value});
        if (value.startsWith('PATHDOZEN ')) emit('twelve-marker',{value});
        if (value.startsWith('PATHGROUP ')) emit('group-order-marker',{value});
        if (value.startsWith('PATHCROWD ')) emit('crowd-marker', {value});
        if (value.startsWith('PATHTARGET ')) emit('target-marker', {value});
        if (value.startsWith('PATHWIDGET ')) emit('widget-marker', {value});
        if (value.startsWith('PATHSTOCK ')) emit('stock-marker', {value});
        if (config.motionEvents && value.startsWith('PATHPOSE ')) {
            const match = /^PATHPOSE case=([a-z0-9_]+)$/.exec(value);
            if (match) positionCase = match[1];
            else if (/^PATHPOSE done=/.test(value)) positionCase = null;
            else throw new Error('Malformed position marker: ' + value);
            emit('position-marker', {value});
            if (positionCase === 'oblique_small') snapshotObliqueGeometry();
        }
        if (config.motionEvents && value.startsWith('PATHSPEED ')) {
            const match = /^PATHSPEED case=([a-z0-9_]+)$/.exec(value);
            if (match) speedCase = match[1];
            else if (/^PATHSPEED done=/.test(value)) speedCase = null;
            else throw new Error('Malformed speed marker: ' + value);
            emit('speed-marker', {value});
        }
        if (value.startsWith('PATHEXPR ')) emit('expression-marker',{value});
        if (value.startsWith('PATHOVERLAP ')) emit('overlap-marker',{value});
        if (value.startsWith('PATHBOUND ')) emit('bound-marker',{value});
        if (value.startsWith('PATHHOLD ')) emit('hold-marker', {value});
        if (value.startsWith('PATHGROUPRADIUS ')) emit('group-radius-marker',{value});
        if (value.startsWith('PATHMORPH ')) emit('morph-marker',{value});
        if (value.startsWith('PATHSELECT ')) emit('selected-marker', {value});
        if (value.startsWith('PATHRANK ')) emit('formation-rank-marker',{value});
        if (/^PATH(BUFF|CAST) /.test(value)) emit('modifier-marker',{value});
        if (value.startsWith('PATHTRACE ')) {
            if(value.includes('label=start_scheduler_mutation '))schedulerMutationScenario=true;
            if(value.includes('label=start_mover_retirement '))moverRetirementScenario=true;
            if(value.includes('label=start_formation_ranks ')){formationRankScenario=true;pairScenario=true;}
            if (config.clockEvents && /label=start_/.test(value)) clockScenario = true;
            if (config.clockEvents && value.includes('label=complete ')) clockScenario = false;
            if (value.includes('label=start_blocker_lifecycle ') || value.includes('label=start_widget_lifecycle ') || value.includes('label=start_widget_escape ') || value.includes('label=start_widget_build_escape '))
                widgetScenario = true;
            if ((value.includes('label=start_speed_modifiers ') || value.includes('label=start_movement_modes ') || value.includes('label=start_movement_bypasses ') || value.includes('label=start_region_callbacks ') || value.includes('label=start_movement_lifecycle ') || value.includes('label=start_terrain_cache ') || value.includes('label=start_chained_expression ') || value.includes('label=start_target_overlap ') || value.includes('label=start_adaptive_passage ') || value.includes('label=start_captain_home ') || value.includes('label=start_captain_point ') || value.includes('label=start_point_bound_matrix ') || value.includes('label=start_outside_west ') || value.includes('label=start_blocked_goal ') || value.includes('label=start_group_radius_grow ') || value.includes('label=start_group_radius_shrink ') || value.includes('label=start_group_radius_remove ') || value.includes('label=start_moving_radius_matrix ') || value.includes('label=start_moving_radius ') || value.includes('label=start_group_pair ') || value.includes('label=start_group_twelve ') || value.includes('label=start_selected_point_pair ') || value.includes('label=start_selected_point_queued_pair ') || value.includes('label=start_selected_point_mixed_pair ') || value.includes('label=start_selected_point_independent_pair ') || value.includes('label=start_follow_velocity ') || value.includes('label=start_follow_target_remove_reuse ') || value.includes('label=start_follow_target_kill_reuse ') || value.includes('label=start_follow_target_xy ') || value.includes('label=start_follow_target_position ') || value.includes('label=start_follow_target_travel_xy ') || value.includes('label=start_follow_target_travel_position ') || value.includes('label=start_follow_target_grow ') || value.includes('label=start_follow_target_shrink ') || value.includes('label=start_follow_target_resize_gate '))) pairScenario = true;
            if (value.includes('label=start_follow_target_grow ') || value.includes('label=start_follow_target_shrink ') || value.includes('label=start_follow_target_resize_gate ')) resizeScenario = true;
            if (value.includes('label=start_captain_home ')) captainScenario = true;
            if (value.includes('label=complete ')) {pairScenario = false;captainScenario = false;}
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
    if(config.mapLoadEvents) {
    // Read-only complete file-backed load, before map object constructors.
    hook(0x04c860, {
        onEnter() {
            this.loadFile=this.context.edx.isNull() ? null : this.context.edx.readCString();
            this.loadBounds=ints(this.context.ecx,4);
        },
        onLeave() {
            const owner=base.add(0xd53a48).readPointer();
            if(owner.isNull()) throw new Error('Completed map load without owner');
            const game=base.add(0xd3c82c).readPointer();
            const fine=owner.add(0x238).readPointer(), [width,height]=ints(fine.add(0x3c),2);
            const data=fine.add(0x28).readPointer(), cells=[];
            for(let n=0;n<width*height;n++) cells.push(data.add(n*4).readU32()>>>24);
            const hierarchy=[];
            for(let level=0;level<4;level++) {
                const map=owner.add(0x23c+level*4).readPointer(), [w,h]=ints(map.add(0x3c),2);
                const data=map.add(0x28).readPointer(), values=[];
                for(let n=0;n<w*h;n++) {
                    const word=data.add(n*8+4).readU32();
                    values.push([0,2,4,6].map(s=>(word>>>(30-s))&3));
                }
                hierarchy.push({level,width:w,height:h,values});
            }
            bump('map-load-complete');
            emit('map-load-complete',{filename:this.loadFile,inputBounds:this.loadBounds,
                worldBounds:ints(game.add(0x6c),4),width,height,cells,hierarchy,
                constants:Object.fromEntries([0xd53a50,0xd53a54,0xd53a58,0xd53a5c,0xd53a60]
                    .map(rva=>[rva.toString(16),base.add(rva).readFloat()]))});
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
rpc.exports = {status() {return {installed, samples, counts};},
    finish() {recording = false; return {installed, samples, counts};}};
