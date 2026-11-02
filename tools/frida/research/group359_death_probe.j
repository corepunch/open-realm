globals
 unit udg_G338Actor=null
 unit udg_G338Old=null
 unit udg_G338New=null
 unit udg_G338Caster=null
 integer udg_G338Tick=0
 integer udg_G338Scene=-1
 integer udg_G338Local=0
 integer array udg_G338Types
 timer udg_G338Timer=null
endglobals
function G338Mark takes string label returns nothing
 call Preload("O359 tick="+I2S(udg_G338Tick)+" scene="+I2S(udg_G338Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_G338New))+" freeze="+I2S(GetUnitAbilityLevel(udg_G338New,'Bfre'))+" issued="+I2S(udg_G338Types[0])+" x="+R2SW(GetUnitX(udg_G338New),1,4)+" life="+R2SW(GetWidgetLife(udg_G338New),1,4)+" y="+R2SW(GetUnitY(udg_G338New),1,4))
endfunction
function G338Tick takes nothing returns nothing
 set udg_G338Tick=udg_G338Tick+1
 set udg_G338Local=udg_G338Local+1
 if udg_G338Local==1 then
  set udg_G338Scene=udg_G338Scene+1
  if udg_G338Scene==2 then
   call G338Mark("complete")
   call PreloadGenEnd("death359.txt")
   call PauseTimer(udg_G338Timer)
   return
  endif
  set udg_G338Types[0]=0
  set udg_G338Actor=CreateUnit(Player(0),'hrif',1280.,512.,180.)
  set udg_G338Caster=udg_G338Actor
  set udg_G338New=CreateUnit(Player(2),'hfoo',1024.,512.,0.)
  call SetWidgetLife(udg_G338New,1.)
  call SetUnitInvulnerable(udg_G338Actor,true)
  call UnitAddAbility(udg_G338Actor,'AIzb')
  if udg_G338Scene==0 then
   call IssuePointOrder(udg_G338New,"move",1024.,1536.)
  else
   call IssueTargetOrder(udg_G338New,"attack",udg_G338Actor)
  endif
  call G338Mark("setup")
 elseif udg_G338Local==3 then
  call G338Mark("cast-before")
  call IssueTargetOrder(udg_G338Actor,"attack",udg_G338New)
  call G338Mark("cast-after")
 elseif udg_G338Local==9 then
  call G338Mark("frozen")
 elseif udg_G338Local==10 then
  call PauseUnit(udg_G338Actor,true)
  call G338Mark("source-paused")
 elseif udg_G338Local==15 then
  call G338Mark("move-before")
  if IssuePointOrder(udg_G338New,"move",1856.,1024.) then
   set udg_G338Types[0]=1
  endif
  call G338Mark("move-after")
 elseif udg_G338Local==20 then
  call G338Mark("remove-before")
  call UnitRemoveBuffs(udg_G338New,false,true)
  call G338Mark("remove-after")
 elseif udg_G338Local==25 or udg_G338Local==30 then
  call G338Mark("continuation")
 elseif udg_G338Local==35 then
  call RemoveUnit(udg_G338New)
  call RemoveUnit(udg_G338Actor)
  set udg_G338New=null
  set udg_G338Actor=null
  set udg_G338Caster=null
  call G338Mark("cleanup")
  set udg_G338Local=0
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call SetRandomSeed(1)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(2),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(2),Player(0),ALLIANCE_PASSIVE,false)
 call PreloadGenClear()
 call PreloadGenStart()
 call G338Mark("start")
 set udg_G338Timer=CreateTimer()
 call TimerStart(udg_G338Timer,.1,true,function G338Tick)
endfunction
