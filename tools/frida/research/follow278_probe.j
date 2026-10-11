globals
 unit udg_F278Actor=null
 unit udg_F278Target=null
 timer udg_F278Timer=null
 integer udg_F278Tick=0
 integer udg_F278Scene=0
endglobals
function F278Mark takes string label returns nothing
 call Preload("F278 tick="+I2S(udg_F278Tick)+" scene="+I2S(udg_F278Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_F278Actor))+" x="+R2S(GetUnitX(udg_F278Actor))+" y="+R2S(GetUnitY(udg_F278Actor))+" tx="+R2S(GetUnitX(udg_F278Target))+" ty="+R2S(GetUnitY(udg_F278Target)))
endfunction
function F278Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_F278Tick,140)
 local integer targetType='hfoo'
 local boolean accepted=false
 if udg_F278Tick==1 then
  call F278Mark("start_file")
  call PreloadGenEnd("rs-follow278-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if t==0 then
  if udg_F278Scene==1 or udg_F278Scene==2 then
   set targetType='hbar'
  elseif udg_F278Scene==3 then
   set targetType='hkee'
  elseif udg_F278Scene>=4 then
   set targetType='etol'
  endif
  set udg_F278Actor=CreateUnit(Player(0),'hfoo',320.0,512.0,0.0)
  set udg_F278Target=CreateUnit(Player(0),targetType,1280.0,512.0,180.0)
  call SetUnitAcquireRange(udg_F278Actor,0.0)
  call SetUnitAcquireRange(udg_F278Target,0.0)
  if udg_F278Scene==5 then
   call IssueImmediateOrder(udg_F278Target,"unroot")
  endif
  call F278Mark("begin")
 elseif t==30 then
  if udg_F278Scene==2 then
   set accepted=IssueTargetOrder(udg_F278Actor,"smart",udg_F278Target)
  else
   set accepted=IssueTargetOrder(udg_F278Actor,"move",udg_F278Target)
  endif
  if accepted then
   call F278Mark("accepted")
  else
   call F278Mark("rejected")
  endif
 elseif t==125 then
  call F278Mark("settled")
 elseif t==130 then
  call IssueImmediateOrder(udg_F278Actor,"stop")
  call RemoveUnit(udg_F278Actor)
  call RemoveUnit(udg_F278Target)
 elseif t==139 then
  if udg_F278Scene==5 then
   call F278Mark("complete")
   call PreloadGenEnd("rs-follow278.txt")
   call PauseTimer(udg_F278Timer)
  endif
  set udg_F278Scene=udg_F278Scene+1
 endif
 if t<130 then
  call F278Mark("sample")
 endif
 set udg_F278Tick=udg_F278Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1030)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerController(Player(0),MAP_CONTROL_USER)
 call EnableUserControl(true)
 call ShowInterface(true,0.0)
 call F278Mark("start")
 set udg_F278Timer=CreateTimer()
 call TimerStart(udg_F278Timer,0.1,true,function F278Tick)
endfunction
