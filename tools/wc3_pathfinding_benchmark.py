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


def display_budget(rows):
    """A simulation burst occupies one display frame; never amortize it."""
    frames = [row for row in rows if row.get('event') == 'host_frame']
    measured = [max(row['movement_cpu_ms'], row['movement_wall_ms']) for row in frames
                if row.get('movement_cpu_ms') is not None and row.get('movement_wall_ms') is not None]
    allowance = 16 * 0.05
    over = sum(cost > allowance for cost in measured)
    return {'frame_budget_ms': 16, 'allowance_ms': allowance, 'frames': len(frames),
            'measured_frames': len(measured), 'over_budget_frames': over,
            'peak_ms': max(measured, default=None),
            'passed': bool(frames) and len(measured) == len(frames) and over == 0}


def presentation_budget(capture):
    """Count actual swap gaps, including first publication after creation."""
    frames = capture.get('frames', [])
    intervals = [row['interval_ms'] for row in frames]
    allowance = 1000 / 60
    over = sum(value > allowance for value in intervals)
    complete = (not capture.get('overflow') and capture.get('post_host_frames', 0) >= 60 and
                sum(row['phase'] == 2 for row in frames) >= 60 and
                bool(intervals) and all(math.isfinite(value) and value > 0 for value in intervals))
    return {'allowance_ms': allowance, 'frames': len(frames),
            'during_spawn_frames': sum(row['phase'] == 1 for row in frames),
            'peak_interval_ms': max(intervals, default=None),
            'over_budget_frames': over,
            'double_period_gaps': sum(value > 2 * allowance for value in intervals),
            'capture_complete': complete, 'passed': complete and over == 0}


# Observe actual swaps independently of whether the compiler inlines drawing.
PRESENTATION_NATIVE = r'''
typedef unsigned long long u64;
typedef unsigned int u32;
typedef struct { long long sec,nsec; } ts;
extern int gettime(int,ts *);
extern u32 phase[];
extern u64 rows[],clocks[];
extern void finish(void);
static u64 now(int id) { ts t;gettime(id,&t);return (u64)t.sec*1000000000ULL+t.nsec; }
void normal_enter(GumInvocationContext *ctx) { phase[4]=1; }
void normal_leave(GumInvocationContext *ctx) {
    phase[4]=0;
    if(phase[0]==2 && ++phase[2]>=60) { phase[0]=3;finish(); }
}
void swap_enter(GumInvocationContext *ctx) {
    u64 *start=gum_invocation_context_get_listener_invocation_data(ctx,16);
    start[0]=now(3);start[1]=now(1);
}
/* The SDL boundary survives local screen-function inlining. Record each actual
 * swap directly; these CPU/wall fields measure the swap, not all drawing work. */
void swap_leave(GumInvocationContext *ctx) {
    u64 *start=gum_invocation_context_get_listener_invocation_data(ctx,16);
    u64 cpu=now(3),wall=now(1),interval=clocks[0]?wall-clocks[0]:0;
    clocks[0]=wall;
    if(!phase[0] || phase[0]>=3)return;
    if(phase[1]<PRESENTATION_CAPACITY) {
        u64 *r=rows+phase[1]++*5;
        r[0]=wall;r[1]=interval;r[2]=cpu-start[0];r[3]=wall-start[1];r[4]=phase[0];
    } else phase[3]=1;
}
'''

