globals
 unit udg_Route012Unit=null
 timer udg_Route012Timer=null
 integer udg_Route012Tick=0
endglobals

function Route012Mark takes string label returns nothing
 call Preload("ROUTE012 tick="+I2S(udg_Route012Tick)+" label="+label+" x="+R2S(GetUnitX(udg_Route012Unit))+" y="+R2S(GetUnitY(udg_Route012Unit))+" order="+I2S(GetUnitCurrentOrder(udg_Route012Unit)))
endfunction

function Route012Tick takes nothing returns nothing
 local integer phase=udg_Route012Tick/40
 local integer k=ModuloInteger(udg_Route012Tick,40)
 local real x=964.0
 local real y=1084.0
 if phase>=3 then
  call Route012Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_Route012Timer)
  return
 endif
 if k==0 then
  call IssueImmediateOrder(udg_Route012Unit,"stop")
  call SetUnitX(udg_Route012Unit,x)
  call SetUnitY(udg_Route012Unit,y)
  call Route012Mark("anchor")
  call IssuePointOrder(udg_Route012Unit,"move",1640.0,1368.0)
 elseif k==10 then
  call IssueImmediateOrder(udg_Route012Unit,"stop")
  call Route012Mark("prior-done")
 elseif k==12 then
  if phase==0 then
   set x=-112.0
   set y=328.0
  elseif phase==1 then
   set x=2248.0
   set y=328.0
  else
   set x=328.0
   set y=-16.0
  endif
  call Route012Mark("before-outside")
  call SetUnitX(udg_Route012Unit,x)
  call SetUnitY(udg_Route012Unit,y)
  call Route012Mark("after-outside")
 elseif k==13 then
  // Public setter reachability only. The separate interrupted capture
  // records setup0 after issuing Move from the negative-X source.
  call Route012Mark("outside-held")
 endif
 call Route012Mark("sample")
 set udg_Route012Tick=udg_Route012Tick+1
endfunction

function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_Route012Unit=CreateUnit(Player(0),'hfoo',964.0,1084.0,0.0)
 call Route012Mark("start")
 set udg_Route012Timer=CreateTimer()
 call TimerStart(udg_Route012Timer,0.05,true,function Route012Tick)
endfunction
