globals
 unit udg_AH153Victim=null
 unit udg_AH153Helper=null
 unit udg_AH153Source=null
 timer udg_AH153Timer=null
 integer udg_AH153Tick=0
 integer udg_AH153Case=-1
endglobals
function AH153Mark takes string label returns nothing
 call Preload("AH153 tick="+I2S(udg_AH153Tick)+" label="+label+" case="+I2S(udg_AH153Case)+" victim="+I2S(GetHandleId(udg_AH153Victim))+" owner="+I2S(GetPlayerId(GetOwningPlayer(udg_AH153Victim)))+" order="+I2S(GetUnitCurrentOrder(udg_AH153Victim))+" x="+R2S(GetUnitX(udg_AH153Victim))+" y="+R2S(GetUnitY(udg_AH153Victim))+" helperOrder="+I2S(GetUnitCurrentOrder(udg_AH153Helper)))
endfunction
function AH153New takes nothing returns nothing
 set udg_AH153Case=udg_AH153Case+1
 if udg_AH153Case==0 or udg_AH153Case==2 or udg_AH153Case==6 or udg_AH153Case==7 then
  if udg_AH153Victim!=null then
   call RemoveUnit(udg_AH153Victim)
  endif
  if udg_AH153Case==6 then
   set udg_AH153Victim=CreateUnit(Player(12),'hfoo',800.0,800.0,0.0)
  elseif udg_AH153Case==7 then
   set udg_AH153Victim=CreateUnit(Player(15),'hfoo',800.0,800.0,0.0)
  else
   set udg_AH153Victim=CreateUnit(Player(0),'hfoo',800.0,800.0,0.0)
  endif
 elseif udg_AH153Case==1 then
  call StartCampaignAI(Player(0),"attack_help153_probe.ai")
 elseif udg_AH153Case==3 or udg_AH153Case==4 then
  call SetUnitOwner(udg_AH153Victim,Player(2),false)
 elseif udg_AH153Case==5 then
  call SetUnitOwner(udg_AH153Victim,Player(0),false)
 endif
 call SetUnitAcquireRange(udg_AH153Victim,0.0)
 call IssueImmediateOrder(udg_AH153Victim,"holdposition")
 call AH153Mark("new")
endfunction
function AH153Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_AH153Tick+1,40)
 set udg_AH153Tick=udg_AH153Tick+1
 if udg_AH153Tick==1 then
  call AH153Mark("start")
  call AH153New()
 elseif phase==1 then
  call AH153New()
 endif
 if phase==10 or phase==11 or phase==15 or phase==16 or phase==17 or phase==39 then
  call AH153Mark("hit")
  call UnitDamageTarget(udg_AH153Source,udg_AH153Victim,0.0,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS)
 endif
 if udg_AH153Tick>=320 then
  call AH153Mark("complete")
  call PauseTimer(udg_AH153Timer)
  call PreloadGenEnd("@OUTPUT@")
 else
  call AH153Mark("sample")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 loop
  exitwhen i>=16
  call SetPlayerAlliance(Player(i),Player(1),ALLIANCE_HELP_REQUEST,true)
  call SetPlayerAlliance(Player(1),Player(i),ALLIANCE_HELP_RESPONSE,true)
  set i=i+1
 endloop
 call SetPlayerAlliance(Player(1),Player(3),ALLIANCE_PASSIVE,false)
 set udg_AH153Helper=CreateUnit(Player(1),'hfoo',1650.0,800.0,0.0)
 call SetUnitAcquireRange(udg_AH153Helper,0.0)
 call IssueImmediateOrder(udg_AH153Helper,"holdposition")
 set udg_AH153Source=CreateUnit(Player(3),'hfoo',1800.0,1800.0,0.0)
 call SetUnitAcquireRange(udg_AH153Source,0.0)
 call IssueImmediateOrder(udg_AH153Source,"holdposition")
 set udg_AH153Timer=CreateTimer()
 call TimerStart(udg_AH153Timer,0.1,true,function AH153Tick)
endfunction
