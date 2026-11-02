globals
 unit array udg_O343Units
 integer udg_O343Tick=0
 timer udg_O343Timer=null
endglobals
function O343Mark takes string label returns nothing
 local integer i=0
 local string state=""
 loop
  exitwhen i==9
  set state=state+" u"+I2S(i)+"="+I2S(GetUnitTypeId(udg_O343Units[i]))
  set i=i+1
 endloop
 call Preload("O343 tick="+I2S(udg_O343Tick)+" label="+label+state)
endfunction
function O343Tick takes nothing returns nothing
 local integer i=0
 if udg_O343Tick==0 then
  loop
   exitwhen i==9
   set udg_O343Units[i]=CreateUnit(Player(0),'hfoo',400+I2R(ModuloInteger(i,3)*160),400+I2R((i/3)*160),0)
   call SetUnitAcquireRange(udg_O343Units[i],0)
   set i=i+1
  endloop
 elseif udg_O343Tick==1 then
  call O343Mark("kill")
  loop
   exitwhen i==9
   if i!=5 then
    call KillUnit(udg_O343Units[i])
   endif
   set i=i+1
  endloop
  call UnitSuspendDecay(udg_O343Units[5],true)
  call UnitSuspendDecay(udg_O343Units[5],false)
  call UnitSuspendDecay(udg_O343Units[6],true)
  call UnitSuspendDecay(udg_O343Units[6],false)
 elseif udg_O343Tick==40 then
  call O343Mark("remove-flesh")
  call RemoveUnit(udg_O343Units[1])
  call UnitSuspendDecay(udg_O343Units[3],true)
  call UnitSuspendDecay(udg_O343Units[7],false)
  call UnitSuspendDecay(udg_O343Units[8],true)
 elseif udg_O343Tick==45 then
  call UnitSuspendDecay(udg_O343Units[3],true)
 elseif udg_O343Tick==60 then
  call O343Mark("remove-bone")
  call RemoveUnit(udg_O343Units[2])
  call UnitSuspendDecay(udg_O343Units[4],true)
  call UnitSuspendDecay(udg_O343Units[7],false)
  call RemoveUnit(udg_O343Units[8])
 elseif udg_O343Tick==80 then
  call O343Mark("resume-flesh")
  call UnitSuspendDecay(udg_O343Units[3],false)
 elseif udg_O343Tick==100 then
  call O343Mark("resume-bone")
  call UnitSuspendDecay(udg_O343Units[4],false)
 elseif udg_O343Tick==180 then
  call O343Mark("complete")
  call PreloadGenEnd("order343.txt")
  call PauseTimer(udg_O343Timer)
 endif
 call O343Mark("sample")
 set udg_O343Tick=udg_O343Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 call O343Mark("start")
 set udg_O343Timer=CreateTimer()
 call TimerStart(udg_O343Timer,.1,true,function O343Tick)
endfunction
