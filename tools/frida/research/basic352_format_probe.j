globals
 timer udg_F352Timer=null
 integer udg_F352Tick=0
 integer udg_F352Case=0
 unit udg_F352Unit=null
endglobals
function F352Mark takes string label returns nothing
 call Preload("F352 tick="+I2S(udg_F352Tick)+" label="+label)
endfunction
function F352Value takes real a returns nothing
 local integer wi=0
 local integer pi=0
 local integer w=0
 local integer p=0
 loop
  exitwhen wi==6
  if wi==0 then
   set w=-5
  elseif wi==1 then
   set w=0
  elseif wi==2 then
   set w=1
  elseif wi==3 then
   set w=8
  elseif wi==4 then
   set w=16
  else
   set w=64
  endif
  set pi=0
  loop
   exitwhen pi==7
   if pi==0 then
    set p=-7
   elseif pi==1 then
    set p=-1
   elseif pi==2 then
    set p=0
   elseif pi==3 then
    set p=1
   elseif pi==4 then
    set p=3
   elseif pi==5 then
    set p=9
   else
    set p=17
   endif
   call F352Mark("case="+I2S(udg_F352Case)+" value=["+R2SW(a,w,p)+"]")
   set udg_F352Case=udg_F352Case+1
   set pi=pi+1
  endloop
  set wi=wi+1
 endloop
endfunction
function F352Tick takes nothing returns nothing
 set udg_F352Tick=udg_F352Tick+1
 if udg_F352Tick==1 then
  call F352Mark("start")
 elseif udg_F352Tick==2 then
  call F352Value(-0.)
 elseif udg_F352Tick==3 then
  call F352Value(0.5)
 elseif udg_F352Tick==4 then
  call F352Value(-0.5)
 elseif udg_F352Tick==5 then
  call F352Value(1.9995)
 elseif udg_F352Tick==6 then
  call F352Value(-1.9995)
 elseif udg_F352Tick==7 then
  call F352Value(0.125)
 elseif udg_F352Tick==8 then
  call F352Value(16777217.)
 elseif udg_F352Tick==9 then
  call F352Value(I2R(2147483647))
 elseif udg_F352Tick==10 then
  call F352Value(I2R(-2147483648))
 elseif udg_F352Tick==11 then
  call F352Value(I2R(2147483647)*2.)
 elseif udg_F352Tick==20 then
  set udg_F352Unit=CreateUnit(Player(0),'hfoo',768.,768.,0.)
  call UnitRemoveAbility(udg_F352Unit,'Aatk')
 elseif udg_F352Tick==21 then
  call F352Mark("move-text=["+R2SW(1152.125,8,3)+"] parsed="+R2S(S2R(R2SW(1152.125,8,3))))
  call IssuePointOrder(udg_F352Unit,"move",S2R(R2SW(1152.125,8,3)),768.)
 elseif udg_F352Tick==40 or udg_F352Tick==60 or udg_F352Tick==80 then
  call F352Mark("unit x="+R2S(GetUnitX(udg_F352Unit))+" y="+R2S(GetUnitY(udg_F352Unit))+" order="+I2S(GetUnitCurrentOrder(udg_F352Unit)))
 endif
 if udg_F352Tick==80 then
  call F352Mark("complete")
  call PreloadGenEnd("basic352-format.txt")
  call PauseTimer(udg_F352Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0.)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_F352Timer=CreateTimer()
 call TimerStart(udg_F352Timer,0.1,true,function F352Tick)
endfunction
