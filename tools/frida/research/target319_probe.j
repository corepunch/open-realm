globals
 timer udg_T319Timer=null
 integer udg_T319Tick=0
 unit array udg_T319Unit
 unit udg_T319Caster=null
 group udg_T319Group=null
 boolean udg_T319Held=false
endglobals
function T319Mark takes string label returns nothing
 local integer i=0
 loop
  exitwhen i==2
  call Preload("T319 tick="+I2S(udg_T319Tick)+" scene=0 label="+label+" u="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_T319Unit[i]))+" x="+R2SW(GetUnitX(udg_T319Unit[i]),1,4)+" y="+R2SW(GetUnitY(udg_T319Unit[i]),1,4)+" speed="+R2SW(GetUnitMoveSpeed(udg_T319Unit[i]),1,4)+" slow="+I2S(GetUnitAbilityLevel(udg_T319Unit[i],'Bslo')))
  set i=i+1
 endloop
 call PreloadGenEnd("@OUTPUT@")
endfunction
function T319Tick takes nothing returns nothing
 set udg_T319Tick=udg_T319Tick+1
 if udg_T319Tick==1 then
  call T319Mark("start")
  call PreloadGenEnd("target319-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif udg_T319Tick==20 then
  call T319Mark("begin-setup")
  set udg_T319Unit[0]=CreateUnit(Player(0),'hTAA',768,512,0)
  set udg_T319Unit[1]=CreateUnit(Player(0),'hTAA',1536,1024,0)
  call UnitRemoveAbility(udg_T319Unit[0],'Aatk')
  call UnitRemoveAbility(udg_T319Unit[1],'Aatk')
  set udg_T319Caster=CreateUnit(Player(1),'hsor',640,576,0)
  call UnitRemoveAbility(udg_T319Caster,'Aatk')
  call IssueImmediateOrder(udg_T319Caster,"slowoff")
  set udg_T319Group=CreateGroup()
  call GroupAddUnit(udg_T319Group,udg_T319Unit[0])
  call GroupAddUnit(udg_T319Group,udg_T319Unit[1])
  call T319Mark("created")
 elseif udg_T319Tick==22 then
  call IssueTargetOrder(udg_T319Unit[0],"move",udg_T319Unit[1])
  call T319Mark("move")
 elseif udg_T319Tick==30 then
  call IssueTargetOrder(udg_T319Caster,"slow",udg_T319Unit[0])
  call T319Mark("slow-request")
 elseif udg_T319Tick==55 then
  call UnitRemoveBuffs(udg_T319Unit[0],false,true)
  call T319Mark("remove-slow")
 elseif udg_T319Tick==65 then
  call IssuePointOrder(udg_T319Unit[1],"move",1536,1536)
  call T319Mark("target-move")
 elseif udg_T319Tick==100 then
  call IssueImmediateOrder(udg_T319Unit[0],"stop")
  call T319Mark("stop")
 elseif udg_T319Tick==110 then
  call T319Mark("complete")
  call PauseTimer(udg_T319Timer)
 elseif udg_T319Tick>20 then
  call T319Mark("sample")
 endif
 if not udg_T319Held and GetUnitAbilityLevel(udg_T319Unit[0],'Bslo')>0 then
  call PauseUnit(udg_T319Caster,true)
  set udg_T319Held=true
  call T319Mark("slow-applied")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_T319Timer=CreateTimer()
 call TimerStart(udg_T319Timer,0.1,true,function T319Tick)
endfunction
