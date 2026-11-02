globals
 timer udg_G308Timer=null
 integer udg_G308Tick=0
 integer udg_G308Scene=-1
 integer udg_G308Local=0
 unit udg_G308Unit=null
 unit udg_G308Block=null
 unit udg_G308Extra=null
endglobals
function G308Mark takes string label returns nothing
 call Preload("G308 tick="+I2S(udg_G308Tick)+" scene="+I2S(udg_G308Scene)+" l="+I2S(udg_G308Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_G308Unit))+" x="+R2SW(GetUnitX(udg_G308Unit),1,4)+" y="+R2SW(GetUnitY(udg_G308Unit),1,4))
endfunction
function G308Setup takes nothing returns nothing
 local integer kind='hfoo'
 local real x=1536
 local real y=800
 local boolean accepted=false
 call G308Mark("begin-setup")
 if udg_G308Scene==10 then
  set kind='hgry'
 endif
 set udg_G308Unit=CreateUnit(Player(0),kind,288,288,0)
 call SetUnitAcquireRange(udg_G308Unit,0)
 if udg_G308Scene==0 then
  set x=2560
 elseif udg_G308Scene==1 then
  set y=2560
 elseif udg_G308Scene==2 then
  set x=-512
  set y=2560
 else
  set kind='hhou'
  if udg_G308Scene==4 or udg_G308Scene==7 or udg_G308Scene==9 then
   set kind='hfoo'
  endif
  set udg_G308Block=CreateUnit(Player(0),kind,1536,800,0)
  call SetUnitAcquireRange(udg_G308Block,0)
  call PauseUnit(udg_G308Block,true)
  set x=GetUnitX(udg_G308Block)
  set y=GetUnitY(udg_G308Block)
  if udg_G308Scene==5 or udg_G308Scene==7 then
   set udg_G308Extra=CreateUnit(Player(0),'hfoo',1312,800,0)
   call SetUnitAcquireRange(udg_G308Extra,0)
   call PauseUnit(udg_G308Extra,true)
   call SetUnitX(udg_G308Extra,x)
   call SetUnitY(udg_G308Extra,y)
  endif
 endif
 if udg_G308Scene==11 then
  call SetUnitPathing(udg_G308Unit,false)
 endif
 call G308Mark("created")
 if udg_G308Scene>=6 and udg_G308Scene<=9 then
  set accepted=IssueTargetOrder(udg_G308Unit,"move",udg_G308Block)
 else
  set accepted=IssuePointOrder(udg_G308Unit,"move",x,y)
 endif
 if accepted then
  call G308Mark("accepted")
 else
  call G308Mark("rejected")
 endif
endfunction
function G308Tick takes nothing returns nothing
 set udg_G308Tick=udg_G308Tick+1
 if udg_G308Tick==1 then
  call G308Mark("start")
  call PreloadGenEnd("goal308-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_G308Tick==20 then
  set udg_G308Scene=0
  call G308Setup()
 elseif udg_G308Scene>=0 then
  set udg_G308Local=udg_G308Local+1
  if udg_G308Local==10 and (udg_G308Scene==8 or udg_G308Scene==9) then
   call G308Mark("before-removal")
   call RemoveUnit(udg_G308Block)
   set udg_G308Block=null
   call G308Mark("after-removal")
  endif
  call G308Mark("sample")
  if udg_G308Local==60 then
   call G308Mark("begin-cleanup")
   call RemoveUnit(udg_G308Unit)
   call RemoveUnit(udg_G308Block)
   call RemoveUnit(udg_G308Extra)
   set udg_G308Unit=null
   set udg_G308Block=null
   set udg_G308Extra=null
   call G308Mark("end-cleanup")
   set udg_G308Local=0
   set udg_G308Scene=udg_G308Scene+1
   if udg_G308Scene==12 then
    call G308Mark("complete")
    call PauseTimer(udg_G308Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call G308Setup()
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
 set udg_G308Timer=CreateTimer()
 call TimerStart(udg_G308Timer,0.1,true,function G308Tick)
endfunction
