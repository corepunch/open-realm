globals
 unit udg_M271Caster=null
 unit udg_M271Target=null
 timer udg_M271Timer=null
 integer udg_M271Tick=0
endglobals
function M271Mark takes string label returns nothing
 call Preload("M271 tick="+I2S(udg_M271Tick)+" label="+label+" current="+R2S(GetUnitMoveSpeed(udg_M271Target))+" default="+R2S(GetUnitDefaultMoveSpeed(udg_M271Target))+" form="+I2S(GetUnitAbilityLevel(udg_M271Target,'Bply'))+" type="+I2S(GetUnitTypeId(udg_M271Target))+" order="+I2S(GetUnitCurrentOrder(udg_M271Target)))
endfunction
function M271Tick takes nothing returns nothing
 set udg_M271Tick=udg_M271Tick+1
 if udg_M271Tick==1 then
  call M271Mark("start")
  call SetUnitMoveSpeed(udg_M271Target,300.)
  call M271Mark("before")
 elseif udg_M271Tick==2 then
  call Preload("M271 tick=2 label=cast accepted="+I2S(GetUnitAbilityLevel(udg_M271Caster,'Aply')))
  call IssueTargetOrder(udg_M271Caster,"polymorph",udg_M271Target)
 elseif udg_M271Tick==10 then
  call M271Mark("morphed")
  call SetUnitMoveSpeed(udg_M271Target,1000.)
  call M271Mark("high")
 elseif udg_M271Tick==11 then
  call SetUnitMoveSpeed(udg_M271Target,1.)
  call M271Mark("low")
 elseif udg_M271Tick==12 then
  call SetUnitMoveSpeed(udg_M271Target,300.)
  call M271Mark("middle")
  call IssuePointOrder(udg_M271Target,"move",1600.,512.)
 elseif udg_M271Tick==14 then
  call M271Mark("moving")
  call UnitRemoveBuffs(udg_M271Target,false,true)
  call M271Mark("removed")
 elseif udg_M271Tick==16 then
  call M271Mark("complete")
  call PreloadGenEnd("move271.txt")
  call PauseTimer(udg_M271Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerTechResearched(Player(0),'Rhst',2)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_M271Caster=CreateUnit(Player(0),'hsor',256.,512.,0.)
 set udg_M271Target=CreateUnit(Player(1),'hP27',512.,512.,0.)
 call UnitRemoveAbility(udg_M271Caster,'Aatk')
 call UnitRemoveAbility(udg_M271Target,'Aatk')
 call SetUnitState(udg_M271Caster,UNIT_STATE_MANA,500.)
 set udg_M271Timer=CreateTimer()
 call TimerStart(udg_M271Timer,.2,true,function M271Tick)
endfunction
