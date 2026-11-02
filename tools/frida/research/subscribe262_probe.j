globals
 unit udg_S262Actor=null
 unit udg_S262Old=null
 unit udg_S262New=null
 timer udg_S262Timer=null
 integer udg_S262Tick=0
endglobals
function S262Mark takes string label returns nothing
 call Preload("S262 tick="+I2S(udg_S262Tick)+" label="+label+" scene="+I2S(udg_S262Tick)+" order="+I2S(GetUnitCurrentOrder(udg_S262Actor))+" x="+R2S(GetUnitX(udg_S262Actor))+" y="+R2S(GetUnitY(udg_S262Actor)))
endfunction
function S262Transfer takes nothing returns nothing
 call S262Mark("owner-before")
 call SetUnitOwner(udg_S262New,Player(0),false)
 call SetUnitOwner(udg_S262New,Player(1),false)
 call S262Mark("owner-after")
endfunction
function S262Tick takes nothing returns nothing
 set udg_S262Tick=udg_S262Tick+1
 if udg_S262Tick==1 then
  call S262Mark("start")
  call IssueTargetOrder(udg_S262Actor,"attack",udg_S262Old)
 elseif udg_S262Tick==2 then
  call S262Transfer()
 elseif udg_S262Tick==3 then
  call IssueImmediateOrder(udg_S262Actor,"stop")
 elseif udg_S262Tick==4 then
  call S262Transfer()
 elseif udg_S262Tick==5 then
  call IssuePointOrder(udg_S262Actor,"attack",1800.,256.)
 elseif udg_S262Tick==6 then
  call S262Transfer()
 elseif udg_S262Tick==7 then
  call IssueTargetOrder(udg_S262Actor,"attackonce",udg_S262Old)
 elseif udg_S262Tick==8 then
  call S262Transfer()
 elseif udg_S262Tick==9 then
  call S262Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_S262Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_S262Actor=CreateUnit(Player(0),'hfoo',512.,256.,0.)
 set udg_S262Old=CreateUnit(Player(1),'hfoo',1024.,256.,180.)
 set udg_S262New=CreateUnit(Player(0),'hfoo',640.,256.,180.)
 call SetUnitInvulnerable(udg_S262Actor,true)
 call SetUnitAcquireRange(udg_S262Actor,600.)
 set udg_S262Timer=CreateTimer()
 call TimerStart(udg_S262Timer,.1,true,function S262Tick)
endfunction
