globals
 timer entTimer=null
 unit entTree=null
 unit entMine=null
 unit entWorker=null
 integer entTick=0
 integer entScene=0
 integer entOverlays=0
endglobals
function EntBool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function EntCount takes nothing returns nothing
 set entOverlays=entOverlays+1
endfunction
function EntMark takes string label,boolean accepted returns nothing
 local group g=GetUnitsOfTypeIdAll('egol')
 set entOverlays=0
 call ForGroup(g,function EntCount)
 call DestroyGroup(g)
 call Preload("TEN tick="+I2S(entTick)+" scene="+I2S(entScene)+" label="+label+" accepted="+I2S(EntBool(accepted))+" head="+I2S(GetUnitCurrentOrder(entTree))+" x="+R2S(GetUnitX(entTree))+" y="+R2S(GetUnitY(entTree))+" mx="+R2S(GetUnitX(entMine))+" my="+R2S(GetUnitY(entMine))+" minevisible="+I2S(EntBool(not IsUnitHidden(entMine)))+" overlays="+I2S(entOverlays)+" wx="+R2S(GetUnitX(entWorker))+" wy="+R2S(GetUnitY(entWorker)))
endfunction
function EntRemove takes nothing returns nothing
 call RemoveUnit(GetEnumUnit())
endfunction
function EntTick takes nothing returns nothing
 local integer age=ModuloInteger(entTick,1000)
 local real x=704.0
 local real mx=1216.0
 local group g=null
 if entScene<3 then
  set x=704.0
  set mx=1536.0
 endif
 if age==0 then
  set entTree=CreateUnit(Player(0),'etol',x,1024.0,0.0)
  call SetUnitAcquireRange(entTree,0.0)
  if entScene>=3 then
   call EntMark("unroot-return",IssueImmediateOrder(entTree,"unroot"))
  endif
  call EntMark("spawn",true)
 elseif age==155 then
  set entMine=CreateUnit(Player(15),'ngol',mx,1024.0,0.0)
  call SetResourceAmount(entMine,5000)
  call EntMark("mine",true)
 elseif age==156 then
  if entScene==1 or entScene==2 or entScene==4 or entScene==5 then
   set entWorker=CreateUnit(Player(ModuloInteger(entScene,3)-1),'hpea',1024.0,960.0,0.0)
   call SetUnitAcquireRange(entWorker,0.0)
   if entScene>=3 then
    call SetUnitX(entWorker,704.0)
    call SetUnitY(entWorker,1024.0)
   else
    call SetUnitX(entWorker,1024.0)
    call SetUnitY(entWorker,960.0)
   endif
  endif
 elseif age==160 then
  call EntMark("before",true)
  call EntMark("entangle-return",IssueTargetOrder(entTree,"entangle",entMine))
 elseif age==998 then
  call EntMark("settled",true)
 elseif age==999 then
  call RemoveUnit(entTree)
  call RemoveUnit(entMine)
  call RemoveUnit(entWorker)
  set entWorker=null
  set entTree=null
  set entMine=null
  set g=GetUnitsOfTypeIdAll('egol')
  call ForGroup(g,function EntRemove)
  call DestroyGroup(g)
  set entScene=entScene+1
  if entScene==6 then
   call Preload("TEN tick="+I2S(entTick)+" scene=6 label=complete")
   call PreloadGenEnd("PRELOAD_FILE")
   call PauseTimer(entTimer)
   return
  endif
 elseif age>160 and ModuloInteger(age,20)==0 then
  call EntMark("sample",true)
 endif
 set entTick=entTick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 call Preload("TEN tick=0 scene=-1 label=start")
 call PreloadGenEnd("START_FILE")
 call PreloadGenClear()
 call PreloadGenStart()
 set entTimer=CreateTimer()
 call TimerStart(entTimer,0.02,true,function EntTick)
endfunction
