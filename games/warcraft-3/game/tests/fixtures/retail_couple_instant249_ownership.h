/* Retail249; only Preload replaced by observation adapter. */
#ifndef RETAIL_COUPLE_INSTANT249_OWNERSHIP_H
#define RETAIL_COUPLE_INSTANT249_OWNERSHIP_H
static char const couple_native_probe249[] =
    "globals\n"
    " timer cpTimer=null\n"
    " integer cpTick=0\n"
    " integer cpScene=0\n"
    " unit array cpHippo\n"
    " unit array cpArcher\n"
    " unit array cpBuff\n"
    " unit array cpReceiver\n"
    " item array cpPotion\n"
    " boolean cpNested=false\n"
    "endglobals\n"
    "function CpBool takes boolean b returns integer\n"
    " if b then\n"
    " return 1\n"
    " endif\n"
    " return 0\n"
    "endfunction\n"
    "function CpLog takes string s returns nothing\n"
    " if StringLength(s)>160 then\n"
    " call CouplePreparationObserve(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=OVERLONG\")\n"
    " else\n"
    " call CouplePreparationObserve(s)\n"
    " endif\n"
    "endfunction\n"
    "function CpMark takes string label returns nothing\n"
    " local unit h=cpHippo[cpScene]\n"
    " local item p=cpPotion[cpScene]\n"
    " call CpLog(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=\"+label+\" head=\"+I2S(GetUnitCurrentOrder(h))+\" type=\"+I2S(GetUnitTypeId(h))+\" life=\"+R2S(GetWidgetLife(h)))\n"
    " call CpLog(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=resource mana=\"+R2S(GetUnitState(h,UNIT_STATE_MANA))+\" max=\"+R2S(GetUnitState(h,UNIT_STATE_MAX_MANA))+\" charges=\"+I2S(GetItemCharges(p)))\n"
    " call CpLog(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=item type=\"+I2S(GetItemTypeId(p))+\" held=\"+I2S(CpBool(UnitHasItem(h,p)))+\" transferred=\"+I2S(CpBool(UnitHasItem(cpReceiver[cpScene],p)))+\" outer=\"+I2S(GetUnitAbilityLevel(h,'Xinf')))\n"
    "endfunction\n"
    "function CpEvent takes nothing returns nothing\n"
    " local integer oid=GetIssuedOrderId()\n"
    " local boolean accepted=false\n"
    " call CpLog(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=issued oid=\"+I2S(oid)+\" actor=\"+I2S(GetUnitUserData(GetTriggerUnit()))+\" target=\"+I2S(GetUnitUserData(GetOrderTargetUnit())))\n"
    " if GetTriggerUnit()==cpHippo[cpScene] and oid>=852008 and oid<=852013 and not cpNested then\n"
    "  set cpNested=true\n"
    "  call CpMark(\"callback-before\")\n"
    "  if cpScene==1 then\n"
    "   set accepted=IssuePointOrder(cpHippo[cpScene],\"move\",1792.0,1024.0+I2R(cpScene)*512.0)\n"
    "  elseif cpScene==2 then\n"
    "   call RemoveUnit(cpHippo[cpScene])\n"
    "  elseif cpScene==3 then\n"
    "   call RemoveItem(cpPotion[cpScene])\n"
    "  elseif cpScene==4 then\n"
    "   set accepted=UnitAddItem(cpReceiver[cpScene],cpPotion[cpScene])\n"
    "  elseif cpScene==5 then\n"
    "   set accepted=UnitRemoveAbility(cpHippo[cpScene],'Xinf')\n"
    "  endif\n"
    "  call CpLog(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=callback-return accepted=\"+I2S(CpBool(accepted)))\n"
    "  call CpMark(\"callback-after\")\n"
    " endif\n"
    "endfunction\n"
    "function CpSpell takes nothing returns nothing\n"
    " call CpLog(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=spell event=\"+I2S(GetHandleId(GetTriggerEventId()))+\" actor=\"+I2S(GetUnitUserData(GetTriggerUnit()))+\" ability=\"+I2S(GetSpellAbilityId()))\n"
    " call CpLog(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=context target=\"+I2S(GetUnitUserData(GetSpellTargetUnit()))+\" x=\"+R2S(GetSpellTargetX())+\" y=\"+R2S(GetSpellTargetY()))\n"
    " call CpMark(\"spell-state\")\n"
    "endfunction\n"
    "function CpUsed takes nothing returns nothing\n"
    " call CpLog(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=used actor=\"+I2S(GetUnitUserData(GetTriggerUnit()))+\" item=\"+I2S(GetItemTypeId(GetManipulatedItem()))+\" charges=\"+I2S(GetItemCharges(GetManipulatedItem())))\n"
    " call CpMark(\"used-state\")\n"
    "endfunction\n"
    "function CpTick takes nothing returns nothing\n"
    " local boolean accepted=false\n"
    " local integer age=0\n"
    " set cpTick=cpTick+1\n"
    " set cpScene=(cpTick-1)/400\n"
    " set age=ModuloInteger(cpTick-1,400)+1\n"
    " if cpTick==1 then\n"
    "  call StartCampaignAI(Player(1),\"Scripts\\\\target243.ai\")\n"
    " endif\n"
    " if age==1 then\n"
    "  set cpNested=false\n"
    "  call CpMark(\"scene-start\")\n"
    " elseif age==100 then\n"
    "  call CpMark(\"outer-before\")\n"
    "  set accepted=IssueTargetOrder(cpHippo[cpScene],\"innerfire\",cpBuff[cpScene])\n"
    "  call CpLog(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=outer-return accepted=\"+I2S(CpBool(accepted)))\n"
    "  call CpMark(\"outer-after\")\n"
    " elseif age==399 then\n"
    "  call CpMark(\"settled\")\n"
    " elseif ModuloInteger(age,25)==0 then\n"
    "  call CpMark(\"sample\")\n"
    " endif\n"
    " if cpTick==2400 then\n"
    "  call CpMark(\"complete\")\n"
    "  call PreloadGenEnd(\"target-coupling-public249-ownership.txt\")\n"
    "  call PauseTimer(cpTimer)\n"
    " endif\n"
    "endfunction\n"
    "function PathProbeInit takes nothing returns nothing\n"
    " local integer i=0\n"
    " local real y=0.0\n"
    " local trigger ti=CreateTrigger()\n"
    " local trigger tt=CreateTrigger()\n"
    " local trigger tp=CreateTrigger()\n"
    " local trigger ts=CreateTrigger()\n"
    " local trigger tu=CreateTrigger()\n"
    " call PreloadGenClear()\n"
    " call PreloadGenStart()\n"
    " loop\n"
    "  exitwhen i==6\n"
    "  set y=1024.0+I2R(i)*512.0\n"
    "  set cpArcher[i]=CreateUnit(Player(1),'earc',1280.0,y,0.0)\n"
    "  set cpHippo[i]=CreateUnit(Player(1),'ehip',1536.0,y,0.0)\n"
    "  set cpBuff[i]=CreateUnit(Player(1),'hfoo',1700.0,y,0.0)\n"
    "  set cpReceiver[i]=CreateUnit(Player(1),'hfoo',1600.0,y+128.0,0.0)\n"
    "  call SetUnitUserData(cpHippo[i],i*4+1)\n"
    "  call SetUnitUserData(cpArcher[i],i*4+2)\n"
    "  call SetUnitUserData(cpBuff[i],i*4+3)\n"
    "  call SetUnitUserData(cpReceiver[i],i*4+4)\n"
    "  call SetUnitAcquireRange(cpArcher[i],0.0)\n"
    "  call SetUnitAcquireRange(cpHippo[i],0.0)\n"
    "  call SetUnitAcquireRange(cpBuff[i],0.0)\n"
    "  call SetUnitAcquireRange(cpReceiver[i],0.0)\n"
    "  call UnitAddAbility(cpHippo[i],'AInv')\n"
    "  call UnitAddAbility(cpHippo[i],'Xinf')\n"
    "  call UnitAddAbility(cpReceiver[i],'AInv')\n"
    "  call SetUnitState(cpHippo[i],UNIT_STATE_MANA,0.0)\n"
    "  set cpPotion[i]=UnitAddItemById(cpHippo[i],'pman')\n"
    "  set i=i+1\n"
    " endloop\n"
    " call SetPlayerTechResearched(Player(1),'Reht',1)\n"
    " call SetPlayerTechResearched(Player(1),'Rhpt',2)\n"
    " call TriggerRegisterPlayerUnitEvent(ti,Player(1),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
    " call TriggerRegisterPlayerUnitEvent(tt,Player(1),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)\n"
    " call TriggerRegisterPlayerUnitEvent(tp,Player(1),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
    " call TriggerAddAction(ti,function CpEvent)\n"
    " call TriggerAddAction(tt,function CpEvent)\n"
    " call TriggerAddAction(tp,function CpEvent)\n"
    " call TriggerRegisterPlayerUnitEvent(ts,Player(1),EVENT_PLAYER_UNIT_SPELL_CHANNEL,null)\n"
    " call TriggerRegisterPlayerUnitEvent(ts,Player(1),EVENT_PLAYER_UNIT_SPELL_CAST,null)\n"
    " call TriggerRegisterPlayerUnitEvent(ts,Player(1),EVENT_PLAYER_UNIT_SPELL_EFFECT,null)\n"
    " call TriggerRegisterPlayerUnitEvent(ts,Player(1),EVENT_PLAYER_UNIT_SPELL_ENDCAST,null)\n"
    " call TriggerRegisterPlayerUnitEvent(ts,Player(1),EVENT_PLAYER_UNIT_SPELL_FINISH,null)\n"
    " call TriggerAddAction(ts,function CpSpell)\n"
    " call TriggerRegisterPlayerUnitEvent(tu,Player(1),EVENT_PLAYER_UNIT_USE_ITEM,null)\n"
    " call TriggerAddAction(tu,function CpUsed)\n"
    " call FogEnable(false)\n"
    " call FogMaskEnable(false)\n"
    " call CpMark(\"start\")\n"
    " call PreloadGenEnd(\"target-coupling-public249-ownership-start.txt\")\n"
    " call PreloadGenClear()\n"
    " call PreloadGenStart()\n"
    " set cpTimer=CreateTimer()\n"
    " call TimerStart(cpTimer,0.02,true,function CpTick)\n"
    "endfunction\n"
