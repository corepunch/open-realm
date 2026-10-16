globals
 timer udg_M316Timer=null
 integer udg_M316Tick=0
 integer udg_M316Scene=-1
 unit udg_M316Unit=null
 unit udg_M316Slow=null
 unit udg_M316Haste=null
 real udg_M316Source=0
endglobals
function M316Scalar takes real value returns string
 if value!=value then
  return "nan"
 endif
 return R2SW(value,1,4)
endfunction
function M316Mark takes string label returns nothing
 call Preload("M316 tick="+I2S(udg_M316Tick)+" scene="+I2S(udg_M316Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_M316Unit))+" x="+M316Scalar(GetUnitX(udg_M316Unit))+" y="+M316Scalar(GetUnitY(udg_M316Unit))+" speed="+M316Scalar(GetUnitMoveSpeed(udg_M316Unit))+" slow="+I2S(GetUnitAbilityLevel(udg_M316Unit,'Bslo'))+" haste="+I2S(GetUnitAbilityLevel(udg_M316Unit,'Bblo')))
 call PreloadGenEnd("@OUTPUT@")
endfunction
function M316Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_M316Tick+1-20,100)
 local unit source=null
 set udg_M316Tick=udg_M316Tick+1
 if udg_M316Tick==1 then
  call M316Mark("start")
  call PreloadGenEnd("modifiers316-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_M316Tick==20+100*@COUNT@ then
  call M316Mark("complete")
  call PauseTimer(udg_M316Timer)
 elseif udg_M316Tick>=20 then
  if age==0 then
   set udg_M316Scene=@FIRST@+(udg_M316Tick-20)/100
   call M316Mark("begin-setup")
   set source=CreateUnit(Player(0),'hSAA'+udg_M316Scene,1800,1800,0)
   set udg_M316Source=GetUnitDefaultFlyHeight(source)
   call RemoveUnit(source)
   set source=null
   set udg_M316Unit=CreateUnit(Player(0),'hTAA',768,512,0)
   call UnitRemoveAbility(udg_M316Unit,'Aatk')
   set udg_M316Slow=CreateUnit(Player(1),'hsor',512,384,0)
   call IssueImmediateOrder(udg_M316Slow,"slowoff")
   call UnitRemoveAbility(udg_M316Slow,'Aatk')
   set udg_M316Haste=CreateUnit(Player(0),'hsor',512,640,0)
   call UnitRemoveAbility(udg_M316Haste,'Aslo')
   call UnitAddAbility(udg_M316Haste,'ACbl')
   call IssueImmediateOrder(udg_M316Haste,"bloodlustoff")
   call UnitRemoveAbility(udg_M316Haste,'Aatk')
   call M316Mark("created")
  elseif age==1 then
   call SetUnitMoveSpeed(udg_M316Unit,udg_M316Source)
   call M316Mark("set-speed")
  elseif age==2 then
   call IssueTargetOrder(udg_M316Slow,"slow",udg_M316Unit)
   call M316Mark("slow-request")
  elseif age==15 then
   call IssueTargetOrder(udg_M316Haste,"bloodlust",udg_M316Unit)
   call M316Mark("haste-request")
  elseif age==20 then
   call IssuePointOrder(udg_M316Unit,"move",1536,1024)
   call M316Mark("move")
  elseif age==30 then
   call UnitRemoveBuffs(udg_M316Unit,true,false)
   call M316Mark("remove-haste")
  elseif age==40 then
   call UnitRemoveBuffs(udg_M316Unit,false,true)
   call M316Mark("remove-slow")
  elseif age==45 then
   call IssueImmediateOrder(udg_M316Unit,"stop")
   call SetUnitMoveSpeed(udg_M316Unit,270)
   call M316Mark("restore-stop")
  elseif age==50 then
   call PauseUnit(udg_M316Haste,false)
   call UnitResetCooldown(udg_M316Haste)
   call SetUnitState(udg_M316Haste,UNIT_STATE_MANA,1000)
   call SetUnitPosition(udg_M316Haste,GetUnitX(udg_M316Unit)-100,GetUnitY(udg_M316Unit)+100)
   call IssueTargetOrder(udg_M316Haste,"bloodlust",udg_M316Unit)
   call M316Mark("haste-second")
  elseif age==65 then
   call PauseUnit(udg_M316Slow,false)
   call UnitResetCooldown(udg_M316Slow)
   call SetUnitState(udg_M316Slow,UNIT_STATE_MANA,1000)
   call SetUnitPosition(udg_M316Slow,GetUnitX(udg_M316Unit)-100,GetUnitY(udg_M316Unit)-100)
   call IssueTargetOrder(udg_M316Slow,"slow",udg_M316Unit)
   call M316Mark("slow-second")
  elseif age==80 then
   call UnitRemoveBuffs(udg_M316Unit,false,true)
   call M316Mark("remove-slow-second")
  elseif age==85 then
   call UnitRemoveBuffs(udg_M316Unit,true,false)
   call M316Mark("remove-haste-second")
  elseif age==95 then
   call RemoveUnit(udg_M316Slow)
   call RemoveUnit(udg_M316Haste)
   call RemoveUnit(udg_M316Unit)
   set udg_M316Unit=null
   call M316Mark("end-cleanup")
  else
   call M316Mark("sample")
  endif
  if GetUnitAbilityLevel(udg_M316Unit,'Bslo')>0 then
   call PauseUnit(udg_M316Slow,true)
  endif
  if GetUnitAbilityLevel(udg_M316Unit,'Bblo')>0 then
   call PauseUnit(udg_M316Haste,true)
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_M316Timer=CreateTimer()
 call TimerStart(udg_M316Timer,0.1,true,function M316Tick)
endfunction
