globals
 unit udg_AC151Mover=null
 unit udg_AC151Source=null
 timer udg_AC151Timer=null
 integer udg_AC151Tick=0
endglobals
function AC151Mark takes string label returns nothing
 call Preload("AC151 tick="+I2S(udg_AC151Tick)+" label="+label+" x="+R2S(GetUnitX(udg_AC151Mover))+" y="+R2S(GetUnitY(udg_AC151Mover))+" order="+I2S(GetUnitCurrentOrder(udg_AC151Mover)))
endfunction
function AC151Tick takes nothing returns nothing
 set udg_AC151Tick=udg_AC151Tick+1
 if udg_AC151Tick==1 then
  call AC151Mark("start")
  call IssuePointOrder(udg_AC151Mover,"move",1800.0,300.0)
 elseif udg_AC151Tick==10 or udg_AC151Tick==11 or udg_AC151Tick==14 or udg_AC151Tick==15 or udg_AC151Tick==16 or udg_AC151Tick==20 or udg_AC151Tick==25 or udg_AC151Tick==26 then
  call AC151Mark("damage")
  call UnitDamageTarget(udg_AC151Source,udg_AC151Mover,0.0,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS)
 endif
 if udg_AC151Tick>=80 then
  call AC151Mark("complete")
  call PauseTimer(udg_AC151Timer)
  call PreloadGenEnd("@OUTPUT@")
 else
  call AC151Mark("sample")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_AC151Mover=CreateUnit(Player(0),'hfoo',200.0,300.0,0.0)
 set udg_AC151Source=CreateUnit(Player(1),'hfoo',1800.0,1800.0,0.0)
 call SetUnitAcquireRange(udg_AC151Mover,0.0)
 call SetUnitAcquireRange(udg_AC151Source,0.0)
 set udg_AC151Timer=CreateTimer()
 call TimerStart(udg_AC151Timer,0.1,true,function AC151Tick)
endfunction
