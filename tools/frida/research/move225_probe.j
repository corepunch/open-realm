globals
 unit udg_M225H=null
 unit udg_M225N=null
 item udg_M225I=null
 timer udg_M225Timer=null
 integer udg_M225Tick=0
endglobals
function M225Mark takes string label returns nothing
 call Preload("M225 tick="+I2S(udg_M225Tick)+" scene=0 label="+label+" default="+R2S(GetUnitDefaultMoveSpeed(udg_M225H))+" speed="+R2S(GetUnitMoveSpeed(udg_M225H))+" baseagi="+I2S(GetHeroAgi(udg_M225H,false))+" agi="+I2S(GetHeroAgi(udg_M225H,true))+" level="+I2S(GetHeroLevel(udg_M225H))+" nonhero="+R2S(GetUnitDefaultMoveSpeed(udg_M225N))+" x="+R2S(GetUnitX(udg_M225H))+" y="+R2S(GetUnitY(udg_M225H)))
endfunction
function M225Tick takes nothing returns nothing
 local integer t=udg_M225Tick
 if t==0 then
  set udg_M225H=CreateUnit(Player(0),'Hpal',128,128,0)
  set udg_M225N=CreateUnit(Player(0),'hfoo',128,512,0)
  call SetUnitAcquireRange(udg_M225H,0)
  call SetUnitAcquireRange(udg_M225N,0)
  call M225Mark("created")
 elseif t==1 then
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif t==5 then
  call SetHeroLevel(udg_M225H,3,false)
  call M225Mark("level3")
 elseif t==10 then
  call SetHeroAgi(udg_M225H,77,true)
  call M225Mark("agi77")
 elseif t==15 then
  set udg_M225I=UnitAddItemById(udg_M225H,'belv')
  call M225Mark("item")
 elseif t==20 then
  call SetUnitMoveSpeed(udg_M225H,200)
  call M225Mark("set_speed")
 elseif t==25 then
  call SetHeroAgi(udg_M225H,51,false)
  call M225Mark("agi51")
 elseif t==30 then
  call UnitRemoveItem(udg_M225H,udg_M225I)
  call RemoveItem(udg_M225I)
  call M225Mark("drop")
 elseif t==35 then
  call SetHeroLevel(udg_M225H,4,false)
  call M225Mark("level4")
 elseif t==40 then
  call UnitStripHeroLevel(udg_M225H,2)
  call M225Mark("strip")
 elseif t==45 then
  call IssuePointOrder(udg_M225H,"move",1600,128)
  call M225Mark("move")
 elseif t==60 then
  call IssueImmediateOrder(udg_M225H,"stop")
  call M225Mark("stop")
 elseif t==65 then
  call RemoveUnit(udg_M225H)
  call RemoveUnit(udg_M225N)
 elseif t==70 then
  call M225Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_M225Timer)
 endif
 if t<65 then
  call M225Mark("sample")
 endif
 set udg_M225Tick=t+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 set udg_M225Timer=CreateTimer()
 call TimerStart(udg_M225Timer,0.1,true,function M225Tick)
 call Preload("M225 tick=0 scene=0 label=start")
endfunction
