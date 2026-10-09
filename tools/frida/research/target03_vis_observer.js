// TARGET-03.1/03.2 read-only visibility-policy extension, appended to target021_observer.js by
// target03_capture.py. No calls into game code, no writes. Arms only for the three pursuit visibility
// callers of 66fdd0 (ECX owner unit, stack target, flags, mode; RET c):
//   23a7e9 PathGroup_IsTargetNotVisible, 5fb9f8 CAbilityMove_ValidateTarget, 5ff689 CAbilityMove_OnTargetLost.
// While armed it records the nested 1dd920(player, target, flags|global, mode, 0) arguments/result, its
// 1ddff0 detection and 1ddee0 fog results and the 699b20 owner/shared-vision mask fallback with target
// +148/+14c words, then the final 66fdd0 result.
function installTarget03Visibility(base, hook, emitRow, counterFn) {
    const u32 = p => p.readU32() >>> 0;
    const CALLERS = {0x23a7e9: 'group-callback', 0x5fb9f8: 'validate', 0x5ff689: 'target-lost-reissue'};
    let armed = null;
    hook(0x66fdd0, {
        onEnter(args) {
            const ret = this.returnAddress.sub(base).toUInt32();
            this.role = CALLERS[ret] || null;
            if (!this.role) return;
            armed = {c: counterFn(), role: this.role, owner: this.context.ecx.toString(), target: args[0].toString(),
                flags: args[1].toUInt32(), mode: args[2].toUInt32(), steps: []};
            try {armed.tw = [u32(args[0].add(0x20)), u32(args[0].add(0x5c)), u32(args[0].add(0x148)), u32(args[0].add(0x14c))];} catch (e) {armed.tw = null;}
            this.row = armed;
        },
        onLeave(ret) {
            if (!this.role) return;
            this.row.result = ret.toUInt32();
            emitRow('vis-query', this.row);
            armed = null;
        }
    });
    hook(0x1dd920, {
        onEnter(args) {this.a = armed; if (this.a) this.row = {player: args[0].toUInt32() & 0xffff, flags: args[2].toUInt32(), mode: args[3].toUInt32(), a4: args[4].toUInt32()};},
        onLeave(ret) {if (this.a) {this.row.result = ret.toUInt32(); this.a.steps.push(['1dd920', this.row]);}}
    });
    hook(0x1ddff0, {
        onEnter(args) {this.a = armed; if (this.a) this.row = {mask: args[1].toUInt32() & 0xffff, detectFlag: args[2].toUInt32()};},
        onLeave(ret) {if (this.a) {this.row.result = ret.toUInt32(); this.a.steps.push(['1ddff0-detect', this.row]);}}
    });
    hook(0x1ddee0, {
        onEnter(args) {this.a = armed; if (this.a) this.row = {players: args[0].toUInt32(), cell: [args[1].readS32(), args[1].add(4).readS32()], mode: args[2].toUInt32()};},
        onLeave(ret) {if (this.a) {this.row.result = ret.toUInt32(); this.a.steps.push(['1ddee0-fog', this.row]);}}
    });
    hook(0x699b20, {
        onEnter(args) {this.a = armed; if (this.a) this.row = {player: args[0].toUInt32() & 0xff};},
        onLeave(ret) {if (this.a) {this.row.result = ret.toUInt32() & 0xff; this.a.steps.push(['699b20-mask', this.row]);}}
    });
}
