globals
 timer ft=null
 integer n=0
 fogmodifier keep=null
endglobals
function F253Bits takes real x returns string
 local player p=Player(0)
 return I2S(IntegerTertiaryOp(IsVisibleToPlayer(x,256.0,p),1,0))+I2S(IntegerTertiaryOp(IsFoggedToPlayer(x,256.0,p),1,0))+I2S(IntegerTertiaryOp(IsMaskedToPlayer(x,256.0,p),1,0))
endfunction
function F253LocBits takes real x returns string
 local player p=Player(0)
 local location l=Location(x,256.0)
 local string bits=I2S(IntegerTertiaryOp(IsLocationVisibleToPlayer(l,p),1,0))+I2S(IntegerTertiaryOp(IsLocationFoggedToPlayer(l,p),1,0))+I2S(IntegerTertiaryOp(IsLocationMaskedToPlayer(l,p),1,0))
 call RemoveLocation(l)
 set l=null
 return bits
endfunction
function F253Mark takes string label returns nothing
 call Preload("F253 tick="+I2S(n)+" label="+label+" keep="+F253Bits(256.0)+" stop="+F253Bits(512.0)+" destroy="+F253Bits(768.0)+" locations="+F253LocBits(256.0)+F253LocBits(512.0)+F253LocBits(768.0))
endfunction
function F253Tick takes nothing returns nothing
 local fogmodifier pulse=null
 local fogmodifier gone=null
 set n=n+1
 if n==20 then
  call F253Mark("before")
  call FogEnable(false)
  call FogMaskEnable(false)
  call F253Mark("display-disabled")
  call FogEnable(true)
  call FogMaskEnable(true)
  call F253Mark("display-restored")
 elseif n==21 then
  call F253Mark("before-start")
  set keep=CreateFogModifierRadius(Player(0),FOG_OF_WAR_VISIBLE,256.0,256.0,160.0,false,true)
  call FogModifierStart(keep)
  call F253Mark("after-start h="+I2S(GetHandleId(keep)))
  set pulse=CreateFogModifierRadius(Player(0),FOG_OF_WAR_VISIBLE,512.0,256.0,160.0,false,true)
  call FogModifierStart(pulse)
  call F253Mark("pulse-start h="+I2S(GetHandleId(pulse)))
  call FogModifierStop(pulse)
  call F253Mark("pulse-stop")
  set gone=CreateFogModifierRadius(Player(0),FOG_OF_WAR_VISIBLE,768.0,256.0,160.0,false,true)
  call FogModifierStart(gone)
  call F253Mark("destroy-start h="+I2S(GetHandleId(gone)))
  call DestroyFogModifier(gone)
  call F253Mark("after-destroy")
 elseif n==31 then
  call F253Mark("before-stop")
  call FogModifierStop(keep)
  call F253Mark("after-stop")
 elseif n==40 then
  call F253Mark("complete")
  call PreloadGenEnd("rs-target253.txt")
  call PauseTimer(ft)
 endif
 if n>=20 and n<40 then
  call F253Mark("sample")
 endif
endfunction
function F253Init takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(true)
 call FogMaskEnable(true)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 set ft=CreateTimer()
 call TimerStart(ft,0.1,true,function F253Tick)
endfunction