;
static char const *const couple_native_prefix249[] = {
    "CPF tick=0 scene=0 label=start head=0 type=1701341552 life=525.000",
    "CPF tick=0 scene=0 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=0 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
};
static char const *const couple_native_public249[] = {
    "CPF tick=0 scene=0 label=ai-thread-enter",
    "CPF tick=1 scene=0 label=scene-start head=0 type=1701341552 life=525.000",
    "CPF tick=1 scene=0 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=25 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=25 scene=0 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=25 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=50 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=50 scene=0 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=50 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=75 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=75 scene=0 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=75 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=outer-before head=0 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=100 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=issued oid=852066 actor=1 target=3",
    "CPF tick=100 scene=0 label=issued oid=852008 actor=1 target=0",
    "CPF tick=100 scene=0 label=callback-before head=852066 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=100 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=callback-return accepted=0",
    "CPF tick=100 scene=0 label=callback-after head=852066 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=100 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=spell event=272 actor=1 ability=1095331121",
    "CPF tick=100 scene=0 label=context target=1 x=0.000 y=0.000",
    "CPF tick=100 scene=0 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=100 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=spell event=273 actor=1 ability=1095331121",
    "CPF tick=100 scene=0 label=context target=1 x=0.000 y=0.000",
    "CPF tick=100 scene=0 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=100 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=spell event=274 actor=1 ability=1095331121",
    "CPF tick=100 scene=0 label=context target=1 x=0.000 y=0.000",
    "CPF tick=100 scene=0 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=100 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=spell event=275 actor=1 ability=1095331121",
    "CPF tick=100 scene=0 label=context target=1 x=0.000 y=0.000",
    "CPF tick=100 scene=0 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=150.000 max=200.000 charges=1",
    "CPF tick=100 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=spell event=276 actor=1 ability=1095331121",
    "CPF tick=100 scene=0 label=context target=1 x=0.000 y=0.000",
    "CPF tick=100 scene=0 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=150.000 max=200.000 charges=1",
    "CPF tick=100 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=used actor=1 item=1886216558 charges=0",
    "CPF tick=100 scene=0 label=used-state head=852066 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=100 scene=0 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=spell event=272 actor=1 ability=1483304550",
    "CPF tick=100 scene=0 label=context target=3 x=1712.328 y=1008.217",
    "CPF tick=100 scene=0 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=100 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=spell event=273 actor=1 ability=1483304550",
    "CPF tick=100 scene=0 label=context target=3 x=1712.328 y=1008.217",
    "CPF tick=100 scene=0 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=100 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=100 scene=0 label=outer-return accepted=1",
    "CPF tick=100 scene=0 label=outer-after head=852066 type=1701341552 life=525.000",
    "CPF tick=100 scene=0 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=100 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=115 scene=0 label=spell event=274 actor=1 ability=1483304550",
    "CPF tick=115 scene=0 label=context target=3 x=1712.328 y=1008.217",
    "CPF tick=115 scene=0 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=115 scene=0 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=115 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=125 scene=0 label=sample head=852066 type=1701341552 life=525.000",
    "CPF tick=125 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=125 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=140 scene=0 label=spell event=275 actor=1 ability=1483304550",
    "CPF tick=140 scene=0 label=context target=0 x=0.000 y=0.000",
    "CPF tick=140 scene=0 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=140 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=140 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=140 scene=0 label=spell event=276 actor=1 ability=1483304550",
    "CPF tick=140 scene=0 label=context target=0 x=0.000 y=0.000",
    "CPF tick=140 scene=0 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=140 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=140 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=150 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=150 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=150 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=175 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=175 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=175 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=200 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=200 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=200 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=225 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=225 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=225 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=250 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=250 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=250 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=275 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=275 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=275 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=300 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=300 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=300 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=325 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=325 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=325 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=350 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=350 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=350 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=375 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=375 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=375 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=399 scene=0 label=settled head=0 type=1701341552 life=525.000",
    "CPF tick=399 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=399 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=400 scene=0 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=400 scene=0 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=400 scene=0 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=401 scene=1 label=scene-start head=0 type=1701341552 life=525.000",
    "CPF tick=401 scene=1 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=401 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=425 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=425 scene=1 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=425 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=450 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=450 scene=1 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=450 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=475 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=475 scene=1 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=475 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=500 scene=1 label=outer-before head=0 type=1701341552 life=525.000",
    "CPF tick=500 scene=1 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=500 scene=1 label=issued oid=852066 actor=5 target=7",
    "CPF tick=500 scene=1 label=issued oid=852008 actor=5 target=0",
    "CPF tick=500 scene=1 label=callback-before head=852066 type=1701341552 life=525.000",
    "CPF tick=500 scene=1 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=500 scene=1 label=issued oid=851986 actor=5 target=0",
    "CPF tick=500 scene=1 label=callback-return accepted=1",
    "CPF tick=500 scene=1 label=callback-after head=851986 type=1701341552 life=525.000",
    "CPF tick=500 scene=1 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=500 scene=1 label=spell event=272 actor=5 ability=1095331121",
    "CPF tick=500 scene=1 label=context target=5 x=0.000 y=0.000",
    "CPF tick=500 scene=1 label=spell-state head=851986 type=1701341552 life=525.000",
    "CPF tick=500 scene=1 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=500 scene=1 label=spell event=273 actor=5 ability=1095331121",
    "CPF tick=500 scene=1 label=context target=5 x=0.000 y=0.000",
    "CPF tick=500 scene=1 label=spell-state head=851986 type=1701341552 life=525.000",
    "CPF tick=500 scene=1 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=500 scene=1 label=spell event=274 actor=5 ability=1095331121",
    "CPF tick=500 scene=1 label=context target=5 x=0.000 y=0.000",
    "CPF tick=500 scene=1 label=spell-state head=851986 type=1701341552 life=525.000",
    "CPF tick=500 scene=1 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=500 scene=1 label=spell event=275 actor=5 ability=1095331121",
    "CPF tick=500 scene=1 label=context target=5 x=0.000 y=0.000",
    "CPF tick=500 scene=1 label=spell-state head=851986 type=1701341552 life=525.000",
    "CPF tick=500 scene=1 label=resource mana=150.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=500 scene=1 label=spell event=276 actor=5 ability=1095331121",
    "CPF tick=500 scene=1 label=context target=5 x=0.000 y=0.000",
    "CPF tick=500 scene=1 label=spell-state head=851986 type=1701341552 life=525.000",
    "CPF tick=500 scene=1 label=resource mana=150.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=500 scene=1 label=used actor=5 item=1886216558 charges=0",
    "CPF tick=500 scene=1 label=used-state head=851986 type=1701341552 life=525.000",
    "CPF tick=500 scene=1 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=500 scene=1 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=500 scene=1 label=outer-return accepted=1",
    "CPF tick=500 scene=1 label=outer-after head=851986 type=1701341552 life=525.000",
    "CPF tick=500 scene=1 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=500 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=502 scene=1 label=spell event=272 actor=5 ability=1483304550",
    "CPF tick=502 scene=1 label=context target=7 x=1757.965 y=1481.328",
    "CPF tick=502 scene=1 label=spell-state head=851986 type=1701341552 life=525.000",
    "CPF tick=502 scene=1 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=502 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=502 scene=1 label=spell event=273 actor=5 ability=1483304550",
    "CPF tick=502 scene=1 label=context target=7 x=1757.965 y=1481.328",
    "CPF tick=502 scene=1 label=spell-state head=851986 type=1701341552 life=525.000",
    "CPF tick=502 scene=1 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=502 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=517 scene=1 label=spell event=274 actor=5 ability=1483304550",
    "CPF tick=517 scene=1 label=context target=7 x=1757.965 y=1481.328",
    "CPF tick=517 scene=1 label=spell-state head=851986 type=1701341552 life=525.000",
    "CPF tick=517 scene=1 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=517 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=525 scene=1 label=sample head=851986 type=1701341552 life=525.000",
    "CPF tick=525 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=525 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=543 scene=1 label=spell event=275 actor=5 ability=1483304550",
    "CPF tick=543 scene=1 label=context target=0 x=0.000 y=0.000",
    "CPF tick=543 scene=1 label=spell-state head=851986 type=1701341552 life=525.000",
    "CPF tick=543 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=543 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=543 scene=1 label=spell event=276 actor=5 ability=1483304550",
    "CPF tick=543 scene=1 label=context target=0 x=0.000 y=0.000",
    "CPF tick=543 scene=1 label=spell-state head=851986 type=1701341552 life=525.000",
    "CPF tick=543 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=543 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=550 scene=1 label=sample head=851986 type=1701341552 life=525.000",
    "CPF tick=550 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=550 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=575 scene=1 label=sample head=851986 type=1701341552 life=525.000",
    "CPF tick=575 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=575 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=600 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=600 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=600 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=625 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=625 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=625 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=650 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=650 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=650 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=675 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=675 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=675 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=700 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=700 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=700 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=725 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=725 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=725 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=750 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=750 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=750 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=775 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=775 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=775 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=799 scene=1 label=settled head=0 type=1701341552 life=525.000",
    "CPF tick=799 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=799 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=800 scene=1 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=800 scene=1 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=800 scene=1 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=801 scene=2 label=scene-start head=0 type=1701341552 life=525.000",
    "CPF tick=801 scene=2 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=801 scene=2 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=825 scene=2 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=825 scene=2 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=825 scene=2 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=850 scene=2 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=850 scene=2 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=850 scene=2 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=875 scene=2 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=875 scene=2 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=875 scene=2 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=900 scene=2 label=outer-before head=0 type=1701341552 life=525.000",
    "CPF tick=900 scene=2 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=900 scene=2 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=900 scene=2 label=issued oid=852066 actor=9 target=11",
    "CPF tick=900 scene=2 label=issued oid=852008 actor=9 target=0",
    "CPF tick=900 scene=2 label=callback-before head=852066 type=1701341552 life=525.000",
    "CPF tick=900 scene=2 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=900 scene=2 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=900 scene=2 label=callback-return accepted=0",
    "CPF tick=900 scene=2 label=callback-after head=0 type=1701341552 life=525.000",
    "CPF tick=900 scene=2 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=900 scene=2 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=900 scene=2 label=outer-return accepted=1",
    "CPF tick=900 scene=2 label=outer-after head=852008 type=1701341552 life=525.000",
    "CPF tick=900 scene=2 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=900 scene=2 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=925 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=925 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=925 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=950 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=950 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=950 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=975 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=975 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=975 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=1000 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=1000 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=1000 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=1025 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=1025 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=1025 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=1050 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=1050 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=1050 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=1075 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=1075 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=1075 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=1100 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=1100 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=1100 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=1125 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=1125 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=1125 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=1150 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=1150 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=1150 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=1175 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=1175 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=1175 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=1199 scene=2 label=settled head=0 type=0 life=0.000",
    "CPF tick=1199 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=1199 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=1200 scene=2 label=sample head=0 type=0 life=0.000",
    "CPF tick=1200 scene=2 label=resource mana=0.000 max=0.000 charges=0",
    "CPF tick=1200 scene=2 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=1201 scene=3 label=scene-start head=0 type=1701341552 life=525.000",
    "CPF tick=1201 scene=3 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1201 scene=3 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1225 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1225 scene=3 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1225 scene=3 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1250 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1250 scene=3 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1250 scene=3 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1275 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1275 scene=3 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1275 scene=3 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1300 scene=3 label=outer-before head=0 type=1701341552 life=525.000",
    "CPF tick=1300 scene=3 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1300 scene=3 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1300 scene=3 label=issued oid=852066 actor=13 target=15",
    "CPF tick=1300 scene=3 label=issued oid=852008 actor=13 target=0",
    "CPF tick=1300 scene=3 label=callback-before head=852066 type=1701341552 life=525.000",
    "CPF tick=1300 scene=3 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1300 scene=3 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1300 scene=3 label=callback-return accepted=0",
    "CPF tick=1300 scene=3 label=callback-after head=852066 type=1701341552 life=525.000",
    "CPF tick=1300 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1300 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1300 scene=3 label=outer-return accepted=1",
    "CPF tick=1300 scene=3 label=outer-after head=0 type=1701341552 life=525.000",
    "CPF tick=1300 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1300 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1325 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1325 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1325 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1350 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1350 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1350 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1375 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1375 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1375 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1400 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1400 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1400 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1425 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1425 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1425 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1450 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1450 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1450 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1475 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1475 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1475 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1500 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1500 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1500 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1525 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1525 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1525 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1550 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1550 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1550 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1575 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1575 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1575 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1599 scene=3 label=settled head=0 type=1701341552 life=525.000",
    "CPF tick=1599 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1599 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1600 scene=3 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1600 scene=3 label=resource mana=0.000 max=200.000 charges=0",
    "CPF tick=1600 scene=3 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1601 scene=4 label=scene-start head=0 type=1701341552 life=525.000",
    "CPF tick=1601 scene=4 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1601 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1625 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1625 scene=4 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1625 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1650 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1650 scene=4 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1650 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1675 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1675 scene=4 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1675 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1700 scene=4 label=outer-before head=0 type=1701341552 life=525.000",
    "CPF tick=1700 scene=4 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1700 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1700 scene=4 label=issued oid=852066 actor=17 target=19",
    "CPF tick=1700 scene=4 label=issued oid=852008 actor=17 target=0",
    "CPF tick=1700 scene=4 label=callback-before head=852066 type=1701341552 life=525.000",
    "CPF tick=1700 scene=4 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1700 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1700 scene=4 label=callback-return accepted=0",
    "CPF tick=1700 scene=4 label=callback-after head=852066 type=1701341552 life=525.000",
    "CPF tick=1700 scene=4 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1700 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1700 scene=4 label=spell event=272 actor=17 ability=1095331121",
    "CPF tick=1700 scene=4 label=context target=17 x=0.000 y=0.000",
    "CPF tick=1700 scene=4 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=1700 scene=4 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1700 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1700 scene=4 label=spell event=273 actor=17 ability=1095331121",
    "CPF tick=1700 scene=4 label=context target=17 x=0.000 y=0.000",
    "CPF tick=1700 scene=4 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=1700 scene=4 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1700 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1700 scene=4 label=spell event=274 actor=17 ability=1095331121",
    "CPF tick=1700 scene=4 label=context target=17 x=0.000 y=0.000",
    "CPF tick=1700 scene=4 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=1700 scene=4 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=1700 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1700 scene=4 label=spell event=275 actor=17 ability=1095331121",
    "CPF tick=1700 scene=4 label=context target=17 x=0.000 y=0.000",
    "CPF tick=1700 scene=4 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=1700 scene=4 label=resource mana=150.000 max=200.000 charges=1",
    "CPF tick=1700 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1700 scene=4 label=spell event=276 actor=17 ability=1095331121",
    "CPF tick=1700 scene=4 label=context target=17 x=0.000 y=0.000",
    "CPF tick=1700 scene=4 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=1700 scene=4 label=resource mana=150.000 max=200.000 charges=1",
    "CPF tick=1700 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1700 scene=4 label=used actor=17 item=1886216558 charges=0",
    "CPF tick=1700 scene=4 label=used-state head=852066 type=1701341552 life=525.000",
    "CPF tick=1700 scene=4 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=1700 scene=4 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=1700 scene=4 label=outer-return accepted=1",
    "CPF tick=1700 scene=4 label=outer-after head=852066 type=1701341552 life=525.000",
    "CPF tick=1700 scene=4 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=1700 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1704 scene=4 label=spell event=272 actor=17 ability=1483304550",
    "CPF tick=1704 scene=4 label=context target=19 x=1915.656 y=1869.102",
    "CPF tick=1704 scene=4 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=1704 scene=4 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=1704 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1704 scene=4 label=spell event=273 actor=17 ability=1483304550",
    "CPF tick=1704 scene=4 label=context target=19 x=1915.656 y=1869.102",
    "CPF tick=1704 scene=4 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=1704 scene=4 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=1704 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1719 scene=4 label=spell event=274 actor=17 ability=1483304550",
    "CPF tick=1719 scene=4 label=context target=19 x=1915.656 y=1869.102",
    "CPF tick=1719 scene=4 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=1719 scene=4 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=1719 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1725 scene=4 label=sample head=852066 type=1701341552 life=525.000",
    "CPF tick=1725 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1725 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1744 scene=4 label=spell event=275 actor=17 ability=1483304550",
    "CPF tick=1744 scene=4 label=context target=0 x=0.000 y=0.000",
    "CPF tick=1744 scene=4 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=1744 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1744 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1744 scene=4 label=spell event=276 actor=17 ability=1483304550",
    "CPF tick=1744 scene=4 label=context target=0 x=0.000 y=0.000",
    "CPF tick=1744 scene=4 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=1744 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1744 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1750 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1750 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1750 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1775 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1775 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1775 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1800 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1800 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1800 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1825 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1825 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1825 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1850 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1850 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1850 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1875 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1875 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1875 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1900 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1900 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1900 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1925 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1925 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1925 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1950 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1950 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1950 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1975 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=1975 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1975 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=1999 scene=4 label=settled head=0 type=1701341552 life=525.000",
    "CPF tick=1999 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=1999 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=2000 scene=4 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2000 scene=4 label=resource mana=100.000 max=200.000 charges=0",
    "CPF tick=2000 scene=4 label=item type=0 held=0 transferred=0 outer=1",
    "CPF tick=2001 scene=5 label=scene-start head=0 type=1701341552 life=525.000",
    "CPF tick=2001 scene=5 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=2001 scene=5 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=2025 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2025 scene=5 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=2025 scene=5 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=2050 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2050 scene=5 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=2050 scene=5 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=2075 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2075 scene=5 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=2075 scene=5 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=2100 scene=5 label=outer-before head=0 type=1701341552 life=525.000",
    "CPF tick=2100 scene=5 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=2100 scene=5 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=2100 scene=5 label=issued oid=852066 actor=21 target=23",
    "CPF tick=2100 scene=5 label=issued oid=852008 actor=21 target=0",
    "CPF tick=2100 scene=5 label=callback-before head=852066 type=1701341552 life=525.000",
    "CPF tick=2100 scene=5 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=2100 scene=5 label=item type=1886216558 held=1 transferred=0 outer=1",
    "CPF tick=2100 scene=5 label=callback-return accepted=1",
    "CPF tick=2100 scene=5 label=callback-after head=852066 type=1701341552 life=525.000",
    "CPF tick=2100 scene=5 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=2100 scene=5 label=item type=1886216558 held=1 transferred=0 outer=0",
    "CPF tick=2100 scene=5 label=spell event=272 actor=21 ability=1095331121",
    "CPF tick=2100 scene=5 label=context target=21 x=0.000 y=0.000",
    "CPF tick=2100 scene=5 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=2100 scene=5 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=2100 scene=5 label=item type=1886216558 held=1 transferred=0 outer=0",
    "CPF tick=2100 scene=5 label=spell event=273 actor=21 ability=1095331121",
    "CPF tick=2100 scene=5 label=context target=21 x=0.000 y=0.000",
    "CPF tick=2100 scene=5 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=2100 scene=5 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=2100 scene=5 label=item type=1886216558 held=1 transferred=0 outer=0",
    "CPF tick=2100 scene=5 label=spell event=274 actor=21 ability=1095331121",
    "CPF tick=2100 scene=5 label=context target=21 x=0.000 y=0.000",
    "CPF tick=2100 scene=5 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=2100 scene=5 label=resource mana=0.000 max=200.000 charges=1",
    "CPF tick=2100 scene=5 label=item type=1886216558 held=1 transferred=0 outer=0",
    "CPF tick=2100 scene=5 label=spell event=275 actor=21 ability=1095331121",
    "CPF tick=2100 scene=5 label=context target=21 x=0.000 y=0.000",
    "CPF tick=2100 scene=5 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=2100 scene=5 label=resource mana=150.000 max=200.000 charges=1",
    "CPF tick=2100 scene=5 label=item type=1886216558 held=1 transferred=0 outer=0",
    "CPF tick=2100 scene=5 label=spell event=276 actor=21 ability=1095331121",
    "CPF tick=2100 scene=5 label=context target=21 x=0.000 y=0.000",
    "CPF tick=2100 scene=5 label=spell-state head=852066 type=1701341552 life=525.000",
    "CPF tick=2100 scene=5 label=resource mana=150.000 max=200.000 charges=1",
    "CPF tick=2100 scene=5 label=item type=1886216558 held=1 transferred=0 outer=0",
    "CPF tick=2100 scene=5 label=used actor=21 item=1886216558 charges=0",
    "CPF tick=2100 scene=5 label=used-state head=852066 type=1701341552 life=525.000",
    "CPF tick=2100 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2100 scene=5 label=item type=1886216558 held=1 transferred=0 outer=0",
    "CPF tick=2100 scene=5 label=outer-return accepted=1",
    "CPF tick=2100 scene=5 label=outer-after head=0 type=1701341552 life=525.000",
    "CPF tick=2100 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2100 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2125 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2125 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2125 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2150 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2150 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2150 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2175 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2175 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2175 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2200 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2200 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2200 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2225 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2225 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2225 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2250 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2250 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2250 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2275 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2275 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2275 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2300 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2300 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2300 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2325 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2325 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2325 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2350 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2350 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2350 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2375 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2375 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2375 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2399 scene=5 label=settled head=0 type=1701341552 life=525.000",
    "CPF tick=2399 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2399 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2400 scene=5 label=sample head=0 type=1701341552 life=525.000",
    "CPF tick=2400 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2400 scene=5 label=item type=0 held=0 transferred=0 outer=0",
    "CPF tick=2400 scene=5 label=complete head=0 type=1701341552 life=525.000",
    "CPF tick=2400 scene=5 label=resource mana=150.000 max=200.000 charges=0",
    "CPF tick=2400 scene=5 label=item type=0 held=0 transferred=0 outer=0",
};
/* Native272CHANNEL273CAST274EFFECT275FINISH276ENDCAST; native FourCC. */
typedef struct { unsigned scene,tick,event,actor,target; uint32_t ability,x_word,y_word,clock_word; } coupleNativeContext249_t;
static coupleNativeContext249_t const couple_native_context249[] = {
{0,100,272,1,1,0x41496d31u,0x00000000u,0x00000000u,0x3ffffff0u},
{0,100,273,1,1,0x41496d31u,0x00000000u,0x00000000u,0x3ffffff0u},
{0,100,274,1,1,0x41496d31u,0x00000000u,0x00000000u,0x3ffffff0u},
{0,100,275,1,1,0x41496d31u,0x00000000u,0x00000000u,0x3ffffff0u},
{0,100,276,1,1,0x41496d31u,0x00000000u,0x00000000u,0x3ffffff0u},
{0,100,272,1,3,0x58696e66u,0x44d60a7fu,0x447c0de3u,0x3ffffff0u},
{0,100,273,1,3,0x58696e66u,0x44d60a7fu,0x447c0de3u,0x3ffffff0u},
{0,115,274,1,3,0x58696e66u,0x44d60a7fu,0x447c0de3u,0x4013330cu},
{0,140,275,1,0,0x58696e66u,0x00000000u,0x00000000u,0x4033d6aeu},
{0,140,276,1,0,0x58696e66u,0x00000000u,0x00000000u,0x4033d6aeu},
{1,500,272,5,5,0x41496d31u,0x00000000u,0x00000000u,0x411ffd3au},
{1,500,273,5,5,0x41496d31u,0x00000000u,0x00000000u,0x411ffd3au},
{1,500,274,5,5,0x41496d31u,0x00000000u,0x00000000u,0x411ffd3au},
{1,500,275,5,5,0x41496d31u,0x00000000u,0x00000000u,0x411ffd3au},
{1,500,276,5,5,0x41496d31u,0x00000000u,0x00000000u,0x411ffd3au},
{1,502,272,5,7,0x58696e66u,0x44dbbee1u,0x44b92a7fu,0x4120c9feu},
{1,502,273,5,7,0x58696e66u,0x44dbbee1u,0x44b92a7fu,0x4120c9feu},
{1,517,274,5,7,0x58696e66u,0x44dbbee1u,0x44b92a7fu,0x41259696u},
{1,543,275,5,0,0x58696e66u,0x00000000u,0x00000000u,0x412dbf32u},
{1,543,276,5,0,0x58696e66u,0x00000000u,0x00000000u,0x412dbf32u},
{4,1700,272,17,17,0x41496d31u,0x00000000u,0x00000000u,0x4207fa67u},
{4,1700,273,17,17,0x41496d31u,0x00000000u,0x00000000u,0x4207fa67u},
{4,1700,274,17,17,0x41496d31u,0x00000000u,0x00000000u,0x4207fa67u},
{4,1700,275,17,17,0x41496d31u,0x00000000u,0x00000000u,0x4207fa67u},
{4,1700,276,17,17,0x41496d31u,0x00000000u,0x00000000u,0x4207fa67u},
{4,1704,272,17,19,0x58696e66u,0x44ef74feu,0x44e9a344u,0x42084c47u},
{4,1704,273,17,19,0x58696e66u,0x44ef74feu,0x44e9a344u,0x42084c47u},
{4,1719,274,17,19,0x58696e66u,0x44ef74feu,0x44e9a344u,0x42097f4fu},
{4,1744,275,17,0,0x58696e66u,0x00000000u,0x00000000u,0x420b8e61u},
{4,1744,276,17,0,0x58696e66u,0x00000000u,0x00000000u,0x420b8e61u},
{5,2100,272,21,21,0x41496d31u,0x00000000u,0x00000000u,0x4227fb05u},
{5,2100,273,21,21,0x41496d31u,0x00000000u,0x00000000u,0x4227fb05u},
{5,2100,274,21,21,0x41496d31u,0x00000000u,0x00000000u,0x4227fb05u},
{5,2100,275,21,21,0x41496d31u,0x00000000u,0x00000000u,0x4227fb05u},
{5,2100,276,21,21,0x41496d31u,0x00000000u,0x00000000u,0x4227fb05u},
};
#endif
