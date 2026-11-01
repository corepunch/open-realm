/* Generated from native243. Only Preload is replaced by the observation adapter. */
#ifndef RETAIL_COUPLE_INSTANT243_PREPARATION_H
#define RETAIL_COUPLE_INSTANT243_PREPARATION_H
/* Typed native IDs: 272 CHANNEL, 273 CAST, 274 EFFECT, 275 FINISH, 276 ENDCAST. */
static char const couple_native_probe243[] =
    "globals\n"
    " timer cpTimer=null\n"
    " integer cpTick=0\n"
    " integer cpScene=0\n"
    " unit array cpHippo\n"
    " unit array cpArcher\n"
    " unit cpBuff=null\n"
    " item cpPotion=null\n"
    " boolean cpNested=false\n"
    "endglobals\n"
    "function CpBool takes boolean b returns integer\n"
    " if b then\n"
    " return 1\n"
    " endif\n"
    " return 0\n"
    "endfunction\n"
    "function CpMark takes string label returns nothing\n"
    " call CouplePreparationObserve(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=\"+label+\" h0=\"+I2S(GetUnitCurrentOrder(cpHippo[0]))+\" h1=\"+I2S(GetUnitCurrentOrder(cpHippo[1]))+\" a1=\"+I2S(GetUnitCurrentOrder(cpArcher[1])))\n"
    " call CouplePreparationObserve(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=state mana=\"+R2S(GetUnitState(cpHippo[1],UNIT_STATE_MANA))+\" max=\"+R2S(GetUnitState(cpHippo[1],UNIT_STATE_MAX_MANA))+\" charges=\"+I2S(GetItemCharges(cpPotion)))\n"
    "endfunction\n"
    "function CpEvent takes nothing returns nothing\n"
    " local integer oid=GetIssuedOrderId()\n"
    " local boolean accepted=false\n"
    " if GetTriggerUnit()==cpHippo[0] or GetTriggerUnit()==cpHippo[1] or GetTriggerUnit()==cpArcher[0] or GetTriggerUnit()==cpArcher[1] then\n"
    " call CouplePreparationObserve(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=issued oid=\"+I2S(oid)+\" actor=\"+I2S(GetUnitUserData(GetTriggerUnit()))+\" target=\"+I2S(GetUnitUserData(GetOrderTargetUnit())))\n"
    " endif\n"
    " if GetTriggerUnit()==cpHippo[1] and oid>=852008 and oid<=852013 and not cpNested then\n"
    "  set cpNested=true\n"
    "  call CpMark(\"nested-before\")\n"
    "  set accepted=IssueTargetOrder(cpHippo[1],\"coupletarget\",cpArcher[1])\n"
    "  call CouplePreparationObserve(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=nested-return accepted=\"+I2S(CpBool(accepted)))\n"
    "  call CpMark(\"nested-after\")\n"
    " endif\n"
    "endfunction\n"
    "function CpSpell takes nothing returns nothing\n"
    " call CouplePreparationObserve(\"CPF tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=spell event=\"+I2S(GetHandleId(GetTriggerEventId()))+\" actor=\"+I2S(GetUnitUserData(GetTriggerUnit()))+\" ability=\"+I2S(GetSpellAbilityId()))\n"
    "endfunction\n"
    "function CpTick takes nothing returns nothing\n"
    " local boolean accepted=false\n"
    " set cpTick=cpTick+1\n"
    " if cpTick==1 then\n"
    "  call StartCampaignAI(Player(1),\"Scripts\\\\target243.ai\")\n"
    "  call CpMark(\"ai-started\")\n"
    " elseif cpTick==100 then\n"
    "  call CpMark(\"ordinary-before\")\n"
    "  set accepted=IssueTargetOrder(cpHippo[0],\"coupletarget\",cpArcher[0])\n"
    "  call CouplePreparationObserve(\"CPF tick=100 scene=0 label=ordinary-return accepted=\"+I2S(CpBool(accepted)))\n"
    " elseif cpTick==500 then\n"
    "  set cpScene=1\n"
    "  call CpMark(\"outer-before\")\n"
    "  set accepted=IssueTargetOrder(cpHippo[1],\"innerfire\",cpBuff)\n"
    "  call CouplePreparationObserve(\"CPF tick=500 scene=1 label=outer-return accepted=\"+I2S(CpBool(accepted)))\n"
    "  call CpMark(\"outer-after\")\n"
    " elseif cpTick==800 then\n"
    "  call CpMark(\"complete\")\n"
    "  call PreloadGenEnd(\"target-coupling-public243-setup-v4.txt\")\n"
    "  call PauseTimer(cpTimer)\n"
    " else\n"
    "  if ModuloInteger(cpTick,25)==0 then\n"
    "   call CpMark(\"sample\")\n"
    "  endif\n"
    " endif\n"
    "endfunction\n"
    "function PathProbeInit takes nothing returns nothing\n"
    " local integer i=0\n"
    " local trigger ti=CreateTrigger()\n"
    " local trigger tt=CreateTrigger()\n"
    " local trigger tp=CreateTrigger()\n"
    " local trigger ts=CreateTrigger()\n"
    " call PreloadGenClear()\n"
    " call PreloadGenStart()\n"
    " loop\n"
    "  exitwhen i==2\n"
    "  set cpArcher[i]=CreateUnit(Player(1),'earc',1280.0,512.0+I2R(i)*512.0,0.0)\n"
    "  set cpHippo[i]=CreateUnit(Player(1),'ehip',1536.0,512.0+I2R(i)*512.0,0.0)\n"
    "  call SetUnitUserData(cpArcher[i],i*2+1)\n"
    "  call SetUnitUserData(cpHippo[i],i*2+2)\n"
    "  call SetUnitAcquireRange(cpArcher[i],0.0)\n"
    "  call SetUnitAcquireRange(cpHippo[i],0.0)\n"
    "  set i=i+1\n"
    " endloop\n"
    " set cpBuff=CreateUnit(Player(1),'hfoo',1700.0,1024.0,0.0)\n"
    " call SetUnitAcquireRange(cpBuff,0.0)\n"
    " call UnitAddAbility(cpHippo[1],'AInv')\n"
    " call UnitAddAbility(cpHippo[1],'Xinf')\n"
    " call SetUnitState(cpHippo[1],UNIT_STATE_MANA,0.0)\n"
    " set cpPotion=UnitAddItemById(cpHippo[1],'pman')\n"
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
    " call FogEnable(false)\n"
    " call FogMaskEnable(false)\n"
    " call CpMark(\"start\")\n"
    " call CouplePreparationObserve(\"CPF tick=0 scene=0 label=setup inv=\"+I2S(GetUnitAbilityLevel(cpHippo[1],'AInv'))+\" outer=\"+I2S(GetUnitAbilityLevel(cpHippo[1],'Xinf'))+\" co=\"+I2S(GetUnitAbilityLevel(cpHippo[1],'Aco3'))+\" item=\"+I2S(GetItemTypeId(cpPotion)))\n"
    " call PreloadGenEnd(\"target-coupling-public243-setup-v4-start.txt\")\n"
    " call PreloadGenClear()\n"
    " call PreloadGenStart()\n"
    " set cpTimer=CreateTimer()\n"
    " call TimerStart(cpTimer,0.02,true,function CpTick)\n"
    "endfunction\n"
