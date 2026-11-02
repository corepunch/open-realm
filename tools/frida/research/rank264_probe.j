globals
 unit udg_R264Actor=null
 unit udg_R264Old=null
 unit udg_R264New=null
 unit udg_R264Dummy=null
 timer udg_R264Timer=null
 integer udg_R264Tick=0
endglobals
function R264Mark takes string label returns nothing
 call Preload("R264 tick="+I2S(udg_R264Tick)+" label="+label+" scene=0 variant=@VARIANT@ order="+I2S(GetUnitCurrentOrder(udg_R264Actor))+" x="+R2S(GetUnitX(udg_R264Actor))+" y="+R2S(GetUnitY(udg_R264Actor)))
endfunction
function R264Tick takes nothing returns nothing
 set udg_R264Tick=udg_R264Tick+1
 if udg_R264Tick==1 then
  call R264Mark("start")
  call IssuePointOrder(udg_R264Actor,"attack",1800.,256.)
 elseif udg_R264Tick==4 then
  call R264Mark("owner-before")
  call SetUnitOwner(udg_R264New,Player(1),false)
  call R264Mark("owner-after")
 elseif udg_R264Tick==5 then
  call R264Mark("next")
 elseif udg_R264Tick==8 then
  call R264Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_R264Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local real oldx=1100.
 local real newx=640.
 local integer newtype='hfoo'
 if "@VARIANT@"=="sticky" or "@VARIANT@"=="threat" then
  set oldx=640.
  set newx=608.
 elseif "@VARIANT@"=="farther" then
  set oldx=900.
  set newx=1100.
 elseif "@VARIANT@"=="worker" then
  set newtype='hpea'
 elseif "@VARIANT@"=="tie" then
  set newx=900.
 endif
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_R264Actor=CreateUnit(Player(0),'hfoo',512.,256.,0.)
 set udg_R264Old=CreateUnit(Player(1),'hfoo',oldx,256.,180.)
 set udg_R264New=CreateUnit(Player(0),newtype,newx,256.,180.)
 call SetUnitInvulnerable(udg_R264Actor,true)
 call SetUnitAcquireRange(udg_R264Actor,700.)
 if "@VARIANT@"=="threat" then
  set udg_R264Dummy=CreateUnit(Player(0),'hfoo',608.,400.,270.)
  call SetUnitAcquireRange(udg_R264Dummy,0.)
  call IssueTargetOrder(udg_R264Old,"attack",udg_R264Dummy)
 endif
 if "@VARIANT@"=="disarmed" then
  call UnitRemoveAbility(udg_R264New,'Aatk')
 elseif "@VARIANT@"=="immobile" then
  call UnitRemoveAbility(udg_R264New,'Amov')
 endif
 set udg_R264Timer=CreateTimer()
 call TimerStart(udg_R264Timer,.1,true,function R264Tick)
endfunction
