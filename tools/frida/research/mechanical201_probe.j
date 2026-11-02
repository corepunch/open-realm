globals
 timer udg_M201Timer=null
 integer udg_M201Tick=0
 unit udg_M201Caster=null
 unit udg_M201Critter=null
endglobals
function M201Mark takes string label returns nothing
 call Preload("M201 tick="+I2S(udg_M201Tick)+" label="+label+" type="+I2S(GetUnitTypeId(udg_M201Critter))+" xy="+R2S(GetUnitX(udg_M201Critter))+","+R2S(GetUnitY(udg_M201Critter))+" owner="+I2S(GetPlayerId(GetOwningPlayer(udg_M201Critter)))+" buff="+I2S(GetUnitAbilityLevel(udg_M201Critter,'Bmec')))
endfunction
function M201Find takes nothing returns nothing
 if GetEnumUnit()!=udg_M201Caster then
  set udg_M201Critter=GetEnumUnit()
 endif
endfunction
function M201Tick takes nothing returns nothing
 local item it=null
 local group g=null
 set udg_M201Tick=udg_M201Tick+1
 if udg_M201Tick==1 then
  call M201Mark("start_file")
  call PreloadGenEnd("rs-mechanical201-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_M201Tick==10 then
  set it=UnitAddItemById(udg_M201Caster,'mcri')
  call Preload("M201 tick=10 label=item_created type="+I2S(GetItemTypeId(it)))
  if it==null then
   call Preload("M201 tick=10 label=item_missing")
  elseif UnitUseItem(udg_M201Caster,it) then
   call Preload("M201 tick=10 label=item_use accepted=1")
  else
   call Preload("M201 tick=10 label=item_use accepted=0")
  endif
 endif
 if udg_M201Tick==15 then
  set g=CreateGroup()
  call GroupEnumUnitsOfPlayer(g,Player(0),null)
  call ForGroup(g,function M201Find)
  call DestroyGroup(g)
  call M201Mark("created")
 endif
 if udg_M201Tick==20 then
  call SetUnitOwner(udg_M201Critter,Player(1),true)
  call M201Mark("owner_refresh")
 endif
 if udg_M201Tick==25 then
  call UnitRemoveAbility(udg_M201Critter,'Bmec')
  call M201Mark("buff_remove")
 endif
 if udg_M201Tick==30 then
  call SetUnitOwner(udg_M201Critter,Player(2),true)
  call M201Mark("inverse_refresh")
 endif
 call M201Mark("sample")
 if udg_M201Tick==50 then
  call M201Mark("complete")
  call PauseTimer(udg_M201Timer)
  call PreloadGenEnd("@OUTPUT@")
 endif
 set it=null
 set g=null
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_M201Caster=CreateUnit(Player(0),'Hpal',512,512,0)
 call SetUnitAcquireRange(udg_M201Caster,0)
 call M201Mark("start")
 set udg_M201Timer=CreateTimer()
 call TimerStart(udg_M201Timer,0.1,true,function M201Tick)
endfunction
