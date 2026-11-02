// Public timer observations only. The existing scene owns all units and AI.
function Refresh274Mark takes nothing returns nothing
 // Public axis writes keep one far member outside the Captain arrival range.
 if udg_PathProbeTick>=120 and udg_PathProbeTick<=280 then
  call SetUnitX(udg_PathProbeCrowd[12],-2936.0)
 endif
 if udg_PathProbeTick==1 then
  call Preload("R274 tick=1 label=start")
 endif
 call Preload("R274 tick="+I2S(udg_PathProbeTick)+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeCrowd[0]))+" x="+R2S(GetUnitX(udg_PathProbeCrowd[0]))+" y="+R2S(GetUnitY(udg_PathProbeCrowd[0])))
 if udg_PathProbeTick==400 then
  call Preload("R274 tick=400 label=complete")
 endif
endfunction
