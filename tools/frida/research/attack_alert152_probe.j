globals
 unit udg_AA152Victim=null
 unit udg_AA152Helper=null
 unit udg_AA152Source=null
 timer udg_AA152Timer=null
 integer udg_AA152Tick=0
 integer udg_AA152Case=-1
endglobals
function AA152Mark takes string label returns nothing
 call Preload("AA152 tick="+I2S(udg_AA152Tick)+" label="+label+" case="+I2S(udg_AA152Case)+" victim="+I2S(GetHandleId(udg_AA152Victim))+" helper="+I2S(GetHandleId(udg_AA152Helper))+" order="+I2S(GetUnitCurrentOrder(udg_AA152Helper))+" x="+R2S(GetUnitX(udg_AA152Helper))+" y="+R2S(GetUnitY(udg_AA152Helper)))
endfunction
function AA152New takes nothing returns nothing
 if udg_AA152Victim!=null then
  call RemoveUnit(udg_AA152Victim)
  call RemoveUnit(udg_AA152Helper)
 endif
 set udg_AA152Case=udg_AA152Case+1
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_HELP_REQUEST,ModuloInteger(udg_AA152Case,2)==1)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_HELP_RESPONSE,ModuloInteger(udg_AA152Case/2,2)==1)
 // Opposite directions deliberately stay off throughout the matrix.
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_HELP_REQUEST,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_HELP_RESPONSE,false)
 set udg_AA152Victim=CreateUnit(Player(0),'hfoo',200.0,300.0,0.0)
 set udg_AA152Helper=CreateUnit(Player(1),'hfoo',300.0,300.0,0.0)
 call SetUnitAcquireRange(udg_AA152Victim,0.0)
 call SetUnitAcquireRange(udg_AA152Helper,0.0)
 call IssuePointOrder(udg_AA152Victim,"move",1800.0,300.0)
 call IssuePointOrder(udg_AA152Helper,"move",1800.0,400.0)
 call AA152Mark("new")
endfunction
function AA152Tick takes nothing returns nothing
 set udg_AA152Tick=udg_AA152Tick+1
 if udg_AA152Tick==1 then
  call AA152Mark("start")
  call AA152New()
 elseif ModuloInteger(udg_AA152Tick,50)==1 and udg_AA152Case<3 then
  call AA152New()
 elseif ModuloInteger(udg_AA152Tick,50)==10 then
  call AA152Mark("hit")
  call UnitDamageTarget(udg_AA152Source,udg_AA152Victim,1.0,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS)
 endif
 if udg_AA152Tick>=200 then
  call AA152Mark("complete")
  call PauseTimer(udg_AA152Timer)
  call PreloadGenEnd("@OUTPUT@")
 else
  call AA152Mark("sample")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 call SetPlayerAlliance(Player(0),Player(3),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(3),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(3),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(3),Player(1),ALLIANCE_PASSIVE,false)
 set udg_AA152Source=CreateUnit(Player(3),'hfoo',1800.0,1800.0,0.0)
 call SetUnitAcquireRange(udg_AA152Source,0.0)
 set udg_AA152Timer=CreateTimer()
 call TimerStart(udg_AA152Timer,0.1,true,function AA152Tick)
endfunction
