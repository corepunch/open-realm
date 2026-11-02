globals
 timer udg_P349Timer=null
 integer udg_P349Tick=0
 unit udg_P349Caster=null
 unit udg_P349Target=null
endglobals
function P349Mark takes string label returns nothing
 call Preload("P349 tick="+I2S(udg_P349Tick)+" label="+label)
endfunction
function P349Sample takes unit u,integer index returns nothing
 call P349Mark("unit u="+I2S(index)+" order="+I2S(GetUnitCurrentOrder(u))+" x="+R2S(GetUnitX(u))+" y="+R2S(GetUnitY(u))+" facing="+R2S(GetUnitFacing(u))+" speed="+R2S(GetUnitMoveSpeed(u))+" life="+R2S(GetUnitState(u,UNIT_STATE_LIFE)))
endfunction
function P349Tick takes nothing returns nothing
 set udg_P349Tick=udg_P349Tick+1
 if udg_P349Tick==1 then
  call P349Mark("start")
 endif
 if udg_P349Tick==2 then
  call P349Mark("pow="+R2S(Pow(2.,0.5)))
  call P349Mark("pow="+R2S(Pow(0.,0.)))
  call P349Mark("pow="+R2S(Pow(-2.,-3.)))
 endif
 if udg_P349Tick==10 then
  set udg_P349Caster=CreateUnit(Player(0),'hP49',512.,768.,0.)
  set udg_P349Target=CreateUnit(Player(1),'hP49',672.,768.,0.)
  call UnitRemoveAbility(udg_P349Caster,'Aatk')
  call UnitRemoveAbility(udg_P349Target,'Aatk')
  call UnitAddAbility(udg_P349Caster,'AEsh')
  call SetUnitAbilityLevel(udg_P349Caster,'AEsh',1)
  call SetUnitState(udg_P349Caster,UNIT_STATE_MAX_MANA,1000.)
  call SetUnitState(udg_P349Caster,UNIT_STATE_MANA,1000.)
  call SetUnitState(udg_P349Target,UNIT_STATE_MAX_LIFE,5000.)
  call SetUnitState(udg_P349Target,UNIT_STATE_LIFE,5000.)
 endif
 if udg_P349Tick==12 then
  if IssueTargetOrder(udg_P349Caster,"shadowstrike",udg_P349Target) then
   call P349Mark("cast-accepted")
  else
   call P349Mark("cast-rejected")
  endif
 endif
 if udg_P349Tick==20 or udg_P349Tick==100 or udg_P349Tick==180 then
  call IssuePointOrder(udg_P349Target,"move",1536.,768.)
 endif
 if udg_P349Tick==60 or udg_P349Tick==140 then
  call IssuePointOrder(udg_P349Target,"move",512.,768.)
 endif
 if udg_P349Tick==80 then
  call SetUnitFacing(udg_P349Target,135.)
 endif
 if udg_P349Tick==220 then
  call P349Mark("complete")
 endif
 if udg_P349Tick>=10 then
  call P349Sample(udg_P349Caster,0)
  call P349Sample(udg_P349Target,1)
 endif
 if udg_P349Tick==220 then
  call PreloadGenEnd("power349.txt")
  call PauseTimer(udg_P349Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0.)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_P349Timer=CreateTimer()
 call TimerStart(udg_P349Timer,0.1,true,function P349Tick)
endfunction
