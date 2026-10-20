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
 local integer abilityCode=0
 if udg_G338Scene>=0 and udg_G338Scene<7 then
  set abilityCode=udg_G338Types[udg_G338Scene]
 endif
 call Preload("G367 tick="+I2S(udg_G338Tick)+" scene="+I2S(udg_G338Scene)+" label="+label+" code="+I2S(abilityCode)+" result="+I2S(udg_G338Types[100])+" level="+I2S(GetUnitAbilityLevel(udg_G338New,abilityCode))+" order="+I2S(GetUnitCurrentOrder(udg_G338New)))
endfunction
function G338Tick takes nothing returns nothing
 set udg_G338Tick=udg_G338Tick+1
 set udg_G338Local=udg_G338Local+1
 if udg_G338Local==1 then
  set udg_G338Scene=udg_G338Scene+1
  set udg_G338Types[100]=0
  if udg_G338Scene==7 then
   call G338Mark("complete")
   call PreloadGenEnd("group367.txt")
   call PauseTimer(udg_G338Timer)
   return
  endif
  set udg_G338New=CreateUnit(Player(2),'hfoo',1024.,512.,0.)
  call G338Mark("created")
 elseif udg_G338Local==3 then
  call G338Mark("add-before")
  if UnitAddAbility(udg_G338New,udg_G338Types[udg_G338Scene]) then
   set udg_G338Types[100]=1
  endif
  call G338Mark("add-after")
 elseif udg_G338Local==5 then
  set udg_G338Types[100]=0
  if IssuePointOrder(udg_G338New,"move",1792.,512.) then
   set udg_G338Types[100]=1
  endif
  call G338Mark("move-after")
 elseif udg_G338Local==8 then
  call G338Mark("remove-before")
  set udg_G338Types[100]=0
  if UnitRemoveAbility(udg_G338New,udg_G338Types[udg_G338Scene]) then
   set udg_G338Types[100]=1
  endif
  call G338Mark("remove-after")
 elseif udg_G338Local==12 then
  call RemoveUnit(udg_G338New)
  set udg_G338New=null
  call G338Mark("cleanup")
  set udg_G338Local=0
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call SetRandomSeed(1)
 call FogEnable(false)
 call FogMaskEnable(false)
 set udg_G338Types[0]='BSTN'
 set udg_G338Types[1]='BUst'
 set udg_G338Types[2]='BUsp'
 set udg_G338Types[3]='BUsl'
 set udg_G338Types[4]='BPSE'
 set udg_G338Types[5]='A367'
 set udg_G338Types[6]='AUsl'
 call PreloadGenClear()
 call PreloadGenStart()
 call G338Mark("start")
 set udg_G338Timer=CreateTimer()
 call TimerStart(udg_G338Timer,.1,true,function G338Tick)
endfunction
