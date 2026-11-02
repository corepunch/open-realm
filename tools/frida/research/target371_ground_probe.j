globals
 timer udg_T371Timer=null
 unit udg_T371Unit=null
 unit udg_T371Block=null
 integer udg_T371Tick=0
 integer udg_T371Scene=0
endglobals
function T371Mark takes string label, boolean ok returns nothing
 local integer accepted=0
 if ok then
  set accepted=1
 endif
 call Preload("T371 tick="+I2S(udg_T371Tick)+" scene="+I2S(udg_T371Scene)+" label="+label+" accepted="+I2S(accepted)+" x="+R2S(GetUnitX(udg_T371Unit))+" y="+R2S(GetUnitY(udg_T371Unit))+" order="+I2S(GetUnitCurrentOrder(udg_T371Unit)))
endfunction
function T371Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_T371Tick,100)
 local real gx=1536.0
 local real gy=1024.0
 local string order="attack"
 if age==0 then
  if udg_T371Scene<2 then
   set udg_T371Unit=CreateUnit(Player(0),'hfoo',512.0,1024.0,180.0)
  else
   set udg_T371Unit=CreateUnit(Player(0),'hmtm',512.0,1024.0,180.0)
  endif
  call T371Mark("created",true)
 elseif age==10 then
  set order="attackground"
  if udg_T371Scene==1 or udg_T371Scene==3 then
   set gx=544.0
  elseif udg_T371Scene==2 then
   set gx=1856.0
  endif
  call T371Mark("issue-before",true)
  call T371Mark("issue-after",IssuePointOrder(udg_T371Unit,order,gx,gy))
 elseif age==90 then
  call T371Mark("terminal",true)
  call RemoveUnit(udg_T371Unit)
  set udg_T371Unit=null
  if udg_T371Block!=null then
   call RemoveUnit(udg_T371Block)
   set udg_T371Block=null
  endif
 elseif age==99 then
  if udg_T371Scene==3 then
   call T371Mark("complete",true)
   call PreloadGenEnd("target371-ground.txt")
   call PauseTimer(udg_T371Timer)
  endif
  set udg_T371Scene=udg_T371Scene+1
 endif
 if age<90 then
  call T371Mark("sample",true)
 endif
 set udg_T371Tick=udg_T371Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1371)
 call FogEnable(false)
 call FogMaskEnable(false)
 call Preload("T371 tick=0 label=start")
 call PreloadGenEnd("target371-ground-start.txt")
 set udg_T371Timer=CreateTimer()
 call TimerStart(udg_T371Timer,0.1,true,function T371Tick)
endfunction