# SV_Frame and CL_Frame belong to the same host iteration. Native callbacks
# retain every frame, including initial submission and the final render. The
# limiter sleeps after these functions and is excluded from measured work.
HOST_NATIVE = r'''
typedef unsigned int u32;
typedef unsigned long long u64;
typedef struct { long long sec,nsec; } timespec;
typedef struct { u64 server_cpu,server_wall,client_cpu,client_wall,movement_cpu,movement_wall,work_wall,interval; u32 sim,flags; } host_frame;
extern int gettime(int,timespec *);
extern void host_finish(void);
extern unsigned char *host_level[];
extern host_frame host_frames[];
extern u32 host_state[];
extern u64 host_clocks[],timing[];
static u64 clock_ns(int id) { timespec t;gettime(id,&t);return (u64)t.sec*1000000000ULL+t.nsec; }
void server_enter(void *ctx) {
    u64 now=clock_ns(1);
    host_clocks[7]=host_clocks[6]?now-host_clocks[6]:0;host_clocks[6]=now;
    host_clocks[0]=clock_ns(3);host_clocks[1]=now;
    host_clocks[2]=timing[2]+timing[8];host_clocks[3]=timing[7]+timing[9];
}
void server_leave(void *ctx) {
    if(!host_state[0]||host_state[1]>=HOST_CAPACITY)return;
    host_frame *r=&host_frames[host_state[1]];
    r->server_cpu=clock_ns(3)-host_clocks[0];r->server_wall=clock_ns(1)-host_clocks[1];
}
void client_enter(void *ctx) {
    host_clocks[4]=clock_ns(3);host_clocks[5]=clock_ns(1);
}
void client_leave(void *ctx) {
    if(!host_state[0])return;
    u64 end=clock_ns(1),cpu=clock_ns(3);
    if(host_state[1]>=HOST_CAPACITY)host_state[2]=1;
    else {
        host_frame *r=&host_frames[host_state[1]++];
        r->client_cpu=cpu-host_clocks[4];r->client_wall=end-host_clocks[5];
        r->movement_cpu=timing[2]+timing[8]-host_clocks[2];r->movement_wall=timing[7]+timing[9]-host_clocks[3];
        r->work_wall=end-host_clocks[1];r->interval=host_clocks[7];
        r->sim=host_level[0]?*(u32 *)(host_level[0]+TIME):0;
        r->flags=host_state[3];host_state[3]=0;
    }
    if(host_state[0]==2){host_state[0]=0;host_finish();}
}
'''

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
extern u32 host_state[];
#define cpu0 timing[0]
#define wall0 timing[1]
#define movement_cpu timing[2]
#define frame_movement_start timing[3]
#define movement_start timing[4]
#define movement_depth timing[5]
#define movement_wall_start timing[6]
#define movement_wall timing[7]
static u64 clock_ns(int id) { timespec t; gettime(id,&t); return (u64)t.sec*1000000000ULL+t.nsec; }
static u32 sim(void) { return *(u32 *)(game_level+TIME); }
void prime_frame(void) {
    cpu0=clock_ns(3);wall0=clock_ns(1);frame_movement_start=movement_cpu;timing[10]=timing[8];
}
void submit_order(void) {
    movement_depth++;
    u64 begin=clock_ns(3),wall_begin=clock_ns(1); u32 accepted=0;
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
    movement_wall+=clock_ns(1)-wall_begin;
    movement_depth--;
    if(state[4]<ORDER_CAPACITY)orders[state[4]++]=(order){order_cpu,sim(),COUNT,accepted,0};
    state[3]++; state[2]=sim()+REORDER;
}
void on_enter(void *context) {
    if(state[0])return;
    prime_frame();
    if(sim()>=state[2])submit_order();
}
static void complete(void) {
#if RENDERED
    host_state[0]=2;
#else
    finish();
#endif
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
    if(state[1]>=CAPACITY){state[0]=2;complete();return;}
    frames[state[1]++]=(frame){cpu,wall,sim(),advancing,velocity,(u32)((movement_cpu-frame_movement_start+timing[8]-timing[10])/1000)};
    if(sim()>=START+DURATION){state[0]=1;complete();}
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
        "run_frame": "offsetof(struct game_export,RunFrame)",
        "rusage_size": "sizeof(struct rusage)", "rusage_user": "offsetof(struct rusage,ru_utime)",
        "rusage_system": "offsetof(struct rusage,ru_stime)",
        "rusage_minor": "offsetof(struct rusage,ru_minflt)", "rusage_major": "offsetof(struct rusage,ru_majflt)",
        "rusage_voluntary": "offsetof(struct rusage,ru_nvcsw)", "rusage_involuntary": "offsetof(struct rusage,ru_nivcsw)",
    }
    for field in ("inuse", "s.number", "s.player", "s.origin2", "s.model", "class_id", "spawn_time",
                  "collision", "health.value", "svflags", "destructable", "targtype", "currentmove", "current_order_id", "goalentity", "selected",
                  "movement.velocity", "movement.fine_pose", "movement.group_id", "movement.previous_request_id", "data.Doodads", "data.UnitAbilities"):
        fields[field.replace('.', '_')] = f"offsetof(edict_t,{field})"
    lines = ['#include "games/warcraft-3/game/g_local.h"', '#include <stddef.h>', '#include <sys/resource.h>', 'int main(void) {']
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
function finishCapture(){
    if(cfg.keep_open){
        // Retain CModule storage until the game closes. Remove listeners after
        // the final native callback returns so ordinary play has no capture hooks.
        setImmediate(()=>Interceptor.detachAll());
    }else queueText(quitText);
}
const timing=Memory.alloc(128),hostState=Memory.alloc(16),hostLevel=Memory.alloc(abi.pointer_size);
timing.writeByteArray(new Uint8Array(128));hostState.writeByteArray(new Uint8Array(16));hostLevel.writePointer(ptr(0));
const hostCapacity=Math.ceil(cfg.duration/1000*120)+256,hostRows=Memory.alloc(hostCapacity*72),hostClocks=Memory.alloc(64);
hostClocks.writeByteArray(new Uint8Array(64));
let hostFinishImpl=null;
let sampler=null,sampleStop=null,sampleActive=false;
if(cfg.sampler){
    sampler=Module.load(cfg.sampler);
    new NativeFunction(sampler.getExportByName('sample_set_owner'),'void',['pointer'])(timing.add(40));
    sampleStop=new NativeFunction(sampler.getExportByName('sample_stop'),'void',[]);
}
const hostFinish=new NativeCallback(()=>{if(hostFinishImpl)hostFinishImpl();},'void',[]);
if(cfg.render){
    const host=new CModule('#define TIME '+abi.time+'\n#define HOST_CAPACITY '+hostCapacity+'\n'+hostCode,
        {gettime:Module.getGlobalExportByName('clock_gettime'),host_finish:hostFinish,host_level:hostLevel,
         host_frames:hostRows,host_state:hostState,host_clocks:hostClocks,timing});
    host.keep=[hostRows,hostState,hostClocks,hostLevel,timing,hostFinish];nativeControllers.push(host);
    Interceptor.attach(Process.mainModule.base.add(app.SV_Frame),{onEnter:host.server_enter,onLeave:host.server_leave});
    Interceptor.attach(Process.mainModule.base.add(app.CL_Frame),{onEnter:host.client_enter,onLeave:host.client_leave});
}
Process.attachModuleObserver({onAdded(module){
    if(installed||module.name!==cfg.libraryName)return;
    installed=true;
    const addr=name=>module.base.add(syms[name]);
    const globals=addr('globals'),level=addr('level');hostLevel.writePointer(level);
    sim=()=>level.add(abi.time).readU32();
    const create=new NativeFunction(addr('unit_create'),'pointer',['uint32','uint32','pointer','float']);
    const getPlayerEntity=new NativeFunction(addr('G_GetPlayerEntityByNumber'),'pointer',['uint32']);
    let playerEntity=ptr(0);
    const disabled=new NativeFunction(addr('M_UnitMoveDisabled'),'bool',['pointer']);
    const getbounds=new NativeFunction(addr('CM_GetWorldBounds'),['float','float','float','float'],[]);
    const staticLine=new NativeFunction(addr('G_MovePathLineIsPathable'),'bool',['pointer']);
    const staticPoint=new NativeFunction(addr('G_MovePathPointIsPathable'),'bool',['pointer']);
    const names=[Memory.allocUtf8String('move')],point=Memory.alloc(8);
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
        const aura_sources={};
        for(const name of ['regen_source_count','combat_source_count','slow_source_count','endurance_source_count'])
            if(syms[name]!==undefined)aura_sources[name]=addr(name).readU32();
        return {counts,static_classes:classes,edict_highwater:n,aura_sources};
    }
    let samplesDumped=false;
    function dumpSamples(){
        if(!sampler||samplesDumped)return;
        samplesDumped=true;
        if(sampleActive){sampleStop();sampleActive=false;}
        const count=new NativeFunction(sampler.getExportByName('sample_count'),'int',[])();
        const addresses=new NativeFunction(sampler.getExportByName('sample_addresses'),'pointer',[])();
        const phases=new NativeFunction(sampler.getExportByName('sample_phases'),'pointer',[])();
        const overflow=new NativeFunction(sampler.getExportByName('sample_overflow'),'int',[])();
        const tables=new Map([[module.name,syms],[Process.mainModule.name,app],...Object.entries(supportSyms)].map(([name,table])=>
            [name,Object.entries(table).sort((a,b)=>a[1]-b[1])]));
        const libc=Process.findModuleByName('libc.so.6');
        if(libc){
            // IFUNC implementations are often absent from nm. Resolve their
            // runtime entry points, and retain offsets for nearby samples.
            const names=['strcmp','strcasecmp','strncmp','strncasecmp','strlen','strchr','strrchr',
                         'memcpy','memmove','memset','memcmp','snprintf','malloc','calloc','free','strtol'];
            tables.set(libc.name,names.map(name=>[name,libc.getExportByName(name).sub(libc.base).toInt32()]).sort((a,b)=>a[1]-b[1]));
        }
        const hits={},movementHits={};
        for(let i=0;i<count;i++){
            const pc=addresses.add(i*abi.pointer_size).readPointer(),owner=Process.findModuleByAddress(pc);
            let name=owner?owner.name:'unknown';const table=tables.get(name);
            if(table){
                const offset=pc.sub(owner.base).toInt32();let lo=0,hi=table.length;
                while(lo<hi){const mid=(lo+hi)>>>1;if(table[mid][1]<=offset)lo=mid+1;else hi=mid;}
                if(lo){const distance=offset-table[lo-1][1];
                    if(owner.name!=='libc.so.6')name+='!'+table[lo-1][0];
                    else if(distance<1024)name+='!near_'+table[lo-1][0]+'+'+distance;
                    else name+='!offset_0x'+offset.toString(16);
                }
            }
            hits[name]=(hits[name]||0)+1;
            if(phases.add(i).readU8())movementHits[name]=(movementHits[name]||0)+1;
        }
        send({event:'cpu_samples',phase:cfg.sample_phase,period_us:cfg.sample_us,period_cycles:cfg.sample_cycles,owner_phase_available:!cfg.sample_cycles,count,overflow:!!overflow,hits,movement_hits:movementHits});
        if(overflow)send({event:'error',message:'CPU sample buffer exhausted'});
    }
    let finishSpawn=null,spawnDone=null;
    const spawnPhase=Memory.alloc(20);spawnPhase.writeByteArray(new Uint8Array(20));
    let presentationRows=null,presentationController=null;
    if(cfg.render&&cfg.spawn_only){
        const capacity=8192;
        presentationRows=Memory.alloc(capacity*40);
        const presentationClocks=Memory.alloc(24);presentationClocks.writeByteArray(new Uint8Array(24));
        const finish=new NativeCallback(()=>{
            if(sampleActive&&cfg.sample_phase==='post-spawn'){sampleStop();sampleActive=false;dumpSamples();}
            hostState.writeU32(0);
            const hostFrames=[];
            for(let i=0;i<hostState.add(4).readU32();i++){
                const row=hostRows.add(i*72);
                hostFrames.push({server_cpu_ms:row.readU64().toNumber()/1e6,
                    server_wall_ms:row.add(8).readU64().toNumber()/1e6,
                    client_cpu_ms:row.add(16).readU64().toNumber()/1e6,
                    client_wall_ms:row.add(24).readU64().toNumber()/1e6,
                    setup:!!row.add(68).readU32()});
            }
            send({event:'spawn_host_frames',frames:hostFrames,overflow:!!hostState.add(8).readU32()});
            const complete=finishSpawn;finishSpawn=null;complete();
            const frames=[];
            for(let i=0;i<spawnPhase.add(4).readU32();i++){
                const row=presentationRows.add(i*40);
                frames.push({interval_ms:row.add(8).readU64().toNumber()/1e6,
                    swap_cpu_ms:row.add(16).readU64().toNumber()/1e6,
                    swap_wall_ms:row.add(24).readU64().toNumber()/1e6,
                    phase:row.add(32).readU64().toNumber()});
            }
            send({event:'spawn_presentation',frames,post_host_frames:spawnPhase.add(8).readU32(),overflow:!!spawnPhase.add(12).readU32()});
            if(spawnPhase.add(12).readU32())send({event:'error',message:'Presentation capture buffer exhausted'});
            finishSpawnOnly();
        },'void',[]);
        presentationController=new CModule('#include <gum/guminterceptor.h>\n#define PRESENTATION_CAPACITY '+capacity+'\n'+presentationCode,
            {gettime:Module.getGlobalExportByName('clock_gettime'),phase:spawnPhase,rows:presentationRows,clocks:presentationClocks,finish});
        presentationController.keep=[spawnPhase,presentationRows,presentationClocks,finish];nativeControllers.push(presentationController);
        Interceptor.attach(Module.getGlobalExportByName('SDL_GL_SwapWindow'),{onEnter:presentationController.swap_enter,onLeave:presentationController.swap_leave});
        Interceptor.attach(Process.mainModule.base.add(app.CL_Frame),{onEnter:presentationController.normal_enter,onLeave:presentationController.normal_leave});
    }
    function finishSpawnOnly(){
        send({event:'final',sim:sim(),created:units.length,positions:units.map(position),
            random:[level.add(abi.random).readU32(),level.add(abi.random+4).readU32()]});
        finishCapture();
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
        const codes=cfg.unit_types.map(code=>code.charCodeAt(0)|(code.charCodeAt(1)<<8)|(code.charCodeAt(2)<<16)|(code.charCodeAt(3)<<24));
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
        const placementCpu=clock(3)-begin,added=cfg.units-existing.length;
        const spawnPoints=Memory.alloc(Math.max(1,added)*8),spawnUnits=Memory.alloc(Math.max(1,added)*abi.pointer_size);
        const spawnTypes=Memory.alloc(codes.length*4);
        codes.forEach((code,i)=>spawnTypes.add(i*4).writeU32(code>>>0));
        const spawnType=codes.length===1?(codes[0]>>>0)+'u':'types[i%'+codes.length+']';
        for(let i=existing.length;i<cfg.units;i++){
            const p=cfg.placement!=='region'?locations[i]:[seedpos[0]+(i%cols-cols/2)*cfg.spacing,seedpos[1]+(Math.floor(i/cols)-cols/2)*cfg.spacing];
            spawnPoints.add((i-existing.length)*8).writeFloat(p[0]);spawnPoints.add((i-existing.length)*8+4).writeFloat(p[1]);
        }
        let spawnProfile=null,spawnProfileRows=null,spawnProfileNames=[],spawnProfileHooks=[];
        if(cfg.profile_spawn){
            spawnProfileNames=['SP_SpawnUnit','SP_SpawnFreshUnit','SpawnUnit','G_RegisterUnitSounds','S_InitUnitPosition',
                'G_ApplyPlayerUpgradesToUnit','G_RegisterModel','G_LoadShadowTexture','unit_stand',
                'unit_dispatch_engine_event_abilities','G_BindEntityData','G_ActorHasSkill',
                'G_GetPlayerTechResearchedLevel','G_InitEdict','SP_CallSpawn','FS_FileExists',
                'SV_SoundIndexAlias'];
            if(cfg.profile_detail==='owners')spawnProfileNames=[
                'SP_CallSpawn','SP_SpawnUnit','SP_SpawnFreshUnit','SpawnUnit','SP_monster_unit','S_InitUnitPosition','unit_stand',
                'G_InitEdict','G_BindEntityData','G_BotUnitReady','G_ActivateUnitFood',
                'S_UnitAbilityEvent','S_InitFreshUnitAbilities','unit_dispatch_authored_abilities_mode',
                'S_UnitAbilityMoveLeave','unit_setmove','S_UnitMoveSpeed',
                'S_MoveSpeedBonus','G_InitStockSlots','S_CargoInitUnit','S_GoldMineInitUnit',
                'S_UnitTypeIsGoldMine','S_UnitTypeReturnsGold','G_RecomputeHeroStats',
                'G_ApplyPlayerUpgradesToUnit','M_CheckGround','G_SetUnitAnimation',
                'unit_register_visuals','unit_register_sounds','SV_LinkEntity'];
            spawnProfileNames=spawnProfileNames.filter(name=>syms[name]!==undefined||app[name]!==undefined);
            spawnProfileRows=Memory.alloc(spawnProfileNames.length*16);
            spawnProfileRows.writeByteArray(new Uint8Array(spawnProfileNames.length*16));
            let code='#include <gum/guminterceptor.h>\n'+
                'typedef unsigned long long u64;typedef struct{long long sec,nsec;} timespec;'+
                'extern int gettime(int,timespec *);extern u64 profiles[];'+
                'static u64 now(void){timespec t;gettime(3,&t);return (u64)t.sec*1000000000ULL+t.nsec;}';
            spawnProfileNames.forEach((name,i)=>{code+=
                'void enter_'+i+'(GumInvocationContext *ctx){u64 *start=gum_invocation_context_get_listener_invocation_data(ctx,sizeof(u64));*start=now();profiles['+(i*2)+']++;}'+
                'void leave_'+i+'(GumInvocationContext *ctx){u64 *start=gum_invocation_context_get_listener_invocation_data(ctx,sizeof(u64));profiles['+(i*2+1)+']+=now()-*start;}';});
            spawnProfile=new CModule(code,{gettime:Module.getGlobalExportByName('clock_gettime'),profiles:spawnProfileRows});
            spawnProfile.keep=[spawnProfileRows];nativeControllers.push(spawnProfile);
            spawnProfileNames.forEach((name,i)=>spawnProfileHooks.push(Interceptor.attach(syms[name]!==undefined?addr(name):Process.mainModule.base.add(app[name]),
                {onEnter:spawnProfile['enter_'+i],onLeave:spawnProfile['leave_'+i]})));
            Interceptor.flush();
        }
        const spawnTiming=Memory.alloc(32);spawnTiming.writeByteArray(new Uint8Array(32));
        const spawnUsage=Memory.alloc(abi.rusage_size*2);spawnUsage.writeByteArray(new Uint8Array(abi.rusage_size*2));
        spawnDone=Memory.alloc(4);spawnDone.writeU32(0);
        const spawnController=new CModule('typedef unsigned int u32;typedef unsigned long long u64;typedef struct{long long sec,nsec;} timespec;'+
            'extern int gettime(int,timespec *);extern int getusage(int,void *);extern unsigned char usage[];extern void *create(u32,u32,void *,float);extern float points[];extern void *units[];extern u64 timing[],profiles[];extern u32 done[],phase[],types[];extern void *run_slot[];extern void original_frame(void);'+
            (sampler&&['spawn','post-spawn'].includes(cfg.sample_phase)?'extern int sample_start(int,int);extern void sample_stop(void);':'')+
            'static u64 now(int id){timespec t;gettime(id,&t);return (u64)t.sec*1000000000ULL+t.nsec;}'+
            'void spawn_batch(void){if(done[0])return;for(u32 i=0;i<'+(spawnProfileNames.length*2)+';i++)profiles[i]=0;'+
            (sampler&&cfg.sample_phase==='spawn'?'if(!sample_start(262144,'+(cfg.sample_cycles||cfg.sample_us)+')){done[0]=2;return;}':'')+
            'if(getusage(1,usage)){done[0]=2;return;}phase[0]=1;u64 cpu=now(3),wall=now(1);for(u32 i=0;i<'+added+';i++){units[i]=create(0,'+spawnType+',points+i*2,0);if(i==0){timing[2]=now(3)-cpu;timing[3]=now(1)-wall;}}timing[0]=now(3)-cpu;timing[1]=now(1)-wall;if(getusage(1,usage+'+abi.rusage_size+')){done[0]=2;return;}'+
            (sampler&&cfg.sample_phase==='spawn'?'sample_stop();':'')+
            (sampler&&cfg.sample_phase==='post-spawn'?'if(!sample_start(262144,'+(cfg.sample_cycles||cfg.sample_us)+')){done[0]=2;return;}':'')+
            'done[0]=1;phase[0]=2;}'+
            'void spawn_frame(void){run_slot[0]=(void *)original_frame;spawn_batch();original_frame();}',
            {gettime:Module.getGlobalExportByName('clock_gettime'),getusage:Module.getGlobalExportByName('getrusage'),usage:spawnUsage,types:spawnTypes,create:addr('unit_create'),points:spawnPoints,units:spawnUnits,timing:spawnTiming,done:spawnDone,phase:spawnPhase,
                profiles:spawnProfileRows||spawnTiming,run_slot:globals.add(abi.run_frame),original_frame:globals.add(abi.run_frame).readPointer(),
                ...(sampler&&['spawn','post-spawn'].includes(cfg.sample_phase)?{sample_start:sampler.getExportByName(cfg.sample_cycles?'sample_start_cycles':'sample_start'),sample_stop:sampler.getExportByName('sample_stop')}:{})});
        spawnController.keep=[spawnTypes,spawnPoints,spawnUnits,spawnTiming,spawnDone,spawnUsage];nativeControllers.push(spawnController);
        if(sampler&&['spawn','post-spawn'].includes(cfg.sample_phase)){
            sampleActive=true;
        }
        function finishSetup(){
        if(spawnDone.readU32()!==1)throw new Error('Spawn batch or CPU sampler failed');
        spawnProfileHooks.forEach(hook=>hook.detach());
        if(sampleActive&&cfg.sample_phase==='spawn'){sampleActive=false;dumpSamples();}
        if(spawnProfile)send({event:'spawn_profile',functions:Object.fromEntries(spawnProfileNames.map((name,i)=>{
            const row=spawnProfileRows.add(i*16);return [name,{calls:row.readU64().toNumber(),cpu_ms:row.add(8).readU64().toNumber()/1e6}];}))});
        const usageDelta=offset=>spawnUsage.add(abi.rusage_size+offset).readS64().toNumber()-spawnUsage.add(offset).readS64().toNumber();
        const usageTime=offset=>usageDelta(offset)*1000+usageDelta(offset+8)/1000;
        send({event:'spawn',created:added,cpu_ms:spawnTiming.readU64().toNumber()/1e6,wall_ms:spawnTiming.add(8).readU64().toNumber()/1e6,placement_cpu_ms:placementCpu,first_cpu_ms:spawnTiming.add(16).readU64().toNumber()/1e6,first_wall_ms:spawnTiming.add(24).readU64().toNumber()/1e6,
            user_cpu_ms:usageTime(abi.rusage_user),system_cpu_ms:usageTime(abi.rusage_system),
            minor_faults:usageDelta(abi.rusage_minor),major_faults:usageDelta(abi.rusage_major),
            voluntary_switches:usageDelta(abi.rusage_voluntary),involuntary_switches:usageDelta(abi.rusage_involuntary)});
        for(let i=existing.length;i<cfg.units;i++){
            const u=spawnUnits.add((i-existing.length)*abi.pointer_size).readPointer();
            if(u.isNull())throw new Error('CreateUnit failed at '+i);
            units.push(u);origins.push(position(u));previous.push(position(u));directions.push(vectors[i]||[cfg.distance,cfg.distance*.5]);
        }
        started=sim();spawned=true;
        if(cfg.selected){
            playerEntity=getPlayerEntity(0);
            if(playerEntity.isNull())throw new Error('Player zero has no command entity');
            const setSelection=new NativeFunction(addr('G_SetEntitySelectionMask'),'uint',['pointer','uint']);
            for(let i=0;i<globals.add(abi.num_edicts).readU32();i++)setSelection(edicts().add(i*abi.edict_size),0);
            units.forEach(u=>setSelection(u,1));
        }
        send({event:'setup',sim:sim(),unit_types:cfg.unit_types,requested:cfg.units,created:units.length-existing.length,existing:existing.length,controlled:units.length,cpu_ms:clock(3)-begin,...census()});
        }
        if(cfg.profile_spawn||cfg.spawn_only){
            // Execute once on the simulation thread outside Gum's listener
            // context. Restore the export before calling the original frame.
            globals.add(abi.run_frame).writePointer(spawnController.spawn_frame);
            if(cfg.spawn_only&&cfg.render){hostState.writeU32(1);hostState.add(12).writeU32(1);}
            finishSpawn=finishSetup;
            return false;
        }
        new NativeFunction(spawnController.spawn_batch,'void',[])();
        finishSetup();
        return true;
    }
    let controller=null,nativeHook=null,finishFirstFrame=null;
    function nativeRun(){
        if(cfg.profile&&cfg.profile_detail!=='owners')Interceptor.attach(addr('G_BindEntityData'),{onEnter(args){this.unit=args[0];},onLeave(){
            const row=this.unit.add(abi.data_UnitAbilities).readPointer();
            const list=row.isNull()?ptr(0):row.add(abi.ability_list).readPointer();
            send({event:'bind',sim:sim(),class_id:this.unit.add(abi.class_id).readU32(),abilities:list.isNull()?null:list.readUtf8String()});
        }});
        const capacity=Math.ceil(cfg.duration/100)+128,records=Memory.alloc(capacity*32),orderCapacity=Math.ceil(cfg.duration/cfg.reorder)+4,orderRows=Memory.alloc(orderCapacity*24);
        const plan=Memory.alloc(units.length*32),state=Memory.alloc(20);
        state.writeByteArray(new Uint8Array(20));state.add(8).writeU32(nextOrder);state.add(12).writeU32(orders);
        units.forEach((u,i)=>{const row=plan.add(i*32);row.writePointer(u);
            row.add(8).writeFloat(origins[i][0]);row.add(12).writeFloat(origins[i][1]);
            row.add(16).writeFloat(previous[i][0]);row.add(20).writeFloat(previous[i][1]);
            row.add(24).writeFloat(directions[i][0]);row.add(28).writeFloat(directions[i][1]);});
        // The scheduled pass includes other ability owners: this is a conservative
        // bound. Avoid a listener/clock call for every bound member's no-op think.
        const ownerNames=['M_RunScheduledThinks','S_RunMoveTimers','M_SamplePoses','CM_ProcessPathJobs','CM_BeginPathJobs','CM_FinishPathJobs',
            'G_IssueGroupPointOrder','move_selectlocation'];
        const pipelineNames=['move_run_group_updates','move_group_route','move_group_decide',
            'move_group_regroup','move_repulse_owner_update','unit_commit_motion',
            'move_find_route','move_adaptive_progress','move_retry_fine',
            'move_allocate_group_id','Waypoint_add','unit_issueorder',
            'S_RecoverStoppedUnitPosition','G_UnitMoveGroupDestination','CM_ProcessPathJobs','CM_BeginPathJobs','CM_FinishPathJobs','S_WaygateBuildEdges'];
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
        const finishImpl=()=>{
            dumpSamples();
            for(let i=0;i<hostState.add(4).readU32();i++){const row=hostRows.add(i*72);
                const value=j=>row.add(j*8).readU64().toNumber()/1e6;
                send({event:'host_frame',frame:i,sim:row.add(64).readU32(),setup:!!row.add(68).readU32(),
                    server_cpu_ms:value(0),server_wall_ms:value(1),client_cpu_ms:value(2),client_wall_ms:value(3),
                    movement_cpu_ms:cfg.profile&&cfg.profile_detail==='owners'?value(4):null,
                    movement_wall_ms:cfg.profile&&cfg.profile_detail==='owners'?value(5):null,
                    work_wall_ms:value(6),interval_ms:value(7)});
            }
            if(hostState.add(8).readU32())send({event:'error',message:'Host frame buffer exhausted'});
            const count=state.add(4).readU32();
            for(let i=0;i<count;i++){const row=records.add(i*32);send({event:'simulation',frame:i,
                sim:row.add(16).readU32(),cpu_ms:row.readU64().toNumber()/1e6,wall_ms:row.add(8).readU64().toNumber()/1e6,
                movement_cpu_ms:cfg.profile_detail==='owners'&&cfg.profile?row.add(28).readU32()/1000:null,
                advancing:row.add(20).readU32(),velocity:row.add(24).readU32()});}
            for(let i=0;i<state.add(16).readU32();i++){const row=orderRows.add(i*24);send({event:'order',
                sim:row.add(8).readU32(),requested:row.add(12).readU32(),accepted:row.add(16).readU32(),cpu_ms:row.readU64().toNumber()/1e6});}
            send({event:'flow_worker',cpu_ms:timing.add(64).readU64().toNumber()/1e6,wall_ms:timing.add(72).readU64().toNumber()/1e6,calls:timing.add(96).readU64().toNumber()});
            if(profileNames.length)send({event:'profile',endurance_discoveries:diagnostics.readU64().toNumber(),functions:Object.fromEntries(profileNames.map((name,i)=>{
                const row=profiles.add(i*24);return [name,{calls:row.add(8).readU64().toNumber(),cpu_ms:row.add(16).readU64().toNumber()/1e6}];
            }))});
            send({event:state.readU32()===1?'final':'error' ,sim:sim(),frames:count,positions:units.map(position),
                members:units.map(u=>{const goal=u.add(abi.goalentity).readPointer();return {number:u.add(abi.s_number).readU32(),
                    order_id:u.add(abi.current_order_id).readU32(),walk:u.add(abi.currentmove).readPointer().equals(addr('move_move_walk')),
                    group:u.add(abi.movement_group_id).readU32(),goal:goal.isNull()?null:position(goal)};}),
                random:[level.add(abi.random).readU32(),level.add(abi.random+4).readU32()]});
            // Let shutdown flush sampling profiles; all measured work is over.
            finishCapture();
        };
        hostFinishImpl=finishImpl;
        const finish=new NativeCallback(finishImpl,'void',[]);
        const defines={TIME:abi.time,COUNT:units.length,GROUP:cfg.cohort,REQUEST:abi.request_size,MEMBER:abi.member_size,
            SPAWN_MEMBER:abi.member_spawn,SPAWN:abi.spawn_time,REQ_COUNT:abi.request_count,REQ_ORDER:abi.order_id,
            ORDER_ID:orderid,REQ_NAME:abi.order,REQ_POINT:abi.point,REORDER:cfg.reorder,DISTANCE:cfg.distance,
            SELECTED_ORDER:cfg.selected?1:0,RENDERED:cfg.render?1:0,
            POSITION:abi.s_origin2,VELOCITY:abi.movement_velocity,CURRENT_ORDER:abi.current_order_id,CURRENT_MOVE:abi.currentmove,
            PREVIOUS_REQUEST:abi.movement_previous_request_id,GOAL:abi.goalentity,CAPACITY:capacity,ORDER_CAPACITY:orderCapacity,START:started,DURATION:cfg.duration};
        let code=Object.entries(defines).map(([key,value])=>'#define '+key+' '+value+'\n').join('');
        code+='extern unsigned char order_name[],move_walk[];\n'+nativeCode;
        code+='\n#include <gum/guminterceptor.h>\nextern unsigned long long profiles[],diagnostics[];extern unsigned char endurance_dirty[];extern unsigned int endurance_generation[],ability_generation[];\n';
        profileNames.forEach((name,i)=>{const movement=ownerNames.includes(name);code+=
            'void enter_'+i+'(GumInvocationContext *ctx){u64 *start=gum_invocation_context_get_listener_invocation_data(ctx,sizeof(u64));*start=0;'+
            (name==='CAbilityMove'?'if((unsigned long)gum_invocation_context_get_nth_argument(ctx,1)!='+abi.ability_owner_update+')return;':'')+
            '*start=clock_ns(3);profiles['+(i*3+1)+']++;'+
            (movement?'if(!movement_depth++){movement_start=*start;movement_wall_start=clock_ns(1);}':'')+
            (name==='endurance_prepare'?'if(*endurance_dirty||*endurance_generation!=*ability_generation)diagnostics[0]++;':'')+'}\n'+
            'void leave_'+i+'(GumInvocationContext *ctx){u64 start=*(u64 *)gum_invocation_context_get_listener_invocation_data(ctx,sizeof(u64));'+
            'if(start){u64 end=clock_ns(3);profiles['+(i*3+2)+']+=end-start;'+
            (movement?'if(!--movement_depth){movement_cpu+=end-movement_start;movement_wall+=clock_ns(1)-movement_wall_start;}':'')+'}}\n';});
        // The static frontier runs outside the main-thread owner union. Record
        // its actual CPU separately; inline execution is already in Begin's
        // union and must not be charged twice. The deterministic join completes
        // these writes before frame readers access the counters.
        code+='void worker_enter(GumInvocationContext *ctx){u64 *start=gum_invocation_context_get_listener_invocation_data(ctx,2*sizeof(u64));start[0]=0;'+
            'if(gum_invocation_context_get_thread_id(ctx)==timing[11])return;start[0]=clock_ns(3);start[1]=clock_ns(1);}' +
            'void worker_leave(GumInvocationContext *ctx){u64 *start=gum_invocation_context_get_listener_invocation_data(ctx,2*sizeof(u64));'+
            'if(start[0]){timing[8]+=clock_ns(3)-start[0];timing[9]+=clock_ns(1)-start[1];timing[12]++;}}';
        controller=new CModule(code,{gettime:Module.getGlobalExportByName('clock_gettime'),issue:addr('G_IssueGroupPointOrder'),
            select_point:addr('move_selectlocation'),client_entity:cfg.selected?playerEntity:state,
            finish,game_level:level,movers:plan,frames:records,orders:orderRows,state,host_state:hostState,timing,profiles,diagnostics,endurance_dirty:syms.endurance_dirty===undefined?diagnostics:addr('endurance_dirty'),endurance_generation:syms.endurance_generation===undefined?diagnostics:addr('endurance_generation'),ability_generation:addr('ability_data_generation'),order_name:names[0],move_walk:addr('move_move_walk')});
        // Keep every buffer and callback alive until the native controller retires.
        controller.keep=[records,orderRows,plan,state,timing,profiles,diagnostics,finish,names];
        nativeControllers.push(controller);
        profileNames.forEach((name,i)=>Interceptor.attach(addr(name),{onEnter:controller['enter_'+i],onLeave:controller['leave_'+i]}));
        if(cfg.profile && syms.path_job_run!==undefined)Interceptor.attach(addr('path_job_run'),{onEnter:controller.worker_enter,onLeave:controller.worker_leave});
        nativeHook=Interceptor.attach(addr('G_RunFrame'),{onEnter:controller.on_enter,onLeave:controller.on_leave});
        // This G_RunFrame invocation has already entered the original listener.
        // Seed its clock after setup and finish it through that listener so the
        // first actual path-search tick is measured, even if Gum only installs
        // the new frame listener for subsequent invocations.
        finishFirstFrame=new NativeFunction(controller.on_leave,'void',['pointer']);
        if(cfg.render){hostState.writeU32(1);hostState.add(12).writeU32(1);}
        if(sampler&&(cfg.sample_phase==='movement'||cfg.sample_phase==='orders')){
            if(!new NativeFunction(sampler.getExportByName(cfg.sample_cycles?'sample_start_cycles':'sample_start'),'int',['int','int'])(262144,cfg.sample_cycles||cfg.sample_us))throw new Error('Cannot start main-thread CPU sampler');
            sampleActive=true;
        }
        new NativeFunction(controller.submit_order,'void',[])();
        if(sampleActive&&cfg.sample_phase==='orders')dumpSamples();
        new NativeFunction(controller.prime_frame,'void',[])();
    }
    let frameHook=null;
    let affinityApplied=false;
    if(cfg.cpu!==null && syms.path_worker_main!==undefined){
        // Threads created after main is pinned inherit its CPU mask. Restore
        // the process's original allowed set at the worker's entry point.
        const workerMask=Memory.alloc(128);workerMask.writeByteArray(new Uint8Array(128));
        cfg.allowed_cpus.forEach(cpu=>{const byte=workerMask.add(cpu>>>3);byte.writeU8(byte.readU8()|(1<<(cpu&7)));});
        const setAffinity=new NativeFunction(Module.getGlobalExportByName('sched_setaffinity'),'int',['int','ulong','pointer']);
        Interceptor.attach(addr('path_worker_main'),{onEnter(){if(setAffinity(0,128,workerMask))throw new Error('Cannot restore path worker CPU affinity');}});
        nativeControllers.push(workerMask);
    }
    frameHook=Interceptor.attach(addr('G_RunFrame'),{onEnter(){
        timing.add(88).writeU64(Process.getCurrentThreadId());
        if(cfg.cpu!==null&&!affinityApplied){
            const mask=Memory.alloc(128);mask.writeByteArray(new Uint8Array(128));
            mask.add(cfg.cpu>>>3).writeU8(1<<(cfg.cpu&7));
            if(new NativeFunction(Module.getGlobalExportByName('sched_setaffinity'),'int',['int','ulong','pointer'])(0,128,mask))
                throw new Error('Cannot pin simulation thread to CPU '+cfg.cpu);
            affinityApplied=true;
        }
        if(!spawned&&sim()>=6000){
            if(finishSpawn){
                if(!spawnDone.readU32())return;
                if(cfg.spawn_only&&cfg.render){spawned=true;frameHook.detach();return;}
                const finish=finishSpawn;finishSpawn=null;finish();
                if(cfg.spawn_only){frameHook.detach();finishSpawnOnly();return;}
                nativeRun();
            }else if(setup())nativeRun();
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
    parser.add_argument('--unit-types', help='Comma-separated rawcodes, cycled in creation order; preparation remains timed')
    parser.add_argument('--spacing', type=float, default=96)
    parser.add_argument('--placement', choices=('map', 'region', 'corridor'), default='map', help='Map-wide traversable cells or a congested square around the starting army')
    parser.add_argument('--cohort', type=int, default=12, help='Units per public group order, 1..12')
    parser.add_argument('--distance', type=float, default=768)
    parser.add_argument('--reorder', type=int, default=4000)
    parser.add_argument('--duration', type=int, default=20000, help='Measured simulation milliseconds')
    parser.add_argument('--timeout', type=float, default=180, help='Wall-clock limit including map load and spawn')
    parser.add_argument('--video-mode', type=int, default=10, help='Renderer mode index; default 10 is 1920x1080')
    parser.add_argument('--render', action='store_true')
    parser.add_argument('--keep-open', action='store_true', help='Rendered runs: finish recording, then leave the game open until you close it')
    parser.add_argument('--interactive', action='store_true', help='Spawn idle units for manual control; implies --render --keep-open --spawn-only and never submits benchmark orders')
    parser.add_argument('--existing',action='store_true',help='Use the starting player-zero army first, then add stock units to the requested count')
    parser.add_argument('--selected',action='store_true',help='Issue through the selected-unit command owner (requires at most one cohort)')
    parser.add_argument('--profile', action='store_true', help='Coarse nested CPU timers; use separate runs from acceptance timing')
    parser.add_argument('--profile-spawn', action='store_true', help='Diagnostic native creation-stage timers; adds interception overhead')
    parser.add_argument('--spawn-only', action='store_true', help='Measure public creation without Move; rendered runs capture actual presentation through 60 subsequent frames')
    parser.add_argument('--sample-us', type=int, default=0, help='Diagnostic main-thread CPU sampling period in microseconds; zero disables it')
    parser.add_argument('--sample-cycles', type=int, default=0, help='Linux: diagnostic hardware CPU-cycle sampling period; cannot combine with --sample-us')
    parser.add_argument('--sample-phase', choices=('movement','spawn','orders','post-spawn'), default='movement')
    parser.add_argument('--path-threads', type=int, choices=(0,1), default=1, help='Use the deterministic flow worker or the inline single-core path')
    parser.add_argument('--path-scheduler', choices=('responsive', 'retail'), default='responsive', help='Saved fine-search admission policy: demand-scaled grants or retail fixed limits')
    parser.add_argument('--cpu', type=int, help='Linux: pin only the simulation/render thread to this allowed CPU; keep worker affinity unchanged')
    parser.add_argument('--profile-detail', choices=('owners','pipeline','coarse','fine'), default='coarse',help='owners bounds movement using disjoint scheduled, sample, timer and order costs; pipeline and other modes include nested diagnostic timers')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.interactive:
        args.render = args.keep_open = args.spawn_only = True
    if args.keep_open and not args.render:
        parser.error('--keep-open requires --render')
    if not 1 <= args.units <= 4096:
        parser.error('--units must be 1..4096')
    if not 1 <= args.cohort <= 12 or any(not math.isfinite(v) or v <= 0 for v in (args.spacing,args.distance,args.timeout)) or args.reorder <= 0 or not 1 <= args.duration <= 3600000:
        parser.error('Finite positive spacing, distance, timeout and reorder required; cohort 1..12 and duration 1..3600000')
    if len(args.unit) != 4 or not args.unit.isascii():
        parser.error('--unit must be a four-character ASCII rawcode')
    unit_types = args.unit_types.split(',') if args.unit_types is not None else [args.unit]
    if any(len(code) != 4 or not code.isascii() or any(ord(c) < 32 or ord(c) > 126 for c in code) for code in unit_types):
        parser.error('--unit-types must contain comma-separated four-character ASCII rawcodes')
    if args.selected and args.units>args.cohort:
        parser.error('--selected requires units no greater than cohort')
    if args.cpu is not None and (not hasattr(os, 'sched_getaffinity') or args.cpu not in os.sched_getaffinity(0) or args.cpu >= 1024):
        parser.error('--cpu must name an allowed Linux CPU below 1024')
    if args.sample_cycles < 0 or args.sample_cycles > 2147483647 or (args.sample_cycles and args.sample_us):
        parser.error('Nonnegative --sample-cycles below 2^31 required; choose one sampling backend')
    if args.sample_us and not 100 <= args.sample_us <= 1000000:
        parser.error('--sample-us must be zero or 100..1000000')
    if args.sample_phase == 'post-spawn' and not (args.spawn_only and args.render):
        parser.error('--sample-phase post-spawn requires --spawn-only --render')
    root = Path(__file__).resolve().parents[1]
    binary, library = args.binary.resolve(), args.library.resolve()
    for option, path in (('--binary', binary), ('--library', library)):
        if not path.is_file():
            parser.error(f'{option} file not found: {path}. Build the explicit openwarcraft3 target for these output paths first')
    try:
        import frida
        if not hasattr(frida, 'get_local_device'):
            raise ImportError('Frida runtime is absent')
    except ImportError as error:
        parser.error('Run with a Python environment containing the Frida package: ' + str(error))
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
            '+set', 'wc3_path_threads', str(args.path_threads), '+set', 'wc3_path_scheduler', args.path_scheduler,
            '+com_frame_limit', str((6000 + args.duration) // 100 + 12), '+map', args.map]
    if args.render:
        argv[1:1] = ['+set', 'vid_native', '0', '+set', 'vid_fullscreen', '0', '+set', 'vid_mode', str(args.video_mode),
                     '+set', 'com_maxfps', '60', '+set', 'r_vsync', '0']
        argv[argv.index('+com_frame_limit') + 1] = str((6000 + args.duration) * 60 // 1000 + 600)
        if args.keep_open:
            argv[argv.index('+com_frame_limit') + 1] = '0'
    else:
        argv[1:1] = ['+dedicated', '1', '+set', 'com_fast_forward', '1']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    config = {key: getattr(args, key) for key in ('render', 'keep_open', 'interactive', 'existing', 'selected', 'video_mode', 'profile', 'profile_detail', 'units', 'unit', 'placement', 'cohort', 'spacing', 'distance', 'reorder', 'duration', 'cpu', 'path_threads', 'path_scheduler', 'profile_spawn', 'spawn_only')}
    config['allowed_cpus'] = sorted(os.sched_getaffinity(0)) if args.cpu is not None else []
    config['unit_types'] = unit_types
    config['libraryName'] = library.name
    config['accepted_definition'] = 'Member has Move order ID and Move task and a changed request identity or destination owner after submission. Retaining an old Move is insufficient.'
    config['movement_cpu_definition'] = 'Owners detail measures disjoint scheduled thinks (including other owners), pose sampling, Move timers, flow jobs and order submission; native listener orders charged explicitly. Worker CPU is added to the main-thread union; wall cost is a conservative sum of worker and main owner intervals, which may overlap. Inline work is counted once. Other details omit owners and cannot establish the movement budget. Diagnostic hooks add overhead.'
    config['frame_cpu_definition'] = 'host_frame combines SV_Frame and CL_Frame in one display iteration, excluding rate limiter sleep. Movement includes initial native submission and the first simulation tick in that same host frame, plus all subsequent commands. Setup is flagged; setup overhead is excluded only from the movement counters, never from movement budget acceptance. simulation rows remain 100-ms ticks, not display frames.'
    config['frame_budget_ms'] = 16
    config['movement_allowance_ms'] = 0.8
    config['spawn_cpu_definition'] = 'CPU and wall cost of every synchronous public CreateUnit in the native batch. Placement fixture work is separate. Rendered batches include checkpoint presentation; hooks and CPU sampling add diagnostic overhead and cannot establish raw creation acceptance.'
    sampler_directory = tempfile.TemporaryDirectory(prefix='wc3-cpu-sampler-') if args.sample_us or args.sample_cycles else None
    config['sample_us'] = args.sample_us
    config['sample_cycles'] = args.sample_cycles
    config['sample_phase'] = args.sample_phase
    support_symbols = {}
    if sampler_directory:
        support_libraries = [library.parent / name for name in ('libshared.so', 'libsheet.so', 'libjass.so', 'librenderer.so', 'libmenu.so')]
        support_symbols = {path.name: symbols(path) for path in support_libraries if path.is_file()}
        config['sample_libraries'] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                                    for path in support_libraries if path.is_file()}
        sampler_path = Path(sampler_directory.name) / 'sampler.so'
        subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter', '-fPIC', '-shared',
                        str(root / 'tools/wc3_cpu_sampler.c'), '-o', str(sampler_path), '-lrt'], check=True)
        config['sampler'] = str(sampler_path)
    env = {**os.environ, 'LD_LIBRARY_PATH': str(library.parent) + ':' + os.environ.get('LD_LIBRARY_PATH', '')}
    device = frida.get_local_device()
    pid = device.spawn(argv, cwd=str(root), env=env, stdio='pipe')
    complete, errors, host_frames, presentation = [], [], [], []
    with args.output.open('w') as report, args.output.with_suffix('.log').open('wb') as log:
        def record(row):
            report.write(json.dumps(row) + '\n'); report.flush()
            if row.get('event') == 'error':
                errors.append(row)
            if row.get('event') == 'host_frame':
                host_frames.append(row)
            if row.get('event') == 'spawn_presentation':
                presentation.append(row)
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
                payload = msg.get('payload', msg)
                record(payload)
        record({'metadata': config, 'argv': argv, 'abi': abi,
                'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                'library_sha256': hashlib.sha256(library.read_bytes()).hexdigest()})
        source = 'const presentationCode=' + json.dumps(PRESENTATION_NATIVE) + ';const hostCode=' + json.dumps(HOST_NATIVE) + ';const nativeCode=' + json.dumps(NATIVE) + ';const abi=' + json.dumps(abi) + ';const cfg=' + json.dumps(config) + ';const syms=' + json.dumps(symbols(library)) + ';const app=' + json.dumps(symbols(binary)) + ';const supportSyms=' + json.dumps(support_symbols) + ';' + SCRIPT
        script = session.create_script(source); script.on('message', message); script.load(); device.resume(pid)
        start = time.monotonic()
        try:
            while not session.is_detached and not complete and not errors and time.monotonic() - start < args.timeout:
                time.sleep(.1)
            if args.keep_open and complete and not errors:
                record({'event': 'kept_open', 'pid': pid})
                print(('Interactive demo ready; units have no benchmark orders. ' if args.interactive else
                       'Capture complete; game remains open. ') +
                      'Close the game or press Ctrl+C to finish.', flush=True)
                while not session.is_detached and not errors:
                    time.sleep(.1)
        except KeyboardInterrupt:
            record({'event': 'interrupted', 'capture_complete': bool(complete)})
        finally:
            if complete and not errors and not args.keep_open:
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
        if args.spawn_only:
            if args.render:
                budget = presentation_budget(presentation[0] if len(presentation) == 1 else {})
                budget['capture_complete'] = budget['capture_complete'] and bool(complete) and not errors
                budget['passed'] = budget['passed'] and budget['capture_complete']
                record({'event': 'presentation_budget', **budget})
        else:
            budget = display_budget(host_frames)
            budget['capture_complete'] = bool(complete) and not errors
            budget['passed'] = budget['passed'] and budget['capture_complete']
            record({'event': 'display_budget', **budget})
        record({'event': 'status', 'complete': bool(complete), 'errors': errors,
                'elapsed_seconds': time.monotonic() - start})
    if sampler_directory:
        sampler_directory.cleanup()
    return 0 if complete and not errors else 1


if __name__ == '__main__':
    raise SystemExit(main())
