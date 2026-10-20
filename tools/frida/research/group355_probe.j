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
 call Preload("G355 tick="+I2S(udg_G338Tick)+" scene="+I2S(udg_G338Scene)+" label="+label+" actor="+I2S(GetUnitTypeId(udg_G338Actor))+" target="+I2S(GetUnitTypeId(udg_G338New))+" order="+I2S(GetUnitCurrentOrder(udg_G338Actor))+" stun="+I2S(GetUnitAbilityLevel(udg_G338New,'Bmlt'))+" x="+R2SW(GetUnitX(udg_G338Actor),1,4)+" y="+R2SW(GetUnitY(udg_G338Actor),1,4))
endfunction
function G338Setup takes nothing returns nothing
 local integer oldType='hM40'
 local integer newType='hM41'
 if udg_G338Scene==0 then
  set oldType='hM41'
 elseif udg_G338Scene==2 then
  set oldType='hM42'
 elseif udg_G338Scene==6 then
  set oldType='hM43'
 elseif udg_G338Scene==7 then
  set oldType='hM44'
 elseif udg_G338Scene==3 then
  set oldType='hM41'
  set newType='hM40'
 endif
 set udg_G338Actor=CreateUnit(Player(2),'hM41',512.,512.,0.)
 set udg_G338Old=CreateUnit(Player(1),oldType,576.,512.,180.)
 set udg_G338New=CreateUnit(Player(2),newType,1700.,512.,180.)
 call SetUnitInvulnerable(udg_G338Actor,true)
 call SetUnitAcquireRange(udg_G338Actor,1100.)
 call IssueImmediateOrder(udg_G338Old,"holdposition")
 call IssueImmediateOrder(udg_G338New,"holdposition")
 call IssuePointOrder(udg_G338Actor,"attack",1864.,512.)
 call G338Mark("setup")
endfunction
function G338Tick takes nothing returns nothing
 set udg_G338Tick=udg_G338Tick+1
 set udg_G338Local=udg_G338Local+1
 if udg_G338Local==1 then
  set udg_G338Scene=udg_G338Scene+1
  if udg_G338Scene==8 then
   call G338Mark("complete")
   call PreloadGenEnd("group355.txt")
   call PauseTimer(udg_G338Timer)
   return
  endif
  call G338Setup()
 elseif udg_G338Local==8 then
  call SetUnitX(udg_G338Old,1500.)
  call SetUnitX(udg_G338New,1400.)
  if udg_G338Scene==4 then
   call UnitRemoveAbility(udg_G338Old,'Aatk')
  elseif udg_G338Scene==5 then
   call PauseUnit(udg_G338Old,true)
  endif
  call G338Mark("availability-before")
  call SetUnitOwner(udg_G338New,Player(1),false)
  call G338Mark("availability-after")
 elseif udg_G338Local==10 then
  call G338Mark("inverse-before")
  call SetUnitOwner(udg_G338New,Player(3),false)
  call G338Mark("inverse-after")
 elseif udg_G338Local==20 then
  call G338Mark("end-before")
  call RemoveUnit(udg_G338Actor)
  call RemoveUnit(udg_G338Old)
  call RemoveUnit(udg_G338New)
  call RemoveUnit(udg_G338Caster)
  set udg_G338Actor=null
  set udg_G338Old=null
  set udg_G338New=null
  set udg_G338Caster=null
  call G338Mark("end-after")
  set udg_G338Local=0
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(2),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(2),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(0),Player(2),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(2),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(2),Player(3),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(3),Player(2),ALLIANCE_PASSIVE,false)
 call PreloadGenClear()
 call PreloadGenStart()
 call G338Mark("start")
 set udg_G338Timer=CreateTimer()
 call TimerStart(udg_G338Timer,.1,true,function G338Tick)
endfunction
