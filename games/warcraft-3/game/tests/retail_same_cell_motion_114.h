/* Complete read-only public scene91, cases0..3; repeated28 native commits. */
static uint32_t const same_cell_motion_114[][7]={
    {0x00000000u,0x3f828f54u,0x41008000u,0x41108000u,0x40296439u,0x407799b8u,0x3f78868eu},
    {0x00000000u,0x3f86665eu,0x4101c53bu,0x41125b64u,0x4058b4dau,0x404f7499u,0x3f437b1eu},
    {0x00000000u,0x3f8a3d68u,0x4103654eu,0x4113e9b4u,0x4058b57eu,0x404f73eeu,0x3f437a53u},
    {0x00000000u,0x3f8e1472u,0x41050562u,0x41157803u,0x4058b5cfu,0x404f7398u,0x3f4379efu},
    {0x00000000u,0x3f91eb7cu,0x4106a577u,0x41170651u,0x4058b4dau,0x404f7499u,0x3f437b1eu},
    {0x00000000u,0x3f95c286u,0x4108458au,0x411894a1u,0x4058b57eu,0x404f73eeu,0x3f437a53u},
    {0x00000000u,0x3f999990u,0x4109e59eu,0x411a22f0u,0x00000000u,0x00000000u,0x3f437a53u},
    {0x00000000u,0x411078c6u,0x41008000u,0x41108000u,0x40296439u,0x407799b8u,0x3f78868eu},
    {0x00000000u,0x4110f3a2u,0x4101c52du,0x41125b50u,0x4058b4dau,0x404f7499u,0x3f437b1eu},
    {0x00000000u,0x41116e7eu,0x4103652eu,0x4113e98fu,0x4058b52cu,0x404f7443u,0x3f437ab8u},
    {0x00000000u,0x4111e95au,0x41050530u,0x411577cdu,0x4058b52cu,0x404f7443u,0x3f437ab8u},
    {0x00000000u,0x41126436u,0x4106a532u,0x4117060bu,0x4058b4dau,0x404f7499u,0x3f437b1eu},
    {0x00000000u,0x4112df12u,0x41084533u,0x4118944au,0x4058b52cu,0x404f7443u,0x3f437ab8u},
    {0x00000000u,0x411359eeu,0x4109e535u,0x411a2288u,0x00000000u,0x00000000u,0x3f437ab8u},
    {0x00000000u,0x418810afu,0x41008000u,0x41108000u,0x40296439u,0x407799b8u,0x3f78868eu},
    {0x00000000u,0x41884e1du,0x4101c52du,0x41125b50u,0x4058b4dau,0x404f7499u,0x3f437b1eu},
    {0x00000000u,0x41888b8bu,0x4103652eu,0x4113e98fu,0x4058b52cu,0x404f7443u,0x3f437ab8u},
    {0x00000000u,0x4188c8f9u,0x41050530u,0x411577cdu,0x4058b52cu,0x404f7443u,0x3f437ab8u},
    {0x00000000u,0x41890667u,0x4106a532u,0x4117060bu,0x4058b4dau,0x404f7499u,0x3f437b1eu},
    {0x00000000u,0x418943d5u,0x41084533u,0x4118944au,0x4058b52cu,0x404f7443u,0x3f437ab8u},
    {0x00000000u,0x41898143u,0x4109e535u,0x411a2288u,0x00000000u,0x00000000u,0x3f437ab8u},
    {0x00000000u,0x41c82269u,0x41008000u,0x41108000u,0x40296439u,0x407799b8u,0x3f78868eu},
    {0x00000000u,0x41c85fd7u,0x4101c52du,0x41125b50u,0x4058b4dau,0x404f7499u,0x3f437b1eu},
    {0x00000000u,0x41c89d45u,0x4103652eu,0x4113e98fu,0x4058b52cu,0x404f7443u,0x3f437ab8u},
    {0x00000000u,0x41c8dab3u,0x41050530u,0x411577cdu,0x4058b52cu,0x404f7443u,0x3f437ab8u},
    {0x00000000u,0x41c91821u,0x4106a532u,0x4117060bu,0x4058b4dau,0x404f7499u,0x3f437b1eu},
    {0x00000000u,0x41c9558fu,0x41084533u,0x4118944au,0x4058b52cu,0x404f7443u,0x3f437ab8u},
    {0x00000000u,0x41c992fdu,0x4109e535u,0x411a2288u,0x00000000u,0x00000000u,0x3f437ab8u},
};

