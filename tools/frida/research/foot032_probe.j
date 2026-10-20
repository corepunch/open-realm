globals
 timer udg_Foot032Timer=null
 integer udg_Foot032Tick=0
 integer udg_Foot032Step=0
 unit array udg_Foot032Unit
 destructable array udg_Foot032Wall
 real array udg_Foot032X
 real array udg_Foot032Y
endglobals
// FOOT-03.2 live probe: a Footman (unit category ca) overlapping an LTcr crate footprint
// (widget pathing regions) at two sites; site 0 removes the unit first, site 1 the crate first.
// Public JASS output only; the read-only observer brackets every action with these markers.
function Foot032Mark takes string label returns nothing
 call Preload("FOOT032 tick="+I2S(udg_Foot032Tick)+" step="+I2S(udg_Foot032Step)+" label="+label)
endfunction
function Foot032Units takes integer s returns string
 local string a="none"
 if udg_Foot032Unit[s]!=null then
  set a=R2S(GetUnitX(udg_Foot032Unit[s]))+","+R2S(GetUnitY(udg_Foot032Unit[s]))
 endif
 return a
endfunction
// Endpoint probes at the shared cell centre: ground SetUnitPosition, item admission, flyer.
function Foot032Probe takes integer s,string phase returns nothing
 local real x=udg_Foot032X[s]-16.0
 local real y=udg_Foot032Y[s]-16.0
 local unit p
 local item i
 call Foot032Mark("begin-probe site="+I2S(s)+" phase="+phase)
 set p=CreateUnit(Player(0),'hfoo',@STAGE_X@,@STAGE_Y@,0.0)
 call SetUnitPosition(p,x,y)
 call Foot032Mark("probe-ground site="+I2S(s)+" phase="+phase+" x="+R2S(GetUnitX(p))+" y="+R2S(GetUnitY(p)))
 call RemoveUnit(p)
 set i=CreateItem('ratf',x,y)
 call Foot032Mark("probe-item site="+I2S(s)+" phase="+phase+" x="+R2S(GetItemX(i))+" y="+R2S(GetItemY(i)))
 call RemoveItem(i)
 set p=CreateUnit(Player(0),'hgry',@STAGE_X@,@STAGE_Y@,0.0)
 call SetUnitPosition(p,x,y)
 call Foot032Mark("probe-flyer site="+I2S(s)+" phase="+phase+" x="+R2S(GetUnitX(p))+" y="+R2S(GetUnitY(p)))
 call RemoveUnit(p)
 call Foot032Mark("end-probe site="+I2S(s)+" phase="+phase+" unit="+Foot032Units(s))
 set p=null
 set i=null
endfunction
function Foot032Tick takes nothing returns nothing
 local integer s
 local integer k
 set udg_Foot032Tick=udg_Foot032Tick+1
 if ModuloInteger(udg_Foot032Tick,5)!=0 then
  return
 endif
 set s=udg_Foot032Step/6
 set k=ModuloInteger(udg_Foot032Step,6)
 if s>=2 then
  call Foot032Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_Foot032Timer)
  return
 endif
 if k==0 then
  call Foot032Mark("begin-create-wall site="+I2S(s))
  set udg_Foot032Wall[s]=CreateDestructable('LTcr',udg_Foot032X[s],udg_Foot032Y[s],270.0,1.0,0)
  call Foot032Mark("end-create-wall site="+I2S(s))
  call Foot032Probe(s,"wall")
 elseif k==1 then
  call Foot032Mark("begin-insert-unit site="+I2S(s))
  set udg_Foot032Unit[s]=CreateUnit(Player(0),'hfoo',@STAGE_X@,@STAGE_Y@,270.0)
  call SetUnitX(udg_Foot032Unit[s],udg_Foot032X[s])
  call SetUnitY(udg_Foot032Unit[s],udg_Foot032Y[s])
  call Foot032Mark("end-insert-unit site="+I2S(s)+" unit="+Foot032Units(s))
 elseif k==2 then
  call Foot032Probe(s,"both")
 elseif k==3 then
  if s==0 then
   call Foot032Mark("begin-remove-unit site="+I2S(s))
   call RemoveUnit(udg_Foot032Unit[s])
   set udg_Foot032Unit[s]=null
   call Foot032Mark("end-remove-unit site="+I2S(s))
  else
   call Foot032Mark("begin-remove-wall site="+I2S(s))
   call RemoveDestructable(udg_Foot032Wall[s])
   set udg_Foot032Wall[s]=null
   call Foot032Mark("end-remove-wall site="+I2S(s)+" unit="+Foot032Units(s))
  endif
  call Foot032Probe(s,"first-removed")
 elseif k==4 then
  if s==0 then
   call Foot032Mark("begin-remove-wall site="+I2S(s))
   call RemoveDestructable(udg_Foot032Wall[s])
   set udg_Foot032Wall[s]=null
   call Foot032Mark("end-remove-wall site="+I2S(s))
  else
   call Foot032Mark("begin-remove-unit site="+I2S(s))
   call RemoveUnit(udg_Foot032Unit[s])
   set udg_Foot032Unit[s]=null
   call Foot032Mark("end-remove-unit site="+I2S(s))
  endif
  call Foot032Probe(s,"both-removed")
 else
  call Foot032Mark("site-done site="+I2S(s))
 endif
 set udg_Foot032Step=udg_Foot032Step+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_Foot032X[0]=@SITE0_X@
 set udg_Foot032Y[0]=@SITE0_Y@
 set udg_Foot032X[1]=@SITE1_X@
 set udg_Foot032Y[1]=@SITE1_Y@
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1024.0,1024.0)
 call Foot032Mark("start")
 set udg_Foot032Timer=CreateTimer()
 call TimerStart(udg_Foot032Timer,0.1,true,function Foot032Tick)
endfunction
