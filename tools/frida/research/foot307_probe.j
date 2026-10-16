globals
 timer udg_F307Timer=null
 integer udg_F307Tick=0
 integer udg_F307Scene=-1
 integer udg_F307Local=0
 unit udg_F307Unit=null
 unit udg_F307Block=null
endglobals
function F307Mark takes string label returns nothing
 call Preload("F307 tick="+I2S(udg_F307Tick)+" scene="+I2S(udg_F307Scene)+" l="+I2S(udg_F307Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_F307Unit))+" x="+R2SW(GetUnitX(udg_F307Unit),1,4)+" y="+R2SW(GetUnitY(udg_F307Unit),1,4))
endfunction
function F307Setup takes nothing returns nothing
 local integer kind='hfoo'
 local boolean accepted=false
 call F307Mark("begin-setup")
 set udg_F307Unit=CreateUnit(Player(0),'hfoo',288,288,0)
 call SetUnitAcquireRange(udg_F307Unit,0)
 if udg_F307Scene>0 then
  if udg_F307Scene==2 or udg_F307Scene==3 or udg_F307Scene==5 then
   set kind='hhou'
  endif
  set udg_F307Block=CreateUnit(Player(0),kind,800,800,0)
  call SetUnitAcquireRange(udg_F307Block,0)
  call PauseUnit(udg_F307Block,true)
  call SetUnitX(udg_F307Unit,GetUnitX(udg_F307Block))
  call SetUnitY(udg_F307Unit,GetUnitY(udg_F307Block))
  if udg_F307Scene==5 then
   call SetUnitX(udg_F307Unit,GetUnitX(udg_F307Block)+31.25)
  endif
 endif
 call F307Mark("created")
 if udg_F307Scene==3 or udg_F307Scene==4 then
  set accepted=IssueTargetOrder(udg_F307Unit,"move",udg_F307Block)
 else
  set accepted=IssuePointOrder(udg_F307Unit,"move",1536,800)
 endif
 if accepted then
  call F307Mark("accepted")
 else
  call F307Mark("rejected")
 endif
endfunction
function F307Tick takes nothing returns nothing
 set udg_F307Tick=udg_F307Tick+1
 if udg_F307Tick==1 then
  call F307Mark("start")
  call PreloadGenEnd("foot307-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_F307Tick==20 then
  set udg_F307Scene=0
  call F307Setup()
 elseif udg_F307Scene>=0 then
  set udg_F307Local=udg_F307Local+1
  call F307Mark("sample")
  if udg_F307Local==50 then
   call F307Mark("begin-cleanup")
   call RemoveUnit(udg_F307Unit)
   call RemoveUnit(udg_F307Block)
   set udg_F307Unit=null
   set udg_F307Block=null
   call F307Mark("end-cleanup")
   set udg_F307Local=0
   set udg_F307Scene=udg_F307Scene+1
   if udg_F307Scene==6 then
    call F307Mark("complete")
    call PauseTimer(udg_F307Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call F307Setup()
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
 set udg_F307Timer=CreateTimer()
 call TimerStart(udg_F307Timer,0.1,true,function F307Tick)
endfunction