static char const same_cell_script_114[]=
    "globals\n"
    " unit udg_PathProbeUnit=null\n"
    " timer udg_PathProbeTimer=null\n"
    " integer udg_PathProbeTick=0\n"
    " integer udg_PathProbeCase=0\n"
    "endglobals\n"
    "function ModuloInteger takes integer dividend, integer divisor returns integer\n"
    "return dividend-(dividend/divisor)*divisor\nendfunction\n"
    "function PathProbeRecord takes string label returns nothing\n"
    " call Preload(\"PATHTRACE tick=\"+I2S(udg_PathProbeTick)+\" label=\"+label+\" case=\"+I2S(udg_PathProbeCase)+\" x=\"+R2S(GetUnitX(udg_PathProbeUnit))+\" y=\"+R2S(GetUnitY(udg_PathProbeUnit))+\" order=\"+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))\n"
    "endfunction\n"
    "function PathProbeBirth takes nothing returns nothing\n"
    " local integer cls=ModuloInteger(udg_PathProbeCase,4)\n"
    " if udg_PathProbeUnit!=null then\n"
    "  call RemoveUnit(udg_PathProbeUnit)\n"
    " endif\n"
    " call SetTerrainPathable(272.0,304.0,PATHING_TYPE_WALKABILITY,true)\n"
    " if cls==0 then\n"
    "  set udg_PathProbeUnit=CreateUnit(Player(0),'hF91',272.0,304.0,90.0)\n"
    " elseif cls==1 then\n"
    "  set udg_PathProbeUnit=CreateUnit(Player(0),'hF92',272.0,304.0,90.0)\n"
    " elseif cls==2 then\n"
    "  set udg_PathProbeUnit=CreateUnit(Player(0),'hF93',272.0,304.0,90.0)\n"
    " else\n"
    "  set udg_PathProbeUnit=CreateUnit(Player(0),'hF94',272.0,304.0,90.0)\n"
    " endif\n"
    " call SetUnitMoveSpeed(udg_PathProbeUnit,150.0)\n"
    " call PathProbeRecord(\"birth\")\n"
    "endfunction\n"
    "function PathProbeTick takes nothing returns nothing\n"
    " set udg_PathProbeTick=udg_PathProbeTick+1\n"
    " if ModuloInteger(udg_PathProbeTick,80)==10 then\n"
    "  if udg_PathProbeCase<4 then\n"
    "   call SetUnitX(udg_PathProbeUnit,257.0)\n"
    "   call SetUnitY(udg_PathProbeUnit,289.0)\n"
    "   call IssuePointOrder(udg_PathProbeUnit,\"move\",287.0,319.0)\n"
    "   call PathProbeRecord(\"same_cell_order\")\n"
    "  elseif udg_PathProbeCase<8 then\n"
    "   call SetUnitX(udg_PathProbeUnit,272.0)\n"
    "   call SetUnitY(udg_PathProbeUnit,304.0)\n"
    "   call SetTerrainPathable(272.0,304.0,PATHING_TYPE_WALKABILITY,false)\n"
    "   call PathProbeRecord(\"blocked_source\")\n"
    "   call IssuePointOrder(udg_PathProbeUnit,\"move\",624.0,632.0)\n"
    "   call PathProbeRecord(\"blocked_source_order\")\n"
    "  else\n"
    "   call IssuePointOrder(udg_PathProbeUnit,\"move\",624.0,632.0)\n"
    "   call PathProbeRecord(\"moving_before_block\")\n"
    "  endif\n"
    " endif\n"
    " if udg_PathProbeCase>=8 and ModuloInteger(udg_PathProbeTick,80)==12 then\n"
    "  call SetTerrainPathable(GetUnitX(udg_PathProbeUnit),GetUnitY(udg_PathProbeUnit),PATHING_TYPE_WALKABILITY,false)\n"
    "  call PathProbeRecord(\"block_current_source\")\n"
    " endif\n"
    " call PathProbeRecord(\"sample\")\n"
    " if ModuloInteger(udg_PathProbeTick,80)==0 then\n"
    "  set udg_PathProbeCase=udg_PathProbeCase+1\n"
    "  if udg_PathProbeCase==4 then\n"
    "   call PathProbeRecord(\"complete\")\n"
    "   call PauseTimer(udg_PathProbeTimer)\n"
    "  else\n"
    "   call PathProbeBirth()\n"
    "  endif\n"
    " endif\n"
    "endfunction\n"
    "function PathProbeInit takes nothing returns nothing\n"
    " call FogEnable(false)\n"
    " call FogMaskEnable(false)\n"
    " call SetCameraPosition(1008.0,1040.0)\n"
    " call PathProbeBirth()\n"
    " call PathProbeRecord(\"start_movement_bypasses\")\n"
    " set udg_PathProbeTimer=CreateTimer()\n"
    " call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)\n"
    "endfunction\n"
    "function main takes nothing returns nothing\ncall PathProbeInit()\nendfunction\n"
;
