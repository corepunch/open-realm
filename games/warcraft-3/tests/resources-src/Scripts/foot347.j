globals
 timer udg_F347Timer=null
 integer udg_F347Tick=0
 unit array udg_F347Units
endglobals
function ModuloInteger takes integer a, integer b returns integer
 return a-(a/b)*b
endfunction
function F347Mark takes string label returns nothing
 local integer i=0
 call Preload("F347 tick="+I2S(udg_F347Tick)+" scene=0 label="+label)
 loop
  exitwhen i==13
  call Preload("F347 tick="+I2S(udg_F347Tick)+" scene=0 label=unit u="+I2S(i)+" type="+I2S(GetUnitTypeId(udg_F347Units[i]))+" order="+I2S(GetUnitCurrentOrder(udg_F347Units[i]))+" x="+R2SW(GetUnitX(udg_F347Units[i]),1,4)+" y="+R2SW(GetUnitY(udg_F347Units[i]),1,4))
  set i=i+1
 endloop
 call PreloadGenEnd("foot347.txt")
endfunction
function F347Tick takes nothing returns nothing
 local integer i=0
 set udg_F347Tick=udg_F347Tick+1
 if udg_F347Tick==1 then
  call F347Mark("start")
 elseif udg_F347Tick==10 then
  loop
   exitwhen i==13
   if i==0 then
    set udg_F347Units[i]=CreateUnit(Player(0),'hF3G',512,512,0)
   elseif i==12 then
    set udg_F347Units[i]=CreateUnit(Player(0),'hF3H',512,800,0)
   else
    set udg_F347Units[i]=CreateUnit(Player(0),'hfoo',512+I2R(ModuloInteger(i,4))*96,512+I2R(i/4)*96,0)
   endif
   call SetUnitMoveSpeed(udg_F347Units[i],173)
   set i=i+1
  endloop
  call F347Mark("created")
 elseif udg_F347Tick==12 then
  call StartCampaignAI(Player(0),"Scripts\\foot347.ai")
  call F347Mark("ai")
 elseif udg_F347Tick==30 then
  call F347Mark("before-shrink")
  call UnitAddAbility(udg_F347Units[12],'AF3S')
  call F347Mark("shrink")
 elseif udg_F347Tick==35 then
  call F347Mark("before-grow")
  call UnitAddAbility(udg_F347Units[12],'AF3G')
  call F347Mark("grow")
 elseif udg_F347Tick==40 then
  call RemoveUnit(udg_F347Units[0])
  call F347Mark("remove-largest")
 elseif udg_F347Tick==45 then
  call RemoveUnit(udg_F347Units[11])
  call F347Mark("remove-peer")
 elseif udg_F347Tick==80 then
  loop
   exitwhen i==13
   call IssueImmediateOrder(udg_F347Units[i],"stop")
   set i=i+1
  endloop
  call F347Mark("stop")
 elseif udg_F347Tick==100 then
  call F347Mark("complete")
  call PauseTimer(udg_F347Timer)
 elseif udg_F347Tick>12 then
  call F347Mark("sample")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,true)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_F347Timer=CreateTimer()
 call TimerStart(udg_F347Timer,0.10,true,function F347Tick)
endfunction
