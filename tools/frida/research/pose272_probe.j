globals
 unit udg_P272Unit=null
 timer udg_P272Timer=null
 integer udg_P272Tick=0
 integer udg_P272Scene=0
 integer udg_P272Step=0
 integer array udg_P272Type
endglobals
function P272Mark takes string label returns nothing
 call Preload("P272 tick="+I2S(udg_P272Tick)+" scene="+I2S(udg_P272Scene)+" step="+I2S(udg_P272Step)+" label="+label+" x="+R2S(GetUnitX(udg_P272Unit))+" y="+R2S(GetUnitY(udg_P272Unit))+" order="+I2S(GetUnitCurrentOrder(udg_P272Unit)))
endfunction
function P272Tick takes nothing returns nothing
 set udg_P272Tick=udg_P272Tick+1
 if udg_P272Step==0 then
  set udg_P272Unit=CreateUnit(Player(0),udg_P272Type[udg_P272Scene],192.,672.,0.)
  call UnitRemoveAbility(udg_P272Unit,'Aatk')
  call IssuePointOrder(udg_P272Unit,"move",1856.,672.)
 endif
 if ModuloInteger(udg_P272Step,5)==0 then
  call P272Mark("travel")
 endif
 if udg_P272Step==90 then
  call RemoveUnit(udg_P272Unit)
  set udg_P272Unit=null
  set udg_P272Scene=udg_P272Scene+1
  set udg_P272Step=0
  if udg_P272Scene==3 then
   call Preload("P272 label=complete")
   call PreloadGenEnd("pose272.txt")
   call PauseTimer(udg_P272Timer)
  endif
 else
  set udg_P272Step=udg_P272Step+1
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1024.,672.)
 call PreloadGenClear()
 call PreloadGenStart()
 call Preload("P272 label=start")
 set udg_P272Type[0]='hfoo'
 set udg_P272Type[1]='hP72'
 set udg_P272Type[2]='hZ72'
 set udg_P272Timer=CreateTimer()
 call TimerStart(udg_P272Timer,.1,true,function P272Tick)
endfunction
