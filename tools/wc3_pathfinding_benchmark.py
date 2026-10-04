#!/usr/bin/env python3
"""Bounded production-map movement benchmark; requires local Frida and WC3 data.

Uses public CreateUnit and group Move. Compile ABI offsets from current headers,
record artifact hashes and native scenery counts, and distinguish requested from
actually advancing movers. No game rules or obstacles are disabled.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import time

# Native callbacks keep JavaScript, message delivery and remote memory reads
# outside measured frames. Public orders are also issued natively after setup.
NATIVE = r'''
typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned long long u64;
typedef struct { long long sec,nsec; } timespec;
typedef struct { void *unit; float origin[2],previous[2],direction[2]; } mover;
typedef struct { u64 cpu,wall; u32 sim,advancing,velocity,pad; } frame;
typedef struct { u64 cpu; u32 sim,requested,accepted,pad; } order;
extern int gettime(int,timespec *);
extern _Bool issue(void *);
extern _Bool select_point(void *,void *);
extern u8 client_entity[];
extern void finish(void);
extern u8 game_level[];
extern mover movers[];
extern frame frames[];
extern order orders[];
extern u32 state[];
extern u64 timing[];
#define cpu0 timing[0]
#define wall0 timing[1]
#define movement_cpu timing[2]
#define frame_movement_start timing[3]
#define movement_start timing[4]
#define movement_depth timing[5]
static u64 clock_ns(int id) { timespec t; gettime(id,&t); return (u64)t.sec*1000000000ULL+t.nsec; }
static u32 sim(void) { return *(u32 *)(game_level+TIME); }
void prime_frame(void) {
    cpu0=clock_ns(3);wall0=clock_ns(1);frame_movement_start=movement_cpu;
}
void on_enter(void *context) {
    if(state[0])return;
    prime_frame();
    if(sim()<state[2])return;
    u64 begin=clock_ns(3); u32 accepted=0;
    for(u32 first=0;first<COUNT;first+=GROUP){
        u8 request[REQUEST]; for(u32 i=0;i<REQUEST;i++)request[i]=0;
        u32 count=COUNT-first; if(count>GROUP)count=GROUP;
        u32 previous_request[GROUP];void *previous_goal[GROUP];
        for(u32 i=0;i<count;i++){
            u8 *unit=movers[first+i].unit;
            previous_request[i]=*(u32 *)(unit+PREVIOUS_REQUEST);
            previous_goal[i]=*(void **)(unit+GOAL);
            *(void **)(request+i*MEMBER)=unit;
            *(u32 *)(request+i*MEMBER+SPAWN_MEMBER)=*(u32 *)(unit+SPAWN);
        }
        float sign=state[3]%2?-1:1;
        float point[2]={movers[first].origin[0]+sign*movers[first].direction[0],movers[first].origin[1]+sign*movers[first].direction[1]};
        *(u32 *)(request+REQ_COUNT)=count; *(u32 *)(request+REQ_ORDER)=ORDER_ID;
        *(void **)(request+REQ_NAME)=order_name; *(void **)(request+REQ_POINT)=point;
#if SELECTED_ORDER
        select_point(client_entity,point);
#else
        issue(request);
#endif
        for(u32 i=0;i<count;i++){
            u8 *unit=movers[first+i].unit;
            if(*(u32 *)(unit+CURRENT_ORDER)==ORDER_ID && *(void **)(unit+CURRENT_MOVE)==move_walk &&
                (*(u32 *)(unit+PREVIOUS_REQUEST)!=previous_request[i] || *(void **)(unit+GOAL)!=previous_goal[i]))accepted++;
        }
    }
    u64 order_cpu=clock_ns(3)-begin;
    /* Gum suppresses nested interception inside this listener. Charge native
     * submission explicitly; owner hooks only observe subsequent game work. */
    movement_cpu+=order_cpu;
    if(state[4]<ORDER_CAPACITY)orders[state[4]++]=(order){order_cpu,sim(),COUNT,accepted,0};
    state[3]++; state[2]=sim()+REORDER;
}
void on_leave(void *context) {
    if(state[0]||!cpu0)return;
    u64 cpu=clock_ns(3)-cpu0,wall=clock_ns(1)-wall0;
    cpu0=0;
    u32 advancing=0,velocity=0;
    for(u32 i=0;i<COUNT;i++){
        float *p=(float *)((u8 *)movers[i].unit+POSITION),*v=(float *)((u8 *)movers[i].unit+VELOCITY);
        if(p[0]!=movers[i].previous[0]||p[1]!=movers[i].previous[1])advancing++;
        movers[i].previous[0]=p[0];movers[i].previous[1]=p[1];
        if(v[0]!=0||v[1]!=0)velocity++;
    }
    if(state[1]>=CAPACITY){state[0]=2;finish();return;}
    frames[state[1]++]=(frame){cpu,wall,sim(),advancing,velocity,(u32)((movement_cpu-frame_movement_start)/1000)};
    if(sim()>=START+DURATION){state[0]=1;finish();}
}
'''


def layout(root):
    fields = {
        "pointer_size": "sizeof(void *)", "edict_size": "sizeof(edict_t)", "level_size": "sizeof(level)", "export_size": "offsetof(struct game_export,edict_size)",
        "num_edicts": "offsetof(struct game_export,num_edicts)",
        "time": "offsetof(typeof(level),time)", "random": "offsetof(typeof(level),pathing_random)",
        "request_size": "sizeof(groupPointOrder_t)", "member_size": "sizeof(((groupPointOrder_t *)0)->units[0])",
        "request_count": "offsetof(groupPointOrder_t,count)", "order_id": "offsetof(groupPointOrder_t,order_id)",
        "issuer": "offsetof(groupPointOrder_t,issuer_player)", "order": "offsetof(groupPointOrder_t,order)",
        "point": "offsetof(groupPointOrder_t,point)", "member_spawn": "offsetof(groupPointOrder_t,units[0].spawn)",
        "static_flag": "SVF_STATIC_SCENERY", "tree_type": "TARG_TREE", "max_group": "BZ_WC3_GROUP_ORDER_UNITS",
        "geometry_size": "sizeof(pathAccelParams_t)", "geometry_from": "offsetof(pathAccelParams_t,from)",
        "geometry_target": "offsetof(pathAccelParams_t,target)",
        "geometry_radius": "offsetof(pathAccelParams_t,radius)", "geometry_flags": "offsetof(pathAccelParams_t,blocked_flags)",
        "ability_list": "offsetof(UnitAbilities_t,abilList)",
        "ability_owner_update": "A_OWNER_UPDATE",
    }
    for field in ("inuse", "s.number", "s.player", "s.origin2", "s.model", "class_id", "spawn_time",
                  "collision", "health.value", "svflags", "destructable", "targtype", "currentmove", "current_order_id", "goalentity", "selected",
                  "movement.velocity", "movement.fine_pose", "movement.group_id", "movement.previous_request_id", "data.Doodads", "data.UnitAbilities"):
        fields[field.replace('.', '_')] = f"offsetof(edict_t,{field})"
    lines = ['#include "games/warcraft-3/game/g_local.h"', '#include <stddef.h>', 'int main(void) {']
    for key, expr in fields.items():
        lines.append(f'printf("{key} %zu\\n", (size_t)({expr}));')
    lines.append('return 0;}')
    with tempfile.TemporaryDirectory(prefix="wc3-bench-abi-") as directory:
        src, binary = Path(directory) / 'abi.c', Path(directory) / 'abi'
        src.write_text('\n'.join(lines))
        result = subprocess.run(['cc', '-std=gnu11', '-I.', '-Ishared', '-Ishared/types', '-Igames/warcraft-3',
                                 '-Igames/warcraft-3/common', '-Igames/warcraft-3/game', str(src), '-o', str(binary)],
                                cwd=root, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError('Benchmark ABI compilation failed:\n' + result.stderr)
        return {key: int(value) for key, value in
                (line.split() for line in subprocess.check_output([str(binary)], text=True).splitlines())}


def symbols(path):
    result = {}
    for line in subprocess.check_output(['nm', '-an', str(path)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                result[parts[2]] = int(parts[0], 16)
            except ValueError:
                pass
    return result


SCRIPT = r'''
const stamp=Memory.alloc(16);
const gettime=new NativeFunction(Module.getGlobalExportByName('clock_gettime'),'int',['int','pointer']);
function clock(id){gettime(id,stamp);return stamp.readU64().toNumber()*1000+stamp.add(8).readU64().toNumber()/1e6;}
let installed=false,sim=()=>0,last=0,window=0,intervals=[],orders=0;
const nativeControllers=[];
const quitText=Memory.allocUtf8String('quit\n');
const queueText=new NativeFunction(Process.mainModule.base.add(app.Cbuf_AddText),'void',['pointer']);
if(cfg.render)Interceptor.attach(Process.mainModule.base.add(app.CL_Frame),{onEnter(){
    const now=clock(1);
    if(last&&sim()>0)intervals.push(now-last);
    last=now;
    if(!window)window=now;
    if(now-window>=1000){send({event:'render',sim:sim(),fps:intervals.length*1000/(now-window),interval_ms:intervals});window=now;intervals=[];}
}});
Process.attachModuleObserver({onAdded(module){
    if(installed||module.name!==cfg.libraryName)return;
    installed=true;
    const addr=name=>module.base.add(syms[name]);
    const globals=addr('globals'),level=addr('level');
    sim=()=>level.add(abi.time).readU32();
    const create=new NativeFunction(addr('unit_create'),'pointer',['uint32','uint32','pointer','float']);
    const issue=new NativeFunction(addr('G_IssueGroupPointOrder'),'bool',['pointer']);
    const selectPoint=new NativeFunction(addr('move_selectlocation'),'bool',['pointer','pointer']);
    const getPlayerEntity=new NativeFunction(addr('G_GetPlayerEntityByNumber'),'pointer',['uint32']);
    let playerEntity=ptr(0);
    const disabled=new NativeFunction(addr('M_UnitMoveDisabled'),'bool',['pointer']);
    const getbounds=new NativeFunction(addr('CM_GetWorldBounds'),['float','float','float','float'],[]);
    const staticLine=new NativeFunction(addr('G_MovePathLineIsPathable'),'bool',['pointer']);
    const staticPoint=new NativeFunction(addr('G_MovePathPointIsPathable'),'bool',['pointer']);
    const names=[Memory.allocUtf8String('move')],point=Memory.alloc(8),request=Memory.alloc(abi.request_size);
    const orderid=new NativeFunction(addr('G_OrderId'),'uint32',['pointer'])(names[0]);
    let spawned=false,units=[],origins=[],previous=[],directions=[],nextOrder=6000,started=0,frames=0;
    const edicts=()=>addr('g_edicts').readPointer();
    function position(u){return [u.add(abi.s_origin2).readFloat(),u.add(abi.s_origin2+4).readFloat()];}
    function census(){
        const counts={live:0,static:0,destructables:0,trees:0,other_destructables:0,models:0},classes={};
        const base=edicts(),n=globals.add(abi.num_edicts).readU32();
        for(let i=0;i<n;i++){
            const u=base.add(i*abi.edict_size);if(!u.add(abi.inuse).readU8())continue;
            counts.live++;
            if(u.add(abi.s_model).readU16())counts.models++;
            if(u.add(abi.svflags).readU32()&abi.static_flag){counts.static++;const code=u.add(abi.class_id).readU32();classes[code]=(classes[code]||0)+1;}
            if(!u.add(abi.destructable).readPointer().isNull()){
                counts.destructables++;
                if(u.add(abi.targtype).readU32()===abi.tree_type)counts.trees++;else counts.other_destructables++;
            }
        }
        return {counts,static_classes:classes,edict_highwater:n};
    }
    function setup(){
        const bounds=getbounds();
        if(globals.add(abi.export_size).readU32()!==abi.edict_size)throw new Error('edict ABI does not match headers');
        send({event:'map',sim:sim(),...census()});
        const base=edicts(),n=globals.add(abi.num_edicts).readU32();let seed=null,existing=[];
        for(let i=0;i<n;i++){
            const u=base.add(i*abi.edict_size);
            if(u.add(abi.inuse).readU8()&&u.add(abi.s_player).readU8()===0&&u.add(abi.collision).readFloat()>0&&
                u.add(abi.health_value).readFloat()>0&&!disabled(u)){
                if(!seed)seed=u;
                if(cfg.existing&&existing.length<cfg.units)existing.push(u);
            }
        }
        if(!seed&&cfg.placement==='region')throw new Error('No live player-zero mover for regional placement');
        const seedpos=seed?position(seed):[0,0],cols=Math.ceil(Math.sqrt(cfg.units));
        const code=cfg.unit.charCodeAt(0)|(cfg.unit.charCodeAt(1)<<8)|(cfg.unit.charCodeAt(2)<<16)|(cfg.unit.charCodeAt(3)<<24);
        const begin=clock(3),locations=[],vectors=[];
        existing.forEach(u=>{units.push(u);origins.push(position(u));previous.push(position(u));directions.push([cfg.distance,cfg.distance*.5]);});
        if(cfg.placement!=='region'){
            const geometry=Memory.alloc(abi.geometry_size);geometry.writeByteArray(new Uint8Array(abi.geometry_size));
            geometry.add(abi.geometry_from).writePointer(point);geometry.add(abi.geometry_radius).writeFloat(16);
            geometry.add(abi.geometry_flags).writeU8(2);
            const target=Memory.alloc(8);geometry.add(abi.geometry_target).writePointer(target);
            for(let y=bounds[1]+cfg.spacing;y<bounds[3]-cfg.spacing&&locations.length<cfg.units;y+=cfg.spacing)
                for(let x=bounds[0]+cfg.spacing;x<bounds[2]-cfg.spacing&&locations.length<cfg.units;x+=cfg.spacing){
                    point.writeFloat(x);point.add(4).writeFloat(y);
                    if(!staticPoint(geometry))continue;
                    let vector=[cfg.distance,cfg.distance*.5];
                    if(cfg.placement==='corridor'){
                        vector=null;
                        for(const d of [[cfg.distance,0],[0,cfg.distance]]){
                            target.writeFloat(x+d[0]);target.add(4).writeFloat(y+d[1]);
                            if(!staticLine(geometry))continue;
                            target.writeFloat(x-d[0]);target.add(4).writeFloat(y-d[1]);
                            if(staticLine(geometry)){vector=d;break;}
                        }
                        if(!vector)continue;
                    }
                    locations.push([x,y]);vectors.push(vector);
                }
            if(locations.length<cfg.units)throw new Error('Map lacks '+cfg.units+' spaced traversable spawn positions');
        }
        for(let i=0;i<cfg.units;i++){
            const p=cfg.placement!=='region'?locations[i]:[seedpos[0]+(i%cols-cols/2)*cfg.spacing,seedpos[1]+(Math.floor(i/cols)-cols/2)*cfg.spacing];
            point.writeFloat(p[0]);point.add(4).writeFloat(p[1]);
            if(i<existing.length)continue;
            const u=create(0,code,point,0);
            if(u.isNull())throw new Error('CreateUnit failed at '+i);
            units.push(u);origins.push(position(u));previous.push(position(u));directions.push(vectors[i]||[cfg.distance,cfg.distance*.5]);
        }
        started=sim();spawned=true;
        if(cfg.selected){
            playerEntity=getPlayerEntity(0);
            if(playerEntity.isNull())throw new Error('Player zero has no command entity');
            for(let i=0;i<globals.add(abi.num_edicts).readU32();i++)edicts().add(i*abi.edict_size+abi.selected).writeU32(0);
            units.forEach(u=>u.add(abi.selected).writeU32(1));
        }
        send({event:'setup',sim:sim(),requested:cfg.units,created:units.length-existing.length,existing:existing.length,controlled:units.length,cpu_ms:clock(3)-begin,...census()});
    }
    function order(){
        const begin=clock(3);let accepted=0;
        for(let first=0;first<units.length;first+=cfg.cohort){
            request.writeByteArray(new Uint8Array(abi.request_size));
            const count=Math.min(cfg.cohort,units.length-first);
            for(let j=0;j<count;j++){
                const u=units[first+j];request.add(j*abi.member_size).writePointer(u);
                request.add(j*abi.member_size+abi.member_spawn).writeU32(u.add(abi.spawn_time).readU32());
            }
            const p=origins[first],sign=orders%2?-1:1;
            point.writeFloat(p[0]+sign*directions[first][0]);point.add(4).writeFloat(p[1]+sign*directions[first][1]);
            request.add(abi.request_count).writeU32(count);request.add(abi.order_id).writeU32(orderid);
            request.add(abi.issuer).writeU32(0);request.add(abi.order).writePointer(names[0]);request.add(abi.point).writePointer(point);
            const candidates=units.slice(first,first+count);
            const previousRequest=candidates.map(u=>u.add(abi.movement_previous_request_id).readU32());
            const previousGoal=candidates.map(u=>u.add(abi.goalentity).readPointer());
            const batchAccepted=cfg.selected?selectPoint(playerEntity,point):issue(request);
            const members=candidates.map((u,i)=>({number:u.add(abi.s_number).readU32(),
                order_id:u.add(abi.current_order_id).readU32(),walk:u.add(abi.currentmove).readPointer().equals(addr('move_move_walk')),
                request_changed:u.add(abi.movement_previous_request_id).readU32()!==previousRequest[i],
                goal_changed:!u.add(abi.goalentity).readPointer().equals(previousGoal[i])}));
            accepted+=members.filter(m=>m.order_id===orderid&&m.walk&&(m.request_changed||m.goal_changed)).length;
            send({event:'order_members',sim:sim(),batch_accepted:batchAccepted,point:[point.readFloat(),point.add(4).readFloat()],members});
        }
        orders++;send({event:'order',sim:sim(),requested:units.length,accepted,cpu_ms:clock(3)-begin});
    }
    let controller=null,nativeHook=null,finishFirstFrame=null;
    function nativeRun(){
        if(cfg.profile&&cfg.profile_detail!=='owners')Interceptor.attach(addr('G_BindEntityData'),{onEnter(args){this.unit=args[0];},onLeave(){
            const row=this.unit.add(abi.data_UnitAbilities).readPointer();
            const list=row.isNull()?ptr(0):row.add(abi.ability_list).readPointer();
            send({event:'bind',sim:sim(),class_id:this.unit.add(abi.class_id).readU32(),abilities:list.isNull()?null:list.readUtf8String()});
        }});
        const capacity=Math.ceil(cfg.duration/100)+128,records=Memory.alloc(capacity*32),orderCapacity=Math.ceil(cfg.duration/cfg.reorder)+4,orderRows=Memory.alloc(orderCapacity*24);
        const plan=Memory.alloc(units.length*32),state=Memory.alloc(20),timing=Memory.alloc(48);
        timing.writeByteArray(new Uint8Array(48));
        state.writeByteArray(new Uint8Array(20));state.add(8).writeU32(nextOrder);state.add(12).writeU32(orders);
        units.forEach((u,i)=>{const row=plan.add(i*32);row.writePointer(u);
            row.add(8).writeFloat(origins[i][0]);row.add(12).writeFloat(origins[i][1]);
            row.add(16).writeFloat(previous[i][0]);row.add(20).writeFloat(previous[i][1]);
            row.add(24).writeFloat(directions[i][0]);row.add(28).writeFloat(directions[i][1]);});
        // The scheduled pass includes other ability owners: this is a conservative
        // bound. Avoid a listener/clock call for every bound member's no-op think.
        const ownerNames=['M_RunScheduledThinks','S_RunMoveTimers','M_SamplePoses','CM_ProcessPathJobs',
            'G_IssueGroupPointOrder','move_selectlocation'];
        const pipelineNames=['move_run_group_updates','move_group_route','move_group_decide',
            'move_group_regroup','move_repulse_owner_update','unit_commit_motion',
            'move_find_route','move_adaptive_progress','move_retry_fine',
            'move_allocate_group_id','Waypoint_add','unit_issueorder',
            'S_RecoverStoppedUnitPosition','G_UnitMoveGroupDestination','CM_ProcessPathJobs','S_WaygateBuildEdges'];
        const profileNames=cfg.profile?(cfg.profile_detail==='owners'?ownerNames:cfg.profile_detail==='pipeline'?pipelineNames:[
            'M_RunScheduledThinks','G_RunEntities','M_SamplePoses','S_RunMoveTimers',
            'slow_aura_prepare','slow_aura_bonus','regen_aura_cache_update','hero_aura_bonus',
            'G_BindEntityData','S_InvalidateAuraSources','S_InvalidateEnduranceSources',
            'endurance_prepare','unit_effective_speed_with_bonus.part.0','S_ApplyEnduranceMoveSpeed',
            'G_UnitRegionPositionChanged','move_find_group','move_retry_fine','G_UnitStatusLevel',
            'move_group_route','move_find_route','move_adaptive_progress','move_fine_edges',
            'move_spatial_sync','move_occupancy_cell','move_cell_ok',
            'G_UnitMovePathFinePointIsPathable','move_query_line.constprop.0',
            'wc3_acc_search.constprop.0','wc3_fine_node.constprop.0.isra.0',
            'wc3_fine_enqueue','wc3_fine_pop','unit_current_speed','wc3_atan','wc3_sincos',
            'move_has_dynamic_occupancy','S_UnitMoveFineObjectFlags',
        ]).filter(name=>syms[name]!==undefined && (cfg.profile_detail==='fine' || !['G_UnitStatusLevel','wc3_atan','wc3_sincos','move_fine_edges','move_occupancy_cell','move_cell_ok','wc3_fine_node.constprop.0.isra.0','wc3_fine_enqueue','wc3_fine_pop','move_has_dynamic_occupancy','S_UnitMoveFineObjectFlags'].includes(name))):[];
        const profiles=Memory.alloc(Math.max(1,profileNames.length)*24),diagnostics=Memory.alloc(16);
        diagnostics.writeByteArray(new Uint8Array(16));
        profiles.writeByteArray(new Uint8Array(Math.max(1,profileNames.length)*24));
        const finish=new NativeCallback(()=>{
            const count=state.add(4).readU32();
            for(let i=0;i<count;i++){const row=records.add(i*32);send({event:'simulation',frame:i,
                sim:row.add(16).readU32(),cpu_ms:row.readU64().toNumber()/1e6,wall_ms:row.add(8).readU64().toNumber()/1e6,
                movement_cpu_ms:cfg.profile_detail==='owners'&&cfg.profile?row.add(28).readU32()/1000:null,
                advancing:row.add(20).readU32(),velocity:row.add(24).readU32()});}
            for(let i=0;i<state.add(16).readU32();i++){const row=orderRows.add(i*24);send({event:'order',
                sim:row.add(8).readU32(),requested:row.add(12).readU32(),accepted:row.add(16).readU32(),cpu_ms:row.readU64().toNumber()/1e6});}
            if(profileNames.length)send({event:'profile',endurance_discoveries:diagnostics.readU64().toNumber(),functions:Object.fromEntries(profileNames.map((name,i)=>{
                const row=profiles.add(i*24);return [name,{calls:row.add(8).readU64().toNumber(),cpu_ms:row.add(16).readU64().toNumber()/1e6}];
            }))});
            send({event:state.readU32()===1?'final':'error' ,sim:sim(),frames:count,positions:units.map(position),
                members:units.map(u=>{const goal=u.add(abi.goalentity).readPointer();return {number:u.add(abi.s_number).readU32(),
                    order_id:u.add(abi.current_order_id).readU32(),walk:u.add(abi.currentmove).readPointer().equals(addr('move_move_walk')),
                    group:u.add(abi.movement_group_id).readU32(),goal:goal.isNull()?null:position(goal)};}),
                random:[level.add(abi.random).readU32(),level.add(abi.random+4).readU32()]});
            // Let shutdown flush sampling profiles; all measured work is over.
            queueText(quitText);
        },'void',[]);
        const defines={TIME:abi.time,COUNT:units.length,GROUP:cfg.cohort,REQUEST:abi.request_size,MEMBER:abi.member_size,
            SPAWN_MEMBER:abi.member_spawn,SPAWN:abi.spawn_time,REQ_COUNT:abi.request_count,REQ_ORDER:abi.order_id,
            ORDER_ID:orderid,REQ_NAME:abi.order,REQ_POINT:abi.point,REORDER:cfg.reorder,DISTANCE:cfg.distance,
            SELECTED_ORDER:cfg.selected?1:0,
            POSITION:abi.s_origin2,VELOCITY:abi.movement_velocity,CURRENT_ORDER:abi.current_order_id,CURRENT_MOVE:abi.currentmove,
            PREVIOUS_REQUEST:abi.movement_previous_request_id,GOAL:abi.goalentity,CAPACITY:capacity,ORDER_CAPACITY:orderCapacity,START:started,DURATION:cfg.duration};
        let code=Object.entries(defines).map(([key,value])=>'#define '+key+' '+value+'\n').join('');
        code+='extern unsigned char order_name[],move_walk[];\n'+nativeCode;
        code+='\n#include <gum/guminterceptor.h>\nextern unsigned long long profiles[],diagnostics[];extern unsigned char endurance_dirty[];extern unsigned int endurance_generation[],ability_generation[];\n';
        profileNames.forEach((name,i)=>{const movement=ownerNames.includes(name);code+=
            'void enter_'+i+'(GumInvocationContext *ctx){u64 *start=gum_invocation_context_get_listener_invocation_data(ctx,sizeof(u64));*start=0;'+
            (name==='CAbilityMove'?'if((unsigned long)gum_invocation_context_get_nth_argument(ctx,1)!='+abi.ability_owner_update+')return;':'')+
            '*start=clock_ns(3);profiles['+(i*3+1)+']++;'+
            (movement?'if(!movement_depth++)movement_start=*start;':'')+
            (name==='endurance_prepare'?'if(*endurance_dirty||*endurance_generation!=*ability_generation)diagnostics[0]++;':'')+'}\n'+
            'void leave_'+i+'(GumInvocationContext *ctx){u64 start=*(u64 *)gum_invocation_context_get_listener_invocation_data(ctx,sizeof(u64));'+
            'if(start){u64 end=clock_ns(3);profiles['+(i*3+2)+']+=end-start;'+
            (movement?'if(!--movement_depth)movement_cpu+=end-movement_start;':'')+'}}\n';});
        controller=new CModule(code,{gettime:Module.getGlobalExportByName('clock_gettime'),issue:addr('G_IssueGroupPointOrder'),
            select_point:addr('move_selectlocation'),client_entity:cfg.selected?playerEntity:state,
            finish,game_level:level,movers:plan,frames:records,orders:orderRows,state,timing,profiles,diagnostics,endurance_dirty:syms.endurance_dirty===undefined?diagnostics:addr('endurance_dirty'),endurance_generation:syms.endurance_generation===undefined?diagnostics:addr('endurance_generation'),ability_generation:addr('ability_data_generation'),order_name:names[0],move_walk:addr('move_move_walk')});
        // Keep every buffer and callback alive until the native controller retires.
        controller.keep=[records,orderRows,plan,state,timing,profiles,diagnostics,finish,names];
        nativeControllers.push(controller);
        profileNames.forEach((name,i)=>Interceptor.attach(addr(name),{onEnter:controller['enter_'+i],onLeave:controller['leave_'+i]}));
        nativeHook=Interceptor.attach(addr('G_RunFrame'),{onEnter:controller.on_enter,onLeave:controller.on_leave});
        // This G_RunFrame invocation has already entered the original listener.
        // Seed its clock after setup and finish it through that listener so the
        // first actual path-search tick is measured, even if Gum only installs
        // the new frame listener for subsequent invocations.
        finishFirstFrame=new NativeFunction(controller.on_leave,'void',['pointer']);
        new NativeFunction(controller.prime_frame,'void',[])();
    }
    let frameHook=null;
    frameHook=Interceptor.attach(addr('G_RunFrame'),{onEnter(){
        if(!spawned&&sim()>=6000){
            setup();order();nextOrder=sim()+cfg.reorder;nativeRun();
        }
    },onLeave(){if(finishFirstFrame){finishFirstFrame(ptr(0));finishFirstFrame=null;frameHook.detach();}}
    });
    send({event:'installed',edict_size:abi.edict_size,library:module.path});
}});
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build/bin/openwarcraft3'))
    parser.add_argument('--library', type=Path, default=Path('build/lib/libgame.so'))
    parser.add_argument('--data', required=True)
    parser.add_argument('--map', default='Maps/FrozenThrone/Campaign/NightElfX01.w3x')
    parser.add_argument('--units', type=int, default=12)
    parser.add_argument('--unit', default='earc', help='Stock unit rawcode used for additional movers')
    parser.add_argument('--spacing', type=float, default=96)
    parser.add_argument('--placement', choices=('map', 'region', 'corridor'), default='map', help='Map-wide traversable cells or a congested square around the starting army')
    parser.add_argument('--cohort', type=int, default=12, help='Units per public group order, 1..12')
    parser.add_argument('--distance', type=float, default=768)
    parser.add_argument('--reorder', type=int, default=4000)
    parser.add_argument('--duration', type=int, default=20000, help='Measured simulation milliseconds')
    parser.add_argument('--timeout', type=float, default=180, help='Wall-clock limit including map load and spawn')
    parser.add_argument('--video-mode', type=int, default=10, help='Renderer mode index; default 10 is 1920x1080')
    parser.add_argument('--render', action='store_true')
    parser.add_argument('--existing',action='store_true',help='Use the starting player-zero army first, then add stock units to the requested count')
    parser.add_argument('--selected',action='store_true',help='Issue through the selected-unit command owner (requires at most one cohort)')
    parser.add_argument('--profile', action='store_true', help='Coarse nested CPU timers; use separate runs from acceptance timing')
    parser.add_argument('--profile-detail', choices=('owners','pipeline','coarse','fine'), default='coarse',help='owners bounds movement using disjoint scheduled, sample, timer and order costs; pipeline and other modes include nested diagnostic timers')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.units <= 2048:
        parser.error('--units must be 1..2048')
    if not 1 <= args.cohort <= 12 or any(not math.isfinite(v) or v <= 0 for v in (args.spacing,args.distance,args.timeout)) or args.reorder <= 0 or not 1 <= args.duration <= 3600000:
        parser.error('Finite positive spacing, distance, timeout and reorder required; cohort 1..12 and duration 1..3600000')
    if len(args.unit) != 4:
        parser.error('--unit must be a four-character rawcode')
    if args.selected and args.units>args.cohort:
        parser.error('--selected requires units no greater than cohort')
    try:
        import frida
        if not hasattr(frida, 'get_local_device'):
            raise ImportError('Frida runtime is absent')
    except ImportError as error:
        parser.error('Run with a Python environment containing the Frida package: ' + str(error))
    root = Path(__file__).resolve().parents[1]
    binary, library = args.binary.resolve(), args.library.resolve()
    abi = layout(root)
    if abi['pointer_size'] != 8:
        parser.error('Native benchmark currently requires a 64-bit Linux host')
    # Reject stale libraries before compiled level offsets reach native hooks.
    # Switching output directories/build modes alone does not rebuild Make targets.
    level_symbol = next((line.split() for line in subprocess.check_output(
        ['nm', '-S', '--defined-only', str(library)], text=True).splitlines()
        if line.split()[-1:] == ['level']), None)
    if not level_symbol or int(level_symbol[1], 16) != abi['level_size']:
        parser.error('Library level ABI does not match current headers; rebuild the explicit openwarcraft3 target')
    argv = [str(binary), '-data', args.data, '-tft', '+set', 'skip_cutscene', '1',
            '+com_frame_limit', str((6000 + args.duration) // 100 + 12), '+map', args.map]
    if args.render:
        argv[1:1] = ['+set', 'vid_native', '0', '+set', 'vid_fullscreen', '0', '+set', 'vid_mode', str(args.video_mode),
                     '+set', 'com_maxfps', '60', '+set', 'r_vsync', '0']
        argv[argv.index('+com_frame_limit') + 1] = str((6000 + args.duration) * 60 // 1000 + 600)
    else:
        argv[1:1] = ['+dedicated', '1', '+set', 'com_fast_forward', '1']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    config = {key: getattr(args, key) for key in ('render', 'existing', 'selected', 'video_mode', 'profile', 'profile_detail', 'units', 'unit', 'placement', 'cohort', 'spacing', 'distance', 'reorder', 'duration')}
    config['libraryName'] = library.name
    config['accepted_definition'] = 'Member has Move order ID and Move task and a changed request identity or destination owner after submission. Retaining an old Move is insufficient.'
    config['movement_cpu_definition'] = 'Owners detail measures disjoint scheduled thinks (including other owners), pose sampling, Move timers, flow jobs and order submission; native listener orders charged explicitly. Other details omit owners and cannot establish the movement budget. Diagnostic hooks add overhead.'
    config['frame_cpu_definition'] = 'Includes the first simulation tick after initial orders, starting at 6100 ms. Instrumented setup and initial submission at 6000 ms are reported separately; subsequent submission is included in its frame.'
    env = {**os.environ, 'LD_LIBRARY_PATH': str(library.parent) + ':' + os.environ.get('LD_LIBRARY_PATH', '')}
    device = frida.get_local_device()
    pid = device.spawn(argv, cwd=str(root), env=env, stdio='pipe')
    complete, errors = [], []
    with args.output.open('w') as report, args.output.with_suffix('.log').open('wb') as log:
        def record(row):
            report.write(json.dumps(row) + '\n'); report.flush()
            if row.get('event') == 'final':
                complete.append(True)
        def output(proc, fd, data):
            if proc == pid:
                log.write(data); log.flush()
        device.on('output', output)
        session = device.attach(pid)
        session.on('detached', lambda reason, crash: record(
            {'event': 'detached', 'reason': reason, 'crash': str(crash) if crash else None}))
        def message(msg, data):
            if msg['type'] == 'error':
                errors.append(msg); record(msg)
            else:
                record(msg.get('payload', msg))
        record({'metadata': config, 'argv': argv, 'abi': abi,
                'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                'library_sha256': hashlib.sha256(library.read_bytes()).hexdigest()})
        source = 'const nativeCode=' + json.dumps(NATIVE) + ';const abi=' + json.dumps(abi) + ';const cfg=' + json.dumps(config) + ';const syms=' + json.dumps(symbols(library)) + ';const app=' + json.dumps(symbols(binary)) + ';' + SCRIPT
        script = session.create_script(source); script.on('message', message); script.load(); device.resume(pid)
        start = time.monotonic()
        try:
            while not session.is_detached and not complete and not errors and time.monotonic() - start < args.timeout:
                time.sleep(.1)
        finally:
            if complete and not errors:
                shutdown_deadline = time.monotonic() + 5
                while not session.is_detached and time.monotonic() < shutdown_deadline:
                    time.sleep(.1)
            if not session.is_detached:
                session.detach()
                try:
                    device.kill(pid)
                except frida.ProcessNotFoundError:
                    pass
            device.off('output', output)
        record({'event': 'status', 'complete': bool(complete), 'errors': errors,
                'elapsed_seconds': time.monotonic() - start})
    return 0 if complete and not errors else 1


if __name__ == '__main__':
    raise SystemExit(main())
