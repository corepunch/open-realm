globals
 timer udg_CT=null
 unit udg_CA=null
 unit udg_CB=null
 unit udg_CGate=null
 integer udg_CTick=0
 real udg_CX=1536.0
 real udg_CY=1056.0
 integer udg_CEvents=0
endglobals
function CMark takes string label returns nothing
 call Preload("T377C tick="+I2S(udg_CTick)+" label="+label+" events="+I2S(udg_CEvents)+" ax="+R2S(GetUnitX(udg_CA))+" ay="+R2S(GetUnitY(udg_CA))+" ao="+I2S(GetUnitCurrentOrder(udg_CA))+" bx="+R2S(GetUnitX(udg_CB))+" by="+R2S(GetUnitY(udg_CB))+" bo="+I2S(GetUnitCurrentOrder(udg_CB))+" gx="+R2S(udg_CX)+" gy="+R2S(udg_CY)+" gate="+I2S(GetUnitTypeId(udg_CGate))+" active="+I2S(GetUnitAbilityLevel(udg_CGate,'Awrp')))
endfunction
function CPoint takes nothing returns nothing
 if GetIssuedOrderId()==851986 then
  set udg_CX=GetOrderPointX()
  set udg_CY=GetOrderPointY()
 endif
 set udg_CEvents=udg_CEvents+1
 call CMark("issued-point-"+I2S(GetIssuedOrderId()))
endfunction
function CTick takes nothing returns nothing
 local boolean accepted=false
 if udg_CTick==0 then
  set udg_CA=CreateUnit(Player(0),'hfoo',272.0,304.0,0.0)
  set udg_CB=CreateUnit(Player(0),'hfoo',272.0,400.0,0.0)
  call SetUnitAcquireRange(udg_CA,0.0)
  call SetUnitAcquireRange(udg_CB,0.0)
  call SetUnitMoveSpeed(udg_CA,100.0)
  call SetUnitMoveSpeed(udg_CB,270.0)
  call SetUnitPropWindow(udg_CA,0.0)
  call SetUnitPropWindow(udg_CB,0.0)
  call ClearSelection()
  call SelectUnit(udg_CA,true)
  call SelectUnit(udg_CB,true)
  call SetCameraPosition(1536.0,1056.0)
  call CMark("selected")
 elseif udg_CTick==5 then
  call PreloadGenEnd("target377-cohort-window-input-start.txt")
 elseif udg_CTick==22 then
  call CMark("gate-create-before")
  set udg_CGate=CreateUnit(Player(0),'nwgt',udg_CX,udg_CY,0.0)
  call SetUnitX(udg_CGate,udg_CX)
  call SetUnitY(udg_CGate,udg_CY)
  call WaygateSetDestination(udg_CGate,512.0,1536.0)
  call WaygateActivate(udg_CGate,true)
 elseif udg_CTick==40 then
  call CMark("resume-before")
  call SetUnitPropWindow(udg_CA,60.0)
  call SetUnitPropWindow(udg_CB,60.0)
  call CMark("resume-after")
 elseif udg_CTick==60 then
  call CMark("gate-admit-before")
  set accepted=IssueTargetOrder(udg_CA,"move",udg_CGate)
  if accepted then
   call CMark("gate-admit-accepted")
  else
   call CMark("gate-admit-rejected")
  endif
 elseif udg_CTick==80 then
  call CMark("retarget-before")
  call WaygateSetDestination(udg_CGate,1024.0,1536.0)
  call CMark("retarget-after")
 elseif udg_CTick==270 then
  call CMark("terminal")
  call RemoveUnit(udg_CA)
  call RemoveUnit(udg_CB)
  call RemoveUnit(udg_CGate)
  set udg_CA=null
  set udg_CB=null
  set udg_CGate=null
 elseif udg_CTick==275 then
  call CMark("complete")
  call PreloadGenEnd("target377-cohort-window-input.txt")
  call PauseTimer(udg_CT)
 endif
 if udg_CTick<275 then
  call CMark("sample")
 endif
 set udg_CTick=udg_CTick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger t=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call SetCameraBounds(64.0,64.0,64.0,1984.0,1984.0,1984.0,1984.0,64.0)
 call SetRandomSeed(1377)
 call FogEnable(false)
 call FogMaskEnable(false)
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerAddAction(t,function CPoint)
 call Preload("T377C tick=0 label=start")
 set udg_CT=CreateTimer()
 call TimerStart(udg_CT,0.1,true,function CTick)
endfunction
