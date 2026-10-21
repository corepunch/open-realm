globals
 timer milTimer=null
 unit milWorker=null
 unit milHall=null
 integer milTick=0
 integer milScene=0
endglobals
function MilBool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function MilMark takes string label,boolean accepted returns nothing
 call Preload("MIL tick="+I2S(milTick)+" scene="+I2S(milScene)+" label="+label+" accepted="+I2S(MilBool(accepted))+" head="+I2S(GetUnitCurrentOrder(milWorker))+" type="+I2S(GetUnitTypeId(milWorker))+" x="+R2S(GetUnitX(milWorker))+" y="+R2S(GetUnitY(milWorker))+" facing="+R2S(GetUnitFacing(milWorker))+" amil="+I2S(GetUnitAbilityLevel(milWorker,'Amil'))+" amic="+I2S(GetUnitAbilityLevel(milHall,'Amic'))+" bmil="+I2S(GetUnitAbilityLevel(milWorker,'Bmil')))
endfunction
function MilTick takes nothing returns nothing
 local integer age=ModuloInteger(milTick,500)
 local integer mode=ModuloInteger(milScene,3)
 local real x=1152.0
 local real face=0.0
 local string order="militia"
 if mode==0 then
  set x=704.0
 elseif mode==2 then
  set face=180.0
 endif
 if milScene>=3 then
  set order="militiaoff"
 endif
 if age==0 then
  set milHall=CreateUnit(Player(0),'htow',1536.0,1024.0,0.0)
  call UnitAddAbility(milHall,'Amic')
  set milWorker=CreateUnit(Player(0),'hpea',1152.0,1024.0,0.0)
  call SetUnitAcquireRange(milWorker,0.0)
  call MilMark("spawn",true)
 elseif age==5 and milScene>=3 then
  call MilMark("setup-arm-return",IssueTargetOrder(milWorker,"militia",milHall))
 elseif age==80 then
  call SetUnitPosition(milWorker,x,1024.0)
  call SetUnitFacing(milWorker,face)
  call MilMark("positioned",true)
 elseif age==100 then
  call MilMark("before",true)
  call MilMark("order-return",IssueTargetOrder(milWorker,order,milHall))
 elseif age==498 then
  call MilMark("settled",true)
 elseif age==499 then
  call RemoveUnit(milWorker)
  call RemoveUnit(milHall)
  set milWorker=null
  set milHall=null
  set milScene=milScene+1
  if milScene==6 then
   call Preload("MIL tick="+I2S(milTick)+" scene=6 label=complete")
   call PreloadGenEnd("PRELOAD_FILE")
   call PauseTimer(milTimer)
   return
  endif
 elseif age>100 and ModuloInteger(age,5)==0 then
  call MilMark("sample",true)
 endif
 set milTick=milTick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 call Preload("MIL tick=0 scene=-1 label=start")
 call PreloadGenEnd("START_FILE")
 call PreloadGenClear()
 call PreloadGenStart()
 set milTimer=CreateTimer()
 call TimerStart(milTimer,0.02,true,function MilTick)
endfunction
