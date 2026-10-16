globals
 unit udg_G338Actor=null
 unit udg_G338Old=null
 unit udg_G338New=null
 unit array udg_G338Extra
 integer array udg_G338Types
 integer udg_G338Tick=0
 integer udg_G338Scene=-1
 integer udg_G338Local=0
 timer udg_G338Timer=null
endglobals
function G338Mark takes string label returns nothing
 call Preload("G338 tick="+I2S(udg_G338Tick)+" scene="+I2S(udg_G338Scene)+" label="+label+" actor="+I2S(GetUnitTypeId(udg_G338Actor))+" target="+I2S(GetUnitTypeId(udg_G338New))+" order="+I2S(GetUnitCurrentOrder(udg_G338Actor))+" x="+R2SW(GetUnitX(udg_G338Actor),1,4)+" y="+R2SW(GetUnitY(udg_G338Actor),1,4))
endfunction
function G338Setup takes nothing returns nothing
 local integer owner=0
 local integer kind=ModuloInteger(udg_G338Scene,38)
 local integer unitId=udg_G338Types[kind]
 local integer actor='hfoo'
 local real oldx=1100.
 local real newx=640.
 local integer i=0
 if udg_G338Scene>=38 then
  set owner=2
 endif
 if udg_G338Scene>=76 then
  set owner=12
  set actor='Hpal'
  set unitId='hfoo'
 endif
 if udg_G338Scene>=78 then
  set owner=2
  set actor='hmtm'
  set oldx=1500.
  set newx=1000.
 endif
 set udg_G338Actor=CreateUnit(Player(owner),actor,512.,512.,0.)
 set udg_G338Old=CreateUnit(Player(1),'hfoo',oldx,512.,180.)
 set udg_G338New=CreateUnit(Player(owner),unitId,newx,512.,180.)
 call SetUnitInvulnerable(udg_G338Actor,true)
 call SetUnitAcquireRange(udg_G338Actor,1100.)
 call IssueImmediateOrder(udg_G338Old,"holdposition")
 call IssueImmediateOrder(udg_G338New,"holdposition")
 if udg_G338Scene==76 or udg_G338Scene==77 then
  call SetHeroLevel(udg_G338Actor,udg_G338Scene-76+6,false)
 endif
 if udg_G338Scene>=78 then
  loop
   exitwhen i>=udg_G338Scene-78
   set udg_G338Extra[i]=CreateUnit(Player(owner),'hfoo',newx+I2R(i)*8.,560.,180.)
   call IssueImmediateOrder(udg_G338Extra[i],"holdposition")
   set i=i+1
  endloop
 endif
 call G338Mark("setup")
 call IssuePointOrder(udg_G338Actor,"attack",1800.,512.)
endfunction
function G338Clean takes nothing returns nothing
 local integer i=0
 call RemoveUnit(udg_G338Actor)
 call RemoveUnit(udg_G338Old)
 call RemoveUnit(udg_G338New)
 set udg_G338Actor=null
 set udg_G338Old=null
 set udg_G338New=null
 loop
  exitwhen i>=10
  if udg_G338Extra[i]!=null then
   call RemoveUnit(udg_G338Extra[i])
   set udg_G338Extra[i]=null
  endif
  set i=i+1
 endloop
endfunction
function G338Tick takes nothing returns nothing
 set udg_G338Tick=udg_G338Tick+1
 set udg_G338Local=udg_G338Local+1
 if udg_G338Local==1 then
  set udg_G338Scene=udg_G338Scene+1
  if udg_G338Scene==84 then
   call G338Mark("complete")
   call PreloadGenEnd("group338.txt")
   call PauseTimer(udg_G338Timer)
   return
  endif
  call G338Setup()
 elseif udg_G338Local==4 then
  call G338Mark("owner-before")
  call SetUnitOwner(udg_G338New,Player(1),false)
  call G338Mark("owner-after")
 elseif udg_G338Local==6 then
  call G338Mark("after")
 elseif udg_G338Local==8 then
  call G338Clean()
  set udg_G338Local=0
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12.)
 call SetTimeOfDayScale(0.)
 call PreloadGenClear()
 call PreloadGenStart()
 loop
  exitwhen i==16
  if i!=1 then
   call SetPlayerAlliance(Player(i),Player(1),ALLIANCE_PASSIVE,false)
   call SetPlayerAlliance(Player(1),Player(i),ALLIANCE_PASSIVE,false)
  endif
  set i=i+1
 endloop
 set udg_G338Types[0]='hfoo'
 set udg_G338Types[1]='hpea'
 set udg_G338Types[2]='Hpal'
 set udg_G338Types[3]='hhou'
 set udg_G338Types[4]='hlum'
 set udg_G338Types[5]='htow'
 set udg_G338Types[6]='hkee'
 set udg_G338Types[7]='hcas'
 set udg_G338Types[8]='hbar'
 set udg_G338Types[9]='hvlt'
 set udg_G338Types[10]='oalt'
 set udg_G338Types[11]='halt'
 set udg_G338Types[12]='eate'
 set udg_G338Types[13]='uaod'
 set udg_G338Types[14]='edob'
 set udg_G338Types[15]='ogre'
 set udg_G338Types[16]='ofrt'
 set udg_G338Types[17]='ostr'
 set udg_G338Types[18]='etol'
 set udg_G338Types[19]='etoa'
 set udg_G338Types[20]='etoe'
 set udg_G338Types[21]='unpl'
 set udg_G338Types[22]='unp1'
 set udg_G338Types[23]='unp2'
 set udg_G338Types[24]='ugol'
 set udg_G338Types[25]='egol'
 set udg_G338Types[26]='otrb'
 set udg_G338Types[27]='uzig'
 set udg_G338Types[28]='uzg1'
 set udg_G338Types[29]='uzg2'
 set udg_G338Types[30]='eden'
 set udg_G338Types[31]='ovln'
 set udg_G338Types[32]='utom'
 set udg_G338Types[33]='emow'
 set udg_G338Types[34]='ohwd'
 set udg_G338Types[35]='otot'
 set udg_G338Types[36]='edry'
 set udg_G338Types[37]='hspt'
 call G338Mark("start")
 set udg_G338Timer=CreateTimer()
 call TimerStart(udg_G338Timer,.1,true,function G338Tick)
endfunction
