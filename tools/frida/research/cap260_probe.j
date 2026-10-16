globals
 unit array udg_C260Actor
 unit array udg_C260Target
 timer udg_C260Timer=null
 integer udg_C260Tick=0
endglobals
function C260Mark takes string label,integer scene returns nothing
 call Preload("C260 tick="+I2S(udg_C260Tick)+" label="+label+" scene="+I2S(scene)+" order="+I2S(GetUnitCurrentOrder(udg_C260Actor[scene]))+" x="+R2S(GetUnitX(udg_C260Actor[scene]))+" y="+R2S(GetUnitY(udg_C260Actor[scene])))
endfunction
function C260Tick takes nothing returns nothing
 local integer scene=0
 set udg_C260Tick=udg_C260Tick+1
 if udg_C260Tick==1 then
  call C260Mark("start",0)
  call IssuePointOrder(udg_C260Actor[1],"attack",1800.,768.)
  call IssuePointOrder(udg_C260Actor[2],"move",1800.,1280.)
  call PauseUnit(udg_C260Actor[3],true)
 elseif udg_C260Tick==2 then
  loop
   exitwhen scene==4
   call C260Mark("owner-before",scene)
   call SetUnitOwner(udg_C260Target[scene],Player(1),false)
   call C260Mark("owner-after",scene)
   call PauseUnit(udg_C260Actor[scene],true)
   set scene=scene+1
  endloop
 elseif udg_C260Tick==5 then
  call C260Mark("complete",0)
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_C260Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer scene=0
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 loop
  exitwhen scene==4
  set udg_C260Actor[scene]=CreateUnit(Player(0),'hfoo',512.,256.+I2R(scene)*512.,0.)
  set udg_C260Target[scene]=CreateUnit(Player(0),'hfoo',768.,256.+I2R(scene)*512.,180.)
  call SetUnitAcquireRange(udg_C260Actor[scene],300.)
  call SetUnitAcquireRange(udg_C260Target[scene],0.)
  set scene=scene+1
 endloop
 set udg_C260Timer=CreateTimer()
 call TimerStart(udg_C260Timer,.1,true,function C260Tick)
endfunction
