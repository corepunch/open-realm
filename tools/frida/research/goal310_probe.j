globals
 timer udg_G310Timer=null
 integer udg_G310Tick=0
 integer udg_G310Scene=-1
 integer udg_G310Local=0
 unit udg_G310Unit=null
 unit udg_G310Block=null
 unit udg_G310Extra=null
endglobals
function G310Mark takes string label returns nothing
 call Preload("G310 tick="+I2S(udg_G310Tick)+" scene="+I2S(udg_G310Scene)+" l="+I2S(udg_G310Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_G310Unit))+" x="+R2SW(GetUnitX(udg_G310Unit),1,4)+" y="+R2SW(GetUnitY(udg_G310Unit),1,4))
endfunction
function G310Setup takes nothing returns nothing
 local integer kind='hQ0A'+udg_G310Scene/8
 local integer scenario=ModuloInteger(udg_G310Scene,8)
 local real x=1536
 local real y=800
 local boolean accepted=false
 call G310Mark("begin-setup")
 set udg_G310Unit=CreateUnit(Player(0),kind,288,288,0)
 call SetUnitAcquireRange(udg_G310Unit,0)
 if scenario==0 then
  set x=-512
  set y=2560
 else
  set kind='hhou'
  if scenario==2 or scenario==4 or scenario==7 then
   set kind='hfoo'
  endif
  set udg_G310Block=CreateUnit(Player(0),kind,1536,800,0)
  call SetUnitAcquireRange(udg_G310Block,0)
  call PauseUnit(udg_G310Block,true)
  set x=GetUnitX(udg_G310Block)
  set y=GetUnitY(udg_G310Block)
  if scenario==2 then
   set udg_G310Extra=CreateUnit(Player(0),'hfoo',1312,800,0)
   call SetUnitAcquireRange(udg_G310Extra,0)
   call PauseUnit(udg_G310Extra,true)
   call SetUnitX(udg_G310Extra,x)
   call SetUnitY(udg_G310Extra,y)
  endif
 endif
 if scenario==5 then
  call SetUnitPathing(udg_G310Unit,false)
 endif
 call G310Mark("created")
 if scenario==3 or scenario==4 or scenario==6 or scenario==7 then
  set accepted=IssueTargetOrder(udg_G310Unit,"move",udg_G310Block)
 else
  set accepted=IssuePointOrder(udg_G310Unit,"move",x,y)
 endif
 if accepted then
  call G310Mark("accepted")
 else
  call G310Mark("rejected")
 endif
endfunction
function G310Tick takes nothing returns nothing
 set udg_G310Tick=udg_G310Tick+1
 if udg_G310Tick==1 then
  call G310Mark("start")
  call PreloadGenEnd("goal310-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_G310Tick==20 then
  set udg_G310Scene=0
  call G310Setup()
 elseif udg_G310Scene>=0 then
  set udg_G310Local=udg_G310Local+1
  if udg_G310Local==10 and (ModuloInteger(udg_G310Scene,8)==3 or ModuloInteger(udg_G310Scene,8)==4) then
   call G310Mark("before-removal")
   call RemoveUnit(udg_G310Block)
   set udg_G310Block=null
   call G310Mark("after-removal")
  endif
  call G310Mark("sample")
  if (udg_G310Local==60 and ModuloInteger(udg_G310Scene,8)!=0) or udg_G310Local==120 then
   call G310Mark("begin-cleanup")
   call RemoveUnit(udg_G310Unit)
   call RemoveUnit(udg_G310Block)
   call RemoveUnit(udg_G310Extra)
   set udg_G310Unit=null
   set udg_G310Block=null
   set udg_G310Extra=null
   call G310Mark("end-cleanup")
   set udg_G310Local=0
   set udg_G310Scene=udg_G310Scene+1
   if udg_G310Scene==96 then
    call G310Mark("complete")
    call PauseTimer(udg_G310Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call G310Setup()
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
 set udg_G310Timer=CreateTimer()
 call TimerStart(udg_G310Timer,0.1,true,function G310Tick)
endfunction
