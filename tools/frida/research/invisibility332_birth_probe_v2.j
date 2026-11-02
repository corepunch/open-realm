globals
 unit I332U=null
 unit I332C=null
 timer I332Timer=null
 integer I332Tick=0
endglobals
function I332Bool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function I332Emit takes string s returns nothing
 call Preload("I332 tick="+I2S(I332Tick)+" scene=0 "+s)
endfunction
function I332Point takes string stage, real x, real y returns nothing
 call I332Emit("label=point stage="+stage+" x="+R2S(x)+" y="+R2S(y))
 call I332Emit("label=point-p0 stage="+stage+" visible="+I2S(I332Bool(IsVisibleToPlayer(x,y,Player(0))))+" fogged="+I2S(I332Bool(IsFoggedToPlayer(x,y,Player(0)))))
 call I332Emit("label=point-p1 stage="+stage+" visible="+I2S(I332Bool(IsVisibleToPlayer(x,y,Player(1))))+" fogged="+I2S(I332Bool(IsFoggedToPlayer(x,y,Player(1)))))
endfunction
function I332State takes string stage returns nothing
 call I332Emit("label=pose x="+R2S(GetUnitX(I332U))+" y="+R2S(GetUnitY(I332U)))
 call I332Emit("label=unit-p0 stage="+stage+" visible="+I2S(I332Bool(IsUnitVisible(I332U,Player(0))))+" invisible="+I2S(I332Bool(IsUnitInvisible(I332U,Player(0)))))
 call I332Emit("label=unit-p1 stage="+stage+" visible="+I2S(I332Bool(IsUnitVisible(I332U,Player(1))))+" invisible="+I2S(I332Bool(IsUnitInvisible(I332U,Player(1)))))
 call I332Point(stage,GetUnitX(I332U),GetUnitY(I332U))
endfunction
function I332Step takes nothing returns nothing
 if I332Tick==20 then
  call I332State("complete")
  call I332Emit("label=complete")
  call PreloadGenEnd("rs-invisibility332-v2.txt")
  call PauseTimer(I332Timer)
  return
 endif
 call I332State("periodic")
 set I332Tick=I332Tick+1
endfunction
function I332Birth takes nothing returns nothing
 call I332Emit("label=start")
 call PreloadGenEnd("rs-invisibility332-v2-start.txt")
 call PreloadGenClear()
 call PreloadGenStart()
 call I332Emit("label=start")
 call I332Point("before-source",1024,512)
 // Candidate cell observed in both v1 runs; placement remains unconstrained.
 call I332Point("before-placed-cell",1136,432)
 set I332U=CreateUnit(Player(0),'Hpal',1024,512,0)
 call I332State("after-source")
 call SetUnitAcquireRange(I332U,51)
 call I332Point("before-caster",1400,512)
 set I332C=CreateUnit(Player(1),'hfoo',1400,512,180)
 call I332State("after-caster")
 call SetUnitAcquireRange(I332C,51)
 call I332Emit("label=caster-pose x="+R2S(GetUnitX(I332C))+" y="+R2S(GetUnitY(I332C)))
 call I332Point("caster-cell",GetUnitX(I332C),GetUnitY(I332C))
 set I332Tick=1
 call TimerStart(I332Timer,0.05,true,function I332Step)
endfunction
function I332Init takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 set I332Timer=CreateTimer()
 call TimerStart(I332Timer,0.5,false,function I332Birth)
endfunction
