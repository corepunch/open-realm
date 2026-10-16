globals
 unit udg_AH152Victim=null
 unit udg_AH152Source=null
 unit array udg_AH152Helpers
 timer udg_AH152Timer=null
 integer udg_AH152Tick=0
 integer udg_AH152Case=-1
endglobals
function AH152Mark takes string label returns nothing
 call Preload("AH152 tick="+I2S(udg_AH152Tick)+" label="+label+" case="+I2S(udg_AH152Case)+" victim="+I2S(GetHandleId(udg_AH152Victim)))
endfunction
function AH152New takes nothing returns nothing
 local integer i=0
 local player owner=Player(0)
 local integer typeId='hfoo'
 local real radius=173.0
 if udg_AH152Case>=0 then
  call RemoveUnit(udg_AH152Victim)
  loop
   exitwhen i>=7
   call RemoveUnit(udg_AH152Helpers[i])
   set i=i+1
  endloop
 endif
 set udg_AH152Case=udg_AH152Case+1
 if udg_AH152Case==1 then
  set owner=Player(12)
  set radius=229.0
 elseif udg_AH152Case==2 then
  set typeId='hpea'
 endif
 call SetPlayerAlliance(owner,Player(1),ALLIANCE_HELP_REQUEST,true)
 call SetPlayerAlliance(Player(1),owner,ALLIANCE_HELP_RESPONSE,true)
 call SetPlayerAlliance(Player(1),Player(3),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(3),owner,ALLIANCE_PASSIVE,false)
 set udg_AH152Victim=CreateUnit(owner,typeId,800.0,800.0,0.0)
 call SetUnitX(udg_AH152Victim,800.0)
 call SetUnitY(udg_AH152Victim,800.0)
 call SetUnitAcquireRange(udg_AH152Victim,0.0)
 call IssueImmediateOrder(udg_AH152Victim,"holdposition")
 set i=0
 loop
  exitwhen i>=7
  set udg_AH152Helpers[i]=CreateUnit(Player(1),'hfoo',400.0+I2R(i)*80.0,400.0,0.0)
  call SetUnitX(udg_AH152Helpers[i],800.0+radius+I2R(i)-3.0)
  call SetUnitY(udg_AH152Helpers[i],800.0)
  call SetUnitAcquireRange(udg_AH152Helpers[i],0.0)
  call IssueImmediateOrder(udg_AH152Helpers[i],"holdposition")
  call Preload("AH152 helper="+I2S(i)+" id="+I2S(GetHandleId(udg_AH152Helpers[i]))+" x="+R2S(GetUnitX(udg_AH152Helpers[i]))+" y="+R2S(GetUnitY(udg_AH152Helpers[i])))
  set i=i+1
 endloop
 call AH152Mark("new")
endfunction
function AH152Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_AH152Tick+1,50)
 set udg_AH152Tick=udg_AH152Tick+1
 if udg_AH152Tick==1 then
  call AH152Mark("start")
  call AH152New()
 elseif phase==1 and udg_AH152Case<2 then
  call AH152New()
 elseif phase==10 or phase==11 or phase==39 or phase==40 or phase==41 then
  call AH152Mark("hit")
  call UnitDamageTarget(udg_AH152Source,udg_AH152Victim,0.0,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS)
 endif
 if udg_AH152Tick>=150 then
  call AH152Mark("complete")
  call PauseTimer(udg_AH152Timer)
  call PreloadGenEnd("@OUTPUT@")
 else
  call AH152Mark("sample")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_AH152Source=CreateUnit(Player(3),'hfoo',1800.0,1800.0,0.0)
 call SetUnitAcquireRange(udg_AH152Source,0.0)
 call IssueImmediateOrder(udg_AH152Source,"holdposition")
 set udg_AH152Timer=CreateTimer()
 call TimerStart(udg_AH152Timer,0.1,true,function AH152Tick)
endfunction
