globals
 timer udg_R335Timer=null
 integer udg_R335Tick=0
 integer udg_R335Scene=-1
 integer udg_R335Local=0
 unit udg_R335Unit=null
endglobals
function R335Mark takes string label returns nothing
 call Preload("R335 tick="+I2S(udg_R335Tick)+" scene="+I2S(udg_R335Scene)+" l="+I2S(udg_R335Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_R335Unit))+" x="+R2SW(GetUnitX(udg_R335Unit),1,4)+" y="+R2SW(GetUnitY(udg_R335Unit),1,4))
endfunction
function R335Setup takes nothing returns nothing
 local integer cls=ModuloInteger(udg_R335Scene,4)
 local integer mode=udg_R335Scene/4
 local integer kind='hF91'
 local real x=624
 local real y=632
 local boolean accepted=false
 call R335Mark("begin-setup")
 if cls==1 then
  set kind='hF92'
 elseif cls==2 then
  set kind='hF93'
 elseif cls==3 then
  set kind='hF94'
 endif
 set udg_R335Unit=CreateUnit(Player(0),kind,272,304,90)
 call SetUnitAcquireRange(udg_R335Unit,0)
 if mode==0 then
  call SetUnitX(udg_R335Unit,257)
  call SetUnitY(udg_R335Unit,289)
  set x=287
  set y=319
 elseif mode>=2 then
  set x=1536
  set y=304
 endif
 call R335Mark("created")
 set accepted=IssuePointOrder(udg_R335Unit,"move",x,y)
 if accepted then
  call R335Mark("accepted")
 else
  call R335Mark("rejected")
 endif
endfunction
function R335Tick takes nothing returns nothing
 local integer duration=50
 set udg_R335Tick=udg_R335Tick+1
 if udg_R335Tick==1 then
  call R335Mark("start")
  call PreloadGenEnd("route335-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_R335Tick==20 then
  set udg_R335Scene=0
  call R335Setup()
 elseif udg_R335Scene>=0 then
  set udg_R335Local=udg_R335Local+1
  if udg_R335Scene<4 then
   set duration=35
  elseif udg_R335Scene<12 and udg_R335Scene>=8 then
   set duration=140
  elseif udg_R335Scene>=12 then
   set duration=80
   if udg_R335Local==10 then
    call R335Mark("before-pause")
    call PauseUnit(udg_R335Unit,true)
    call R335Mark("after-pause")
   elseif udg_R335Local==20 then
    call PauseUnit(udg_R335Unit,false)
    call R335Mark("after-resume")
   elseif udg_R335Local==35 then
    call SetUnitPathing(udg_R335Unit,false)
    call R335Mark("query-zero")
   elseif udg_R335Local==40 then
    call SetUnitPathing(udg_R335Unit,true)
    call R335Mark("query-restored")
   elseif udg_R335Local==50 then
    call IssueImmediateOrder(udg_R335Unit,"stop")
    call R335Mark("after-stop")
   elseif udg_R335Local==55 then
    call IssuePointOrder(udg_R335Unit,"move",1536,304)
    call R335Mark("after-replacement")
   endif
  endif
  call R335Mark("sample")
  if udg_R335Local==duration then
   call R335Mark("begin-cleanup")
   call RemoveUnit(udg_R335Unit)
   set udg_R335Unit=null
   call R335Mark("end-cleanup")
   set udg_R335Local=0
   set udg_R335Scene=udg_R335Scene+1
   if udg_R335Scene==16 then
    call R335Mark("complete")
    call PauseTimer(udg_R335Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call R335Setup()
   endif
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_R335Timer=CreateTimer()
 call TimerStart(udg_R335Timer,0.1,true,function R335Tick)
endfunction
