globals
 unit udg_G338Actor=null
 unit udg_G338Old=null
 unit udg_G338New=null
 unit udg_G338Caster=null
 unit array udg_G338Extra
 integer array udg_G338Types
 integer udg_G338Tick=0
 integer udg_G338Scene=-1
 integer udg_G338Local=0
 timer udg_G338Timer=null
endglobals
function G338Mark takes string label returns nothing
 call Preload("G340 tick="+I2S(udg_G338Tick)+" scene="+I2S(udg_G338Scene)+" label="+label+" actor="+I2S(GetUnitTypeId(udg_G338Actor))+" target="+I2S(GetUnitTypeId(udg_G338New))+" order="+I2S(GetUnitCurrentOrder(udg_G338Actor))+" x="+R2SW(GetUnitX(udg_G338Actor),1,4)+" y="+R2SW(GetUnitY(udg_G338Actor),1,4))
endfunction
function G338Setup takes nothing returns nothing
 local integer kind=ModuloInteger(udg_G338Scene,3)
 local integer actor='hM40'
 local integer target='hfoo'
 local real distance=128.
 if kind==1 then
  set actor='hM41'
 elseif kind==2 then
  set actor='hM42'
 endif
 if ModuloInteger(udg_G338Scene/3,2)==1 then
  set target='hF40'
 endif
 if udg_G338Scene>=12 then
  set distance=288.
 endif
 set udg_G338Actor=CreateUnit(Player(2),actor,512.,512.,0.)
 set udg_G338New=CreateUnit(Player(2),target,512.+distance,512.,180.)
 call SetUnitInvulnerable(udg_G338Actor,true)
 call SetUnitAcquireRange(udg_G338Actor,1100.)
 call IssueImmediateOrder(udg_G338New,"holdposition")
 if ModuloInteger(udg_G338Scene/6,2)==1 then
  call UnitRemoveAbility(udg_G338Actor,'Amov')
 endif
 call G338Mark("setup")
endfunction
function G338Tick takes nothing returns nothing
 set udg_G338Tick=udg_G338Tick+1
 set udg_G338Local=udg_G338Local+1
 if udg_G338Local==1 then
  set udg_G338Scene=udg_G338Scene+1
  if udg_G338Scene==24 then
   call G338Mark("complete")
   call PreloadGenEnd("group340.txt")
   call PauseTimer(udg_G338Timer)
   return
  endif
  call G338Setup()
 elseif udg_G338Local==2 then
  call G338Mark("availability-before")
  call SetUnitOwner(udg_G338New,Player(1),false)
  call G338Mark("availability-after")
 elseif udg_G338Local==3 then
  call G338Mark("explicit-before")
  call IssueTargetOrder(udg_G338Actor,"attack",udg_G338New)
  call G338Mark("explicit-after")
 elseif udg_G338Local==4 then
  call G338Mark("after")
 elseif udg_G338Local==5 then
  call RemoveUnit(udg_G338Actor)
  call RemoveUnit(udg_G338New)
  set udg_G338Actor=null
  set udg_G338New=null
  set udg_G338Local=0
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(2),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(2),ALLIANCE_PASSIVE,false)
 call PreloadGenClear()
 call PreloadGenStart()
 call G338Mark("start")
 set udg_G338Timer=CreateTimer()
 call TimerStart(udg_G338Timer,.1,true,function G338Tick)
endfunction
