globals
 timer udg_Point231Timer=null
 unit udg_Point231Unit=null
 integer udg_Point231Tick=0
endglobals
function Point231Record takes string label,real x,real y returns nothing
 local integer i=0
 local string values=""
 call Preload("P231 tick="+I2S(udg_Point231Tick)+" begin="+label)
 loop
  exitwhen i==8
  if IsTerrainPathable(x,y,ConvertPathingType(i)) then
   set values=values+"1"
  else
   set values=values+"0"
  endif
  set i=i+1
 endloop
 call Preload("P231 tick="+I2S(udg_Point231Tick)+" label="+label+" x="+R2S(x)+" y="+R2S(y)+" bits="+values)
endfunction
function Point231Tick takes nothing returns nothing
 set udg_Point231Tick=udg_Point231Tick+1
 if udg_Point231Tick==1 then
  call Point231Record("unit",512.0,512.0)
  call Point231Record("fraction",511.875,512.125)
  call Point231Record("outside",-0.125,512.0)
 elseif udg_Point231Tick==2 then
  call SetTerrainPathable(512.0,512.0,ConvertPathingType(1),false)
  call Point231Record("blocked",512.0,512.0)
 elseif udg_Point231Tick==3 then
  call SetTerrainPathable(512.0,512.0,ConvertPathingType(1),true)
  call Point231Record("cleared",512.0,512.0)
 elseif udg_Point231Tick==4 then
  call RemoveUnit(udg_Point231Unit)
  set udg_Point231Unit=null
  call Point231Record("removed",512.0,512.0)
 elseif udg_Point231Tick==5 then
  call Preload("P231 tick=5 label=complete")
  call PreloadGenEnd("rs-point231.txt")
  call PauseTimer(udg_Point231Timer)
 endif
endfunction
function main takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(512.0,512.0)
 set udg_Point231Unit=CreateUnit(Player(0),'hfoo',512.0,512.0,0.0)
 call Preload("P231 tick=0 label=start")
 set udg_Point231Timer=CreateTimer()
 call TimerStart(udg_Point231Timer,0.1,true,function Point231Tick)
endfunction