;
static char const *const couple_native_prefix243[] = {
    "CPF tick=0 scene=0 label=start h0=0 h1=0 a1=0",
    "CPF tick=0 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=0 scene=0 label=setup inv=1 outer=1 co=1 item=1886216558",
};
static char const *const couple_native_public243[] = {
    "CPF tick=0 scene=0 label=ai-thread-enter",
    "CPF tick=1 scene=0 label=ai-started h0=0 h1=0 a1=0",
    "CPF tick=1 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=25 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=25 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=50 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=50 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=75 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=75 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=100 scene=0 label=ordinary-before h0=0 h1=0 a1=0",
    "CPF tick=100 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=100 scene=0 label=issued oid=852507 actor=2 target=1",
    "CPF tick=100 scene=0 label=ordinary-return accepted=1",
    "CPF tick=125 scene=0 label=sample h0=852507 h1=0 a1=0",
    "CPF tick=125 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=130 scene=0 label=spell event=272 actor=2 ability=1097035571",
    "CPF tick=130 scene=0 label=spell event=273 actor=2 ability=1097035571",
    "CPF tick=130 scene=0 label=spell event=274 actor=2 ability=1097035571",
    "CPF tick=130 scene=0 label=spell event=276 actor=2 ability=1097035571",
    "CPF tick=150 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=150 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=175 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=175 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=200 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=200 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=225 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=225 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=250 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=250 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=275 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=275 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=300 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=300 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=325 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=325 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=350 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=350 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=375 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=375 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=400 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=400 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=425 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=425 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=450 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=450 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=475 scene=0 label=sample h0=0 h1=0 a1=0",
    "CPF tick=475 scene=0 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=outer-before h0=0 h1=0 a1=0",
    "CPF tick=500 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=issued oid=852066 actor=4 target=0",
    "CPF tick=500 scene=1 label=issued oid=852008 actor=4 target=0",
    "CPF tick=500 scene=1 label=nested-before h0=0 h1=852066 a1=0",
    "CPF tick=500 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=issued oid=852507 actor=4 target=3",
    "CPF tick=500 scene=1 label=spell event=272 actor=4 ability=1097035571",
    "CPF tick=500 scene=1 label=spell event=275 actor=4 ability=1097035571",
    "CPF tick=500 scene=1 label=spell event=276 actor=4 ability=1097035571",
    "CPF tick=500 scene=1 label=nested-return accepted=1",
    "CPF tick=500 scene=1 label=nested-after h0=0 h1=0 a1=0",
    "CPF tick=500 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=500 scene=1 label=spell event=272 actor=4 ability=1095331121",
    "CPF tick=500 scene=1 label=spell event=273 actor=4 ability=1095331121",
    "CPF tick=500 scene=1 label=spell event=274 actor=4 ability=1095331121",
    "CPF tick=500 scene=1 label=spell event=275 actor=4 ability=1095331121",
    "CPF tick=500 scene=1 label=spell event=276 actor=4 ability=1095331121",
    "CPF tick=500 scene=1 label=outer-return accepted=1",
    "CPF tick=500 scene=1 label=outer-after h0=0 h1=0 a1=0",
    "CPF tick=500 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=525 scene=1 label=sample h0=0 h1=0 a1=0",
    "CPF tick=525 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=550 scene=1 label=sample h0=0 h1=0 a1=0",
    "CPF tick=550 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=575 scene=1 label=sample h0=0 h1=0 a1=0",
    "CPF tick=575 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=600 scene=1 label=sample h0=0 h1=0 a1=0",
    "CPF tick=600 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=625 scene=1 label=sample h0=0 h1=0 a1=0",
    "CPF tick=625 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=650 scene=1 label=sample h0=0 h1=0 a1=0",
    "CPF tick=650 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=675 scene=1 label=sample h0=0 h1=0 a1=0",
    "CPF tick=675 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=700 scene=1 label=sample h0=0 h1=0 a1=0",
    "CPF tick=700 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=725 scene=1 label=sample h0=0 h1=0 a1=0",
    "CPF tick=725 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=750 scene=1 label=sample h0=0 h1=0 a1=0",
    "CPF tick=750 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=775 scene=1 label=sample h0=0 h1=0 a1=0",
    "CPF tick=775 scene=1 label=state mana=0.000 max=200.000 charges=1",
    "CPF tick=800 scene=1 label=complete h0=0 h1=0 a1=0",
    "CPF tick=800 scene=1 label=state mana=0.000 max=200.000 charges=1",
};
#endif
