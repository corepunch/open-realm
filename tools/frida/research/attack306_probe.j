globals
 timer udg_A306Timer=null
 integer udg_A306Tick=0
 integer udg_A306Scene=-1
 integer udg_A306Local=0
 unit udg_A306Unit=null
 unit udg_A306Target=null
 destructable udg_A306Destruct=null
endglobals
function A306Mark takes string label returns nothing
 call Preload("A306 tick="+I2S(udg_A306Tick)+" scene="+I2S(udg_A306Scene)+" l="+I2S(udg_A306Local)+" label="+label+" type="+I2S(GetUnitTypeId(udg_A306Unit))+" order="+I2S(GetUnitCurrentOrder(udg_A306Unit))+" x="+R2SW(GetUnitX(udg_A306Unit),1,4)+" y="+R2SW(GetUnitY(udg_A306Unit),1,4))
endfunction
function A306Setup takes nothing returns nothing
 local integer kind='hfoo'
 local integer profile=ModuloInteger(udg_A306Scene,3)
 local real x=288
 local real facing=0
 local boolean accepted=false
 call A306Mark("begin-setup")
 if profile==1 then
  set kind='hgry'
 endif
 if udg_A306Scene>=3 then
  set x=1408
  set facing=180
 endif
 if profile==0 then
  set udg_A306Target=CreateUnit(Player(1),'hhou',1536,288,0)
 elseif profile==1 then
  set udg_A306Target=CreateUnit(Player(1),'hfoo',1536,288,0)
  call SetUnitAcquireRange(udg_A306Target,0)
  call PauseUnit(udg_A306Target,true)
 else
  set udg_A306Destruct=CreateDestructable('LTlt',1536,288,0,1,0)
 endif
 set udg_A306Unit=CreateUnit(Player(0),kind,x,288,facing)
 call SetUnitAcquireRange(udg_A306Unit,0)
 call A306Mark("created")
 if profile==2 then
  set accepted=IssueTargetOrder(udg_A306Unit,"attack",udg_A306Destruct)
 else
  set accepted=IssueTargetOrder(udg_A306Unit,"attack",udg_A306Target)
 endif
 if accepted then
  call A306Mark("accepted")
 else
  call A306Mark("rejected")
 endif
endfunction
function A306Cleanup takes nothing returns nothing
 call A306Mark("begin-cleanup")
 call RemoveUnit(udg_A306Unit)
 if udg_A306Target!=null then
  call RemoveUnit(udg_A306Target)
 endif
 if udg_A306Destruct!=null then
  call RemoveDestructable(udg_A306Destruct)
 endif
 set udg_A306Unit=null
 set udg_A306Target=null
 set udg_A306Destruct=null
 call A306Mark("end-cleanup")
endfunction
function A306Tick takes nothing returns nothing
 set udg_A306Tick=udg_A306Tick+1
 if udg_A306Tick==1 then
  call A306Mark("start")
  call PreloadGenEnd("attack306-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_A306Tick==20 then
  set udg_A306Scene=0
  call A306Setup()
 elseif udg_A306Scene>=0 then
  set udg_A306Local=udg_A306Local+1
  if udg_A306Local==10 then
   call PauseUnit(udg_A306Unit,true)
   call A306Mark("paused")
  elseif udg_A306Local==12 then
   call PauseUnit(udg_A306Unit,false)
   call A306Mark("resumed")
  elseif udg_A306Local==40 then
   call IssueImmediateOrder(udg_A306Unit,"stop")
   call A306Mark("stopped")
  endif
  call A306Mark("sample")
  if udg_A306Local==50 then
   call A306Cleanup()
   set udg_A306Local=0
   set udg_A306Scene=udg_A306Scene+1
   if udg_A306Scene==6 then
    call A306Mark("complete")
    call PauseTimer(udg_A306Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call A306Setup()
   endif
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_A306Timer=CreateTimer()
 call TimerStart(udg_A306Timer,0.1,true,function A306Tick)
endfunction
