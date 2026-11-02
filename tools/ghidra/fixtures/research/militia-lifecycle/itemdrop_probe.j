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
 local integer age=ModuloInteger(milTick,350)
 local integer interrupt=115
 local real x=704.0
 local real face=0.0
 if milScene>=7 then
  set x=1152.0
  set face=180.0
  set interrupt=102
 endif
 if age==0 then
  set milHall=CreateUnit(Player(0),'htow',1536.0,1024.0,0.0)
  call UnitAddAbility(milHall,'Amic')
  set milWorker=CreateUnit(Player(0),'hpea',x,1024.0,face)
  call SetUnitAcquireRange(milWorker,0.0)
  call MilMark("spawn",true)
 elseif age==100 then
  call MilMark("before",true)
  call MilMark("order-return",IssueTargetOrder(milWorker,"militiaconvert",milHall))
 elseif age==interrupt then
  call MilMark("before-interrupt",true)
  if milScene==1 or milScene==7 then
   call MilMark("stop-return",IssueImmediateOrder(milWorker,"stop"))
  elseif milScene==2 or milScene==8 then
   call MilMark("move-return",IssuePointOrder(milWorker,"move",512.0,768.0))
  elseif milScene==3 then
   call RemoveUnit(milHall)
   set milHall=null
   call MilMark("remove-hall",true)
  elseif milScene==4 then
   call MilMark("remove-amic",UnitRemoveAbility(milHall,'Amic'))
  elseif milScene==5 then
   call MilMark("remove-amil",UnitRemoveAbility(milWorker,'Amil'))
  elseif milScene==6 then
   call PauseUnit(milWorker,true)
   call MilMark("pause",true)
  else
   call MilMark("control",true)
  endif
 elseif age==165 and milScene==6 then
  call MilMark("before-resume",true)
  call PauseUnit(milWorker,false)
  call MilMark("resume",true)
 elseif age==348 then
  call MilMark("settled",true)
 elseif age==349 then
  call RemoveUnit(milWorker)
  if milHall!=null then
   call RemoveUnit(milHall)
  endif
  set milWorker=null
  set milHall=null
  set milScene=milScene+1
  if milScene==9 then
   call Preload("MIL tick="+I2S(milTick)+" scene=9 label=complete")
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
