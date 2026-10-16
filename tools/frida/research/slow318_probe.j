globals
 timer udg_M318Timer=null
 integer udg_M318Tick=0
 unit array udg_M318Unit
 unit udg_M318Caster=null
 group udg_M318Group=null
 boolean udg_M318Held=false
endglobals
function M318Mark takes string label returns nothing
 local integer i=0
 loop
  exitwhen i==2
  call Preload("M318 tick="+I2S(udg_M318Tick)+" scene=0 label="+label+" u="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_M318Unit[i]))+" x="+R2SW(GetUnitX(udg_M318Unit[i]),1,4)+" y="+R2SW(GetUnitY(udg_M318Unit[i]),1,4)+" speed="+R2SW(GetUnitMoveSpeed(udg_M318Unit[i]),1,4)+" slow="+I2S(GetUnitAbilityLevel(udg_M318Unit[i],'Bslo')))
  set i=i+1
 endloop
 call PreloadGenEnd("@OUTPUT@")
endfunction
function M318Tick takes nothing returns nothing
 set udg_M318Tick=udg_M318Tick+1
 if udg_M318Tick==1 then
  call M318Mark("start")
  call PreloadGenEnd("slow318-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif udg_M318Tick==20 then
  call M318Mark("begin-setup")
  set udg_M318Unit[0]=CreateUnit(Player(0),'hTAA',768,512,0)
  set udg_M318Unit[1]=CreateUnit(Player(0),'hTAA',768,640,0)
  call UnitRemoveAbility(udg_M318Unit[0],'Aatk')
  call UnitRemoveAbility(udg_M318Unit[1],'Aatk')
  set udg_M318Caster=CreateUnit(Player(1),'hsor',640,576,0)
  call UnitRemoveAbility(udg_M318Caster,'Aatk')
  call IssueImmediateOrder(udg_M318Caster,"slowoff")
  set udg_M318Group=CreateGroup()
  call GroupAddUnit(udg_M318Group,udg_M318Unit[0])
  call GroupAddUnit(udg_M318Group,udg_M318Unit[1])
  call M318Mark("created")
 elseif udg_M318Tick==22 then
  call GroupPointOrder(udg_M318Group,"move",1536,1024)
  call M318Mark("move")
 elseif udg_M318Tick==30 then
  call IssueTargetOrder(udg_M318Caster,"slow",udg_M318Unit[0])
  call M318Mark("slow-request")
 elseif udg_M318Tick==55 then
  call UnitRemoveBuffs(udg_M318Unit[0],false,true)
  call M318Mark("remove-slow")
 elseif udg_M318Tick==110 then
  call M318Mark("complete")
  call PauseTimer(udg_M318Timer)
 elseif udg_M318Tick>20 then
  call M318Mark("sample")
 endif
 if not udg_M318Held and GetUnitAbilityLevel(udg_M318Unit[0],'Bslo')>0 then
  call PauseUnit(udg_M318Caster,true)
  set udg_M318Held=true
  call M318Mark("slow-applied")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_M318Timer=CreateTimer()
 call TimerStart(udg_M318Timer,0.1,true,function M318Tick)
endfunction
