globals
 timer udg_M326Timer=null
 unit udg_M326Unit=null
 integer udg_M326Tick=0
 integer udg_M326Scene=0
endglobals
function M326Mark takes string label returns nothing
 local integer paused=0
 if IsUnitPaused(udg_M326Unit) then
  set paused=1
 endif
 call Preload("M326 tick="+I2S(udg_M326Tick)+" scene="+I2S(udg_M326Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_M326Unit))+" order="+I2S(GetUnitCurrentOrder(udg_M326Unit))+" paused="+I2S(paused))
 call PreloadGenEnd("@OUTPUT@")
endfunction
function M326Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_M326Tick,40)
 local integer form=udg_M326Scene/4
 local integer action=ModuloInteger(udg_M326Scene,4)
 local boolean ok=false
 if age==0 then
  if form==0 then
   set udg_M326Unit=CreateUnit(Player(0),'edoc',640,1024,0)
  elseif form==1 then
   set udg_M326Unit=CreateUnit(Player(0),'edcm',640,1024,0)
  elseif form==2 then
   set udg_M326Unit=CreateUnit(Player(0),'edot',640,1024,0)
  else
   set udg_M326Unit=CreateUnit(Player(0),'edtm',640,1024,0)
  endif
  call SetUnitAcquireRange(udg_M326Unit,0)
  call SetUnitState(udg_M326Unit,UNIT_STATE_MANA,1000)
  call M326Mark("created")
 elseif age==2 then
  if form==0 then
   set ok=IssueImmediateOrder(udg_M326Unit,"bearform")
  elseif form==1 then
   set ok=IssueImmediateOrder(udg_M326Unit,"unbearform")
  elseif form==2 then
   set ok=IssueImmediateOrder(udg_M326Unit,"ravenform")
  else
   set ok=IssueImmediateOrder(udg_M326Unit,"unravenform")
  endif
  if ok then
   call M326Mark("cast-accepted")
  else
   call M326Mark("cast-rejected")
  endif
 elseif age==3 then
  if action==1 then
   set ok=IssueImmediateOrder(udg_M326Unit,"stop")
   if ok then
    call M326Mark("stop-accepted")
   else
    call M326Mark("stop-rejected")
   endif
  elseif action==2 then
   call PauseUnit(udg_M326Unit,true)
   call M326Mark("pause")
  elseif action==3 then
   call KillUnit(udg_M326Unit)
   call M326Mark("kill")
  endif
 elseif age==25 and action==2 then
  call PauseUnit(udg_M326Unit,false)
  call M326Mark("unpause")
 elseif age==37 then
  call RemoveUnit(udg_M326Unit)
  call M326Mark("cleanup")
 elseif age==39 then
  if udg_M326Scene==15 then
   call M326Mark("complete")
   call PauseTimer(udg_M326Timer)
  endif
  set udg_M326Scene=udg_M326Scene+1
 endif
 if age<37 then
  call M326Mark("sample")
 endif
 set udg_M326Tick=udg_M326Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1326)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetPlayerTechResearched(Player(0),'Redc',3)
 call SetPlayerTechResearched(Player(0),'Redt',3)
 call M326Mark("start")
 set udg_M326Timer=CreateTimer()
 call TimerStart(udg_M326Timer,0.1,true,function M326Tick)
endfunction
