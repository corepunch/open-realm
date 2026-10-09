// Read-only portal exclusion; function-entry hooks only, no game calls or writes.
let installed=false,recording=true,active=false;
const counts={};
const bump=key=>counts[key]=(counts[key]||0)+1;
const u32=p=>p.readU32()>>>0;
const emit=(event,data={})=>{if(recording)send({event,ms:Date.now(),...data});};
rpc.exports={finish(){recording=false;return {installed,counts};}};
Process.attachModuleObserver({onAdded(module) {
    if(module.name.toLowerCase()!=='game.dll')return;
    const base=module.base,pe=base.add(base.add(0x3c).readU32());
    if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)
        throw new Error('Portal188 PE mismatch');
    if(installed)return;installed=true;
    let scope=null;
    const counter=()=>u32(base.add(0xd53a48).readPointer().add(0x538));
    const flags=record=>record.isNull()?null:u32(record.add(0x40));
    const record=(event,data)=>{if(active){bump(event);emit(event,{c:counter(),...data});}};
    Interceptor.attach(base.add(0x231df0),{onEnter(args){
        if(args[0].isNull())return;
        const value=args[0].readCString();
        if(!value||!value.startsWith(config.prefix))return;
        if(value.includes(' label=begin-setup'))active=true;
        bump('marker');emit('marker',{value,c:counter()});
        if(value.includes(' label=end-cleanup'))active=false;
    }});
    Interceptor.attach(base.add(0x16ec00),{onEnter(args){
        this.parent=scope;scope=this;
        this.self=this.context.ecx.add(0x98).readPointer();this.point=args[0];
        this.before=flags(this.self);
        record('portal-enter',{self:this.self.toString(),before:this.before,
            point:[u32(this.point),u32(this.point.add(4))],args:[args[1].toUInt32(),args[2].toUInt32(),args[3].toUInt32()]});
    },onLeave(ret){
        record('portal-leave',{self:this.self.toString(),before:this.before,after:flags(this.self),
            result:ret.toUInt32(),point:[u32(this.point),u32(this.point.add(4))]});scope=this.parent;
    }});
    Interceptor.attach(base.add(0x16ecc0),{onEnter(){if(scope)
        record('portal-held',{self:scope.self.toString(),flags:flags(scope.self),
            callback:this.context.esp.add(28).readPointer().toString(),context:this.context.esp.add(32).readPointer().toString()});
    }});
    Interceptor.attach(base.add(0x16ee00),{onEnter(){if(scope)
        record('portal-candidate',{self:scope.self.toString(),flags:flags(scope.self),
            outer:this.context.edx.add(0x24).readPointer().toString()});
    }});
}});
