globals
 timer tdrTimer=null
 unit tdrActor=null
 item tdrItem=null
 unit array tdrFarm
 integer tdrFarms=0
 integer tdrTick=0
 integer tdrScene=0
 real tdrX=0.0
 real tdrY=1056.0
endglobals
function TdrMark takes string label,boolean accepted returns nothing
 local integer ok=0
 local integer owned=0
 if accepted then
  set ok=1
 endif
 if tdrItem!=null and IsItemOwned(tdrItem) then
  set owned=1
 endif
 call Preload("TDR tick="+I2S(tdrTick)+" scene="+I2S(tdrScene)+" label="+label+" accepted="+I2S(ok)+" head="+I2S(GetUnitCurrentOrder(tdrActor))+" x="+R2S(GetUnitX(tdrActor))+" y="+R2S(GetUnitY(tdrActor))+" facing="+R2S(GetUnitFacing(tdrActor))+" carried="+I2S(GetItemTypeId(UnitItemInSlot(tdrActor,0)))+" owned="+I2S(owned)+" ix="+R2S(GetItemX(tdrItem))+" iy="+R2S(GetItemY(tdrItem))+" px="+R2S(tdrX)+" py="+R2S(tdrY))
endfunction
function TdrFarm takes real x,real y returns nothing
 set tdrFarm[tdrFarms]=CreateUnit(Player(0),'hhou',x,y,270.0)
 set tdrFarms=tdrFarms+1
endfunction
// Scenes: 0-3 adjacent/equality/far DropItemRange edges, 4 occupied Farm center,
// 5 enclosed Farm ring center, 6 public Stop while walking, 7 zero distance,
// 8/9 undroppable item at zero/far distance, 10 RemoveItem while walking,
// 11 script UnitRemoveItem while walking.
function TdrTick takes nothing returns nothing
 local integer age=ModuloInteger(tdrTick,50)
 local integer i=0
 if age==0 then
  set tdrActor=CreateUnit(Player(0),'Hpal',704.0,1056.0,0.0)
  call SetUnitAcquireRange(tdrActor,0.0)
  call SetUnitMoveSpeed(tdrActor,270.0)
  set tdrItem=CreateItem('ratf',704.0,1056.0)
  call TdrMark("add-item",UnitAddItem(tdrActor,tdrItem))
  set tdrY=1056.0
  if tdrScene==0 then
   set tdrX=704.0+DROP_RANGE-0.25
  elseif tdrScene==1 then
   set tdrX=704.0+DROP_RANGE
  elseif tdrScene==2 then
   set tdrX=704.0+DROP_RANGE+0.25
  elseif tdrScene==3 or tdrScene==6 or tdrScene>=9 then
   set tdrX=704.0+DROP_RANGE+300.0
  elseif tdrScene==4 then
   call TdrFarm(1216.0,1056.0)
   set tdrX=GetUnitX(tdrFarm[0])
   set tdrY=GetUnitY(tdrFarm[0])
  elseif tdrScene==5 then
   call TdrFarm(1344.0,928.0)
   call TdrFarm(1472.0,928.0)
   call TdrFarm(1600.0,928.0)
   call TdrFarm(1344.0,1056.0)
   call TdrFarm(1600.0,1056.0)
   call TdrFarm(1344.0,1184.0)
   call TdrFarm(1472.0,1184.0)
   call TdrFarm(1600.0,1184.0)
   set tdrX=1472.0
   set tdrY=1056.0
  else
   set tdrX=704.0
  endif
  if tdrScene==8 or tdrScene==9 then
   call SetItemDroppable(tdrItem,false)
  endif
  call TdrMark("canonical-actor",GetUnitTypeId(tdrActor)=='Hpal' and GetUnitAbilityLevel(tdrActor,'AInv')>0 and GetUnitMoveSpeed(tdrActor)==270.0)
  loop
   exitwhen i>=tdrFarms
   call Preload("TDR tick="+I2S(tdrTick)+" scene="+I2S(tdrScene)+" label=farm index="+I2S(i)+" fx="+R2S(GetUnitX(tdrFarm[i]))+" fy="+R2S(GetUnitY(tdrFarm[i])))
   set i=i+1
  endloop
 elseif age==2 then
  call TdrMark("before",true)
  call TdrMark("drop-return",UnitDropItemPoint(tdrActor,tdrItem,tdrX,tdrY))
 elseif age==6 and tdrScene==6 then
  call TdrMark("stop-before",true)
  call TdrMark("stop-return",IssueImmediateOrder(tdrActor,"stop"))
 elseif age==6 and tdrScene==10 then
  call TdrMark("remove-before",true)
  call RemoveItem(tdrItem)
  set tdrItem=null
  call TdrMark("remove-return",true)
 elseif age==6 and tdrScene==11 then
  call TdrMark("unequip-before",true)
  call UnitRemoveItem(tdrActor,tdrItem)
  call TdrMark("unequip-return",true)
 elseif age==45 then
  call TdrMark("settled",true)
  call RemoveItem(tdrItem)
  call RemoveUnit(tdrActor)
  loop
   exitwhen tdrFarms<=0
   set tdrFarms=tdrFarms-1
   call RemoveUnit(tdrFarm[tdrFarms])
   set tdrFarm[tdrFarms]=null
  endloop
  set tdrActor=null
  set tdrItem=null
 elseif age==49 then
  set tdrScene=tdrScene+1
  if tdrScene==12 then
   call PauseTimer(tdrTimer)
   call TdrMark("complete",true)
   call PreloadGenEnd("PRELOAD_FILE")
  endif
 endif
 if age<45 then
  call TdrMark("sample",true)
 endif
 set tdrTick=tdrTick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(38301)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SuspendTimeOfDay(true)
 call Preload("TDR tick=0 scene=-1 label=start")
 call PreloadGenEnd("START_FILE")
 set tdrTimer=CreateTimer()
 call TimerStart(tdrTimer,0.1,true,function TdrTick)
endfunction
