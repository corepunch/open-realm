globals
 unit udg_Map022Unit=null
 timer udg_Map022Timer=null
 integer udg_Map022Tick=0
 integer udg_Map022Case=0
 integer udg_Map022Phase=0
 integer udg_Map022Cross=0
 integer array udg_Map022Type
 real array udg_Map022X
 real array udg_Map022Y
 string array udg_Map022Point
 string array udg_Map022TypeName
endglobals
// MAP-02.2 support-height probe. Public JASS output only; no observer state.
function Map022LocZ takes real x,real y returns real
 local location l=Location(x,y)
 local real z=GetLocationZ(l)
 call RemoveLocation(l)
 set l=null
 return z
endfunction
function Map022Record takes string label returns nothing
 local integer t=udg_Map022Case/@POINTS@
 local integer p=ModuloInteger(udg_Map022Case,@POINTS@)
 call Preload("MAP022 tick="+I2S(udg_Map022Tick)+" case="+I2S(udg_Map022Case)+" label="+label+" type="+udg_Map022TypeName[t]+" point="+udg_Map022Point[p]+" x="+R2S(GetUnitX(udg_Map022Unit))+" y="+R2S(GetUnitY(udg_Map022Unit))+" locz="+R2S(Map022LocZ(GetUnitX(udg_Map022Unit),GetUnitY(udg_Map022Unit)))+" fly="+R2S(GetUnitFlyHeight(udg_Map022Unit))+" order="+I2S(GetUnitCurrentOrder(udg_Map022Unit)))
endfunction
function Map022Tick takes nothing returns nothing
 local integer t
 local integer p
 set udg_Map022Tick=udg_Map022Tick+1
 if udg_Map022Case>=@CASES@ and udg_Map022Cross<=@CROSS_TICKS@ then
  // Public crossing: one Footman ordered from the west bank to the east bank.
  if udg_Map022Cross==0 then
   set udg_Map022Unit=CreateUnit(Player(0),'hfoo',@CROSS_FROM_X@,@CROSS_Y@,0.0)
   call IssuePointOrder(udg_Map022Unit,"move",@CROSS_TO_X@,@CROSS_Y@)
  endif
  if ModuloInteger(udg_Map022Cross,5)==0 then
   call Preload("MAP022 tick="+I2S(udg_Map022Tick)+" label=cross step="+I2S(udg_Map022Cross)+" x="+R2S(GetUnitX(udg_Map022Unit))+" y="+R2S(GetUnitY(udg_Map022Unit))+" locz="+R2S(Map022LocZ(GetUnitX(udg_Map022Unit),GetUnitY(udg_Map022Unit)))+" order="+I2S(GetUnitCurrentOrder(udg_Map022Unit)))
  endif
  set udg_Map022Cross=udg_Map022Cross+1
  return
 endif
 if udg_Map022Case>=@CASES@ then
  call Preload("MAP022 tick="+I2S(udg_Map022Tick)+" label=complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_Map022Timer)
  return
 endif
 set t=udg_Map022Case/@POINTS@
 set p=ModuloInteger(udg_Map022Case,@POINTS@)
 if udg_Map022Phase==0 then
  set udg_Map022Unit=CreateUnit(Player(0),udg_Map022Type[t],@STAGE_X@,@STAGE_Y@,0.0)
  call Map022Record("created")
 elseif udg_Map022Phase==1 then
  call SetUnitX(udg_Map022Unit,udg_Map022X[p])
  call SetUnitY(udg_Map022Unit,udg_Map022Y[p])
  call Map022Record("moved")
 elseif udg_Map022Phase==4 then
  call Map022Record("placed")
  call SetUnitX(udg_Map022Unit,udg_Map022X[p]+1.0)
 elseif udg_Map022Phase==7 then
  call Map022Record("nudged")
  call RemoveUnit(udg_Map022Unit)
  set udg_Map022Unit=null
 endif
 set udg_Map022Phase=udg_Map022Phase+1
 if udg_Map022Phase==8 then
  set udg_Map022Phase=0
  set udg_Map022Case=udg_Map022Case+1
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
@TABLES@
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1024.0,1024.0)
 call Preload("MAP022 tick=0 label=start variant=@VARIANT@ locz_dry="+R2S(Map022LocZ(@STAGE_X@,@STAGE_Y@)))
 set udg_Map022Timer=CreateTimer()
 call TimerStart(udg_Map022Timer,0.1,true,function Map022Tick)
endfunction
