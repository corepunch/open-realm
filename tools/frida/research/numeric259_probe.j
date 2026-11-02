globals
 unit udg_N259Unit=null
 timer udg_N259Timer=null
endglobals
function N259Boundary takes nothing returns real
 local real nan=11.
 local real inf=12.
 local real Infinity=13.
 return @EXPRESSION@
endfunction
function N259Finish takes nothing returns nothing
 call Preload("N259 tick=1 label=complete value="+R2S(N259Boundary())+" order="+I2S(GetUnitCurrentOrder(udg_N259Unit))+" x="+R2S(GetUnitX(udg_N259Unit))+" y="+R2S(GetUnitY(udg_N259Unit)))
 call PreloadGenEnd("@OUTPUT@")
 call PauseTimer(udg_N259Timer)
endfunction
function PathProbeInit takes nothing returns nothing
 local real value=N259Boundary()
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 set udg_N259Unit=CreateUnit(Player(0),'hfoo',512,512,0)
 call SetUnitAcquireRange(udg_N259Unit,0)
 call IssuePointOrder(udg_N259Unit,"move",512+value,512)
 call Preload("N259 tick=0 label=start value="+R2S(value)+" order="+I2S(GetUnitCurrentOrder(udg_N259Unit)))
 set udg_N259Timer=CreateTimer()
 call TimerStart(udg_N259Timer,1.,false,function N259Finish)
endfunction
