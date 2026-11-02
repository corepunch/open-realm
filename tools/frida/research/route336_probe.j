globals
 timer udg_R336Timer=null
 integer udg_R336Tick=0
 integer udg_R336Scene=-1
 integer udg_R336Local=0
 unit udg_R336Unit=null
endglobals
function R336Mark takes string label returns nothing
 call Preload("R336 tick="+I2S(udg_R336Tick)+" scene="+I2S(udg_R336Scene)+" l="+I2S(udg_R336Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_R336Unit))+" type="+I2S(GetUnitTypeId(udg_R336Unit))+" speed="+R2S(GetUnitMoveSpeed(udg_R336Unit))+" x="+R2SW(GetUnitX(udg_R336Unit),1,4)+" y="+R2SW(GetUnitY(udg_R336Unit),1,4))
endfunction
function R336Setup takes nothing returns nothing
 local integer cls=ModuloInteger(udg_R336Scene,4)
 local integer mode=udg_R336Scene/4
 local integer kind='hF91'
 local real x=624
 local real y=632
 local boolean accepted=false
 local integer morph='AF91'
 call R336Mark("begin-setup")
 if cls==1 then
  set kind='hF92'
  set morph='AF92'
 elseif cls==2 then
  set kind='hF93'
  set morph='AF93'
 elseif cls==3 then
  set kind='hF94'
  set morph='AF94'
 endif
 set udg_R336Unit=CreateUnit(Player(0),kind,272,304,90)
 call SetUnitAcquireRange(udg_R336Unit,0)
 call R336Mark("before-morph")
 call UnitAddAbility(udg_R336Unit,morph)
 call R336Mark("after-morph")
 if mode==0 then
  call SetUnitX(udg_R336Unit,257)
  call SetUnitY(udg_R336Unit,289)
  set x=287
  set y=319
 elseif mode>=2 then
  set x=1536
  set y=304
 endif
 call R336Mark("created")
 set accepted=IssuePointOrder(udg_R336Unit,"move",x,y)
 if accepted then
  call R336Mark("accepted")
 else
  call R336Mark("rejected")
 endif
endfunction
function R336Tick takes nothing returns nothing
 local integer duration=50
 set udg_R336Tick=udg_R336Tick+1
 if udg_R336Tick==1 then
  call R336Mark("start")
  call PreloadGenEnd("route336-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_R336Tick==20 then
  set udg_R336Scene=0
  call R336Setup()
 elseif udg_R336Scene>=0 then
  set udg_R336Local=udg_R336Local+1
  if udg_R336Scene<4 then
   set duration=35
  elseif udg_R336Scene<12 and udg_R336Scene>=8 then
   set duration=140
  elseif udg_R336Scene>=12 then
   set duration=80
   if udg_R336Local==10 then
    call R336Mark("before-pause")
    call PauseUnit(udg_R336Unit,true)
    call R336Mark("after-pause")
   elseif udg_R336Local==20 then
    call PauseUnit(udg_R336Unit,false)
    call R336Mark("after-resume")
   elseif udg_R336Local==35 then
    call SetUnitPathing(udg_R336Unit,false)
    call R336Mark("query-zero")
   elseif udg_R336Local==40 then
    call SetUnitPathing(udg_R336Unit,true)
    call R336Mark("query-restored")
   elseif udg_R336Local==50 then
    call IssueImmediateOrder(udg_R336Unit,"stop")
    call R336Mark("after-stop")
   elseif udg_R336Local==55 then
    call IssuePointOrder(udg_R336Unit,"move",1536,304)
    call R336Mark("after-replacement")
   endif
  endif
  call R336Mark("sample")
  if udg_R336Local==duration then
   call R336Mark("begin-cleanup")
   call RemoveUnit(udg_R336Unit)
   set udg_R336Unit=null
   call R336Mark("end-cleanup")
   set udg_R336Local=0
   set udg_R336Scene=udg_R336Scene+1
   if udg_R336Scene==16 then
    call R336Mark("complete")
    call PauseTimer(udg_R336Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call R336Setup()
   endif
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer y=0
 loop
  exitwhen y==48
  call SetTerrainPathable(784,I2R(y)*32+16,PATHING_TYPE_FLYABILITY,false)
  call SetTerrainPathable(816,I2R(y)*32+16,PATHING_TYPE_FLYABILITY,false)
  set y=y+1
 endloop
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_R336Timer=CreateTimer()
 call TimerStart(udg_R336Timer,0.1,true,function R336Tick)
endfunction
