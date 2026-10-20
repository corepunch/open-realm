globals
 unit udg_F13Mover=null
 unit udg_F13Target=null
 unit udg_F13Gate=null
 timer udg_F13Timer=null
 integer udg_F13Tick=0
 integer udg_F13Lane=0
endglobals

function F13Mark takes string label returns nothing
 call Preload("F13 tick="+I2S(udg_F13Tick)+" label="+label+" lane="+I2S(udg_F13Lane)+" mover="+R2S(GetUnitX(udg_F13Mover))+","+R2S(GetUnitY(udg_F13Mover))+","+I2S(GetUnitCurrentOrder(udg_F13Mover)))
endfunction

function F13Lane takes integer lane returns nothing
 if udg_F13Mover!=null then
  call RemoveUnit(udg_F13Mover)
  call RemoveUnit(udg_F13Target)
 endif
 set udg_F13Lane=lane
 set udg_F13Mover=CreateUnit(Player(0),'hF00',272.,304.,0.)
 set udg_F13Target=CreateUnit(Player(0),'hF00',1744.,1776.,0.)
 call SetUnitAcquireRange(udg_F13Mover,0.)
 call SetUnitAcquireRange(udg_F13Target,0.)
 if lane==1 then
  call UnitAddAbility(udg_F13Target,'Adro')
 elseif lane==2 then
  call UnitAddAbility(udg_F13Target,'Amed')
 elseif lane==3 then
  call UnitAddAbility(udg_F13Target,'Atdp')
 endif
 call F13Mark("issue")
 call IssueTargetOrder(udg_F13Mover,"move",udg_F13Target)
endfunction

function F13Tick takes nothing returns nothing
 set udg_F13Tick=udg_F13Tick+1
 if udg_F13Tick==1 then
  call F13Lane(0)
 elseif udg_F13Tick==101 then
  call F13Lane(1)
 elseif udg_F13Tick==201 then
  call F13Lane(2)
 elseif udg_F13Tick==301 then
  call F13Lane(3)
 elseif udg_F13Tick==401 then
  call F13Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_F13Timer)
  return
 endif
 call F13Mark("sample")
endfunction

function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1027)
 call FogEnable(false)
 call FogMaskEnable(false)
 set udg_F13Gate=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',512.,768.,0.)
 call WaygateSetDestination(udg_F13Gate,1728.,1760.)
 call WaygateActivate(udg_F13Gate,true)
 call F13Mark("start")
 set udg_F13Timer=CreateTimer()
 call TimerStart(udg_F13Timer,0.1,true,function F13Tick)
endfunction
