globals
 unit udg_S323Unit=null
 unit udg_S323Target=null
 item udg_S323Item=null
 timer udg_S323Timer=null
 integer udg_S323Tick=0
 integer udg_S323Scene=0
endglobals
function S323Mark takes string label returns nothing
 call Preload("S323 tick="+I2S(udg_S323Tick)+" scene="+I2S(udg_S323Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_S323Unit))+" order="+I2S(GetUnitCurrentOrder(udg_S323Unit))+" x="+R2S(GetUnitX(udg_S323Unit))+" y="+R2S(GetUnitY(udg_S323Unit)))
endfunction
function S323Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_S323Tick,90)
 local boolean ok=false
 if t==0 then
  if udg_S323Scene==2 then
   set udg_S323Unit=CreateUnit(Player(0),'edoc',640,1024,0)
  elseif udg_S323Scene==3 then
   set udg_S323Unit=CreateUnit(Player(0),'edot',640,1024,0)
  elseif udg_S323Scene==5 then
   set udg_S323Unit=CreateUnit(Player(0),'Hpal',384,1024,0)
  elseif udg_S323Scene==6 then
   set udg_S323Unit=CreateUnit(Player(0),'ewsp',640,1024,0)
   set udg_S323Target=CreateUnit(Player(0),'egol',896,1024,0)
   call SetResourceAmount(udg_S323Target,100000)
  else
   set udg_S323Unit=CreateUnit(Player(0),'hfoo',640,1024,0)
  endif
  call SetUnitAcquireRange(udg_S323Unit,0)
  call SetUnitState(udg_S323Unit,UNIT_STATE_MANA,1000)
  if udg_S323Scene==0 then
   call UnitAddAbility(udg_S323Unit,'A323')
  elseif udg_S323Scene==1 then
   call UnitAddAbility(udg_S323Unit,'ASch')
  elseif udg_S323Scene==4 then
   call UnitAddAbility(udg_S323Unit,'ASc1')
  elseif udg_S323Scene==5 then
   call UnitAddAbility(udg_S323Unit,'AInv')
   set udg_S323Item=UnitAddItemById(udg_S323Unit,'stwp')
   set udg_S323Target=CreateUnit(Player(0),'htow',1600,1024,0)
  endif
  call S323Mark("created")
 elseif t==2 and udg_S323Scene==7 then
  call StartCampaignAI(Player(0),"Scripts\\separation323.ai")
  call S323Mark("start-ai")
 elseif t==5 then
  if udg_S323Scene==0 then
   set ok=IssueImmediateOrder(udg_S323Unit,"avatar")
  elseif udg_S323Scene==1 or udg_S323Scene==4 then
   set ok=IssueImmediateOrder(udg_S323Unit,"channel")
  elseif udg_S323Scene==2 then
   set ok=IssueImmediateOrder(udg_S323Unit,"bearform")
  elseif udg_S323Scene==3 then
   set ok=IssueImmediateOrder(udg_S323Unit,"ravenform")
  elseif udg_S323Scene==5 then
   set ok=UnitUseItemTarget(udg_S323Unit,udg_S323Item,udg_S323Target)
  elseif udg_S323Scene==6 then
   set ok=IssueTargetOrder(udg_S323Unit,"harvest",udg_S323Target)
  else
   call UnitApplyTimedLife(udg_S323Unit,'BTLF',20)
   set ok=true
  endif
  if ok then
   call S323Mark("accepted")
  else
   call S323Mark("rejected")
  endif

 elseif t==40 then
  if udg_S323Scene==2 then
   call IssueImmediateOrder(udg_S323Unit,"unbearform")
  elseif udg_S323Scene==3 then
   call IssueImmediateOrder(udg_S323Unit,"unravenform")
  elseif udg_S323Scene==6 then
   call KillUnit(udg_S323Target)
  endif
  call S323Mark("inverse")
 elseif t==80 then
  call RemoveUnit(udg_S323Unit)
  if udg_S323Target!=null then
   call RemoveUnit(udg_S323Target)
   set udg_S323Target=null
  endif
  call S323Mark("removed")
 elseif t==89 then
  if udg_S323Scene==7 then
   call S323Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_S323Timer)
  endif
  set udg_S323Scene=udg_S323Scene+1
 endif
 if t<80 then
  call S323Mark("sample")
 endif
 set udg_S323Tick=udg_S323Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1323)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetPlayerTechResearched(Player(0),'Redc',3)
 call SetPlayerTechResearched(Player(0),'Redt',3)
 set udg_S323Timer=CreateTimer()
 call TimerStart(udg_S323Timer,0.1,true,function S323Tick)
 call S323Mark("start")
endfunction
