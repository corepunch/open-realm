/* Original 1.27.1.7085 public query sequence: two read-only repeats and an
 * unhooked control. Only abstract modifier handle numbers are omitted here. */
static char const fog253_script[]=
    "type fogmodifier extends handle\n"
    "globals\n"
    " timer ft=null\n"
    " integer n=0\n"
    " fogmodifier keep=null\n"
    "endglobals\n"
    "function IntegerTertiaryOp takes boolean flag, integer valueA, integer valueB returns integer\n"
    "    if flag then\n"
    "        return valueA\n"
    "    else\n"
    "        return valueB\n"
    "    endif\n"
    "endfunction\n"
    "function F253Bits takes real x returns string\n"
    " local player p=Player(0)\n"
    " return I2S(IntegerTertiaryOp(IsVisibleToPlayer(x,256.0,p),1,0))+I2S(IntegerTertiaryOp(IsFoggedToPlayer(x,256.0,p),1,0))+I2S(IntegerTertiaryOp(IsMaskedToPlayer(x,256.0,p),1,0))\n"
    "endfunction\n"
    "function F253LocBits takes real x returns string\n"
    " local player p=Player(0)\n"
    " local location l=Location(x,256.0)\n"
    " local string bits=I2S(IntegerTertiaryOp(IsLocationVisibleToPlayer(l,p),1,0))+I2S(IntegerTertiaryOp(IsLocationFoggedToPlayer(l,p),1,0))+I2S(IntegerTertiaryOp(IsLocationMaskedToPlayer(l,p),1,0))\n"
    " call RemoveLocation(l)\n"
    " set l=null\n"
    " return bits\n"
    "endfunction\n"
    "function F253Mark takes string label returns nothing\n"
    " call Preload(\"F253 tick=\"+I2S(n)+\" label=\"+label+\" keep=\"+F253Bits(256.0)+\" stop=\"+F253Bits(512.0)+\" destroy=\"+F253Bits(768.0)+\" locations=\"+F253LocBits(256.0)+F253LocBits(512.0)+F253LocBits(768.0))\n"
    "endfunction\n"
    "function F253Tick takes nothing returns nothing\n"
    " local fogmodifier pulse=null\n"
    " local fogmodifier gone=null\n"
    " set n=n+1\n"
    " if n==20 then\n"
    "  call F253Mark(\"before\")\n"
    "  call FogEnable(false)\n"
    "  call FogMaskEnable(false)\n"
    "  call F253Mark(\"display-disabled\")\n"
    "  call FogEnable(true)\n"
    "  call FogMaskEnable(true)\n"
    "  call F253Mark(\"display-restored\")\n"
    " elseif n==21 then\n"
    "  call F253Mark(\"before-start\")\n"
    "  set keep=CreateFogModifierRadius(Player(0),FOG_OF_WAR_VISIBLE,256.0,256.0,160.0,false,true)\n"
    "  call FogModifierStart(keep)\n"
    "  call F253Mark(\"after-start h=\"+I2S(GetHandleId(keep)))\n"
    "  set pulse=CreateFogModifierRadius(Player(0),FOG_OF_WAR_VISIBLE,512.0,256.0,160.0,false,true)\n"
    "  call FogModifierStart(pulse)\n"
    "  call F253Mark(\"pulse-start h=\"+I2S(GetHandleId(pulse)))\n"
    "  call FogModifierStop(pulse)\n"
    "  call F253Mark(\"pulse-stop\")\n"
    "  set gone=CreateFogModifierRadius(Player(0),FOG_OF_WAR_VISIBLE,768.0,256.0,160.0,false,true)\n"
    "  call FogModifierStart(gone)\n"
    "  call F253Mark(\"destroy-start h=\"+I2S(GetHandleId(gone)))\n"
    "  call DestroyFogModifier(gone)\n"
    "  call F253Mark(\"after-destroy\")\n"
    " elseif n==31 then\n"
    "  call F253Mark(\"before-stop\")\n"
    "  call FogModifierStop(keep)\n"
    "  call F253Mark(\"after-stop\")\n"
    " elseif n==40 then\n"
    "  call F253Mark(\"complete\")\n"
    "  call PreloadGenEnd(\"rs-target253.txt\")\n"
    "  call PauseTimer(ft)\n"
    " endif\n"
    " if n>=20 and n<40 then\n"
    "  call F253Mark(\"sample\")\n"
    " endif\n"
    "endfunction\n"
    "function F253Init takes nothing returns nothing\n"
    " call PreloadGenClear()\n"
    " call PreloadGenStart()\n"
    " call FogEnable(true)\n"
    " call FogMaskEnable(true)\n"
    " call SetTimeOfDayScale(0)\n"
    " call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)\n"
    " set ft=CreateTimer()\n"
    " call TimerStart(ft,0.1,true,function F253Tick)\n"
    "endfunction\n"
    "\n"
    "function main takes nothing returns nothing\n"
    "call F253Init()\n"
    "endfunction\n"
;
static struct { unsigned counter; char const *marker; } const fog253_markers[]={
    {1090,"F253 tick=20 label=before keep=001 stop=001 destroy=001 locations=001001001"},
    {1090,"F253 tick=20 label=display-disabled keep=100 stop=100 destroy=100 locations=100100100"},
    {1090,"F253 tick=20 label=display-restored keep=001 stop=001 destroy=001 locations=001001001"},
    {1090,"F253 tick=20 label=sample keep=001 stop=001 destroy=001 locations=001001001"},
    {1094,"F253 tick=21 label=before-start keep=001 stop=001 destroy=001 locations=001001001"},
    {1094,"F253 tick=21 label=after-start keep=100 stop=001 destroy=001 locations=100001001"},
    {1094,"F253 tick=21 label=pulse-start keep=100 stop=100 destroy=001 locations=100100001"},
    {1094,"F253 tick=21 label=pulse-stop keep=100 stop=100 destroy=001 locations=100100001"},
    {1094,"F253 tick=21 label=destroy-start keep=100 stop=100 destroy=100 locations=100100100"},
    {1094,"F253 tick=21 label=after-destroy keep=100 stop=100 destroy=100 locations=100100100"},
    {1094,"F253 tick=21 label=sample keep=100 stop=100 destroy=100 locations=100100100"},
    {1097,"F253 tick=22 label=sample keep=100 stop=100 destroy=100 locations=100100100"},
    {1100,"F253 tick=23 label=sample keep=100 stop=100 destroy=100 locations=100100100"},
    {1104,"F253 tick=24 label=sample keep=100 stop=100 destroy=100 locations=100100100"},
    {1107,"F253 tick=25 label=sample keep=100 stop=010 destroy=010 locations=100010010"},
    {1110,"F253 tick=26 label=sample keep=100 stop=010 destroy=010 locations=100010010"},
    {1114,"F253 tick=27 label=sample keep=100 stop=010 destroy=010 locations=100010010"},
    {1117,"F253 tick=28 label=sample keep=100 stop=010 destroy=010 locations=100010010"},
    {1120,"F253 tick=29 label=sample keep=100 stop=010 destroy=010 locations=100010010"},
    {1124,"F253 tick=30 label=sample keep=100 stop=010 destroy=010 locations=100010010"},
    {1127,"F253 tick=31 label=before-stop keep=100 stop=010 destroy=010 locations=100010010"},
    {1127,"F253 tick=31 label=after-stop keep=100 stop=010 destroy=010 locations=100010010"},
    {1127,"F253 tick=31 label=sample keep=100 stop=010 destroy=010 locations=100010010"},
    {1130,"F253 tick=32 label=sample keep=100 stop=010 destroy=010 locations=100010010"},
    {1134,"F253 tick=33 label=sample keep=010 stop=010 destroy=010 locations=010010010"},
    {1137,"F253 tick=34 label=sample keep=010 stop=010 destroy=010 locations=010010010"},
    {1140,"F253 tick=35 label=sample keep=010 stop=010 destroy=010 locations=010010010"},
    {1144,"F253 tick=36 label=sample keep=010 stop=010 destroy=010 locations=010010010"},
    {1147,"F253 tick=37 label=sample keep=010 stop=010 destroy=010 locations=010010010"},
    {1150,"F253 tick=38 label=sample keep=010 stop=010 destroy=010 locations=010010010"},
    {1154,"F253 tick=39 label=sample keep=010 stop=010 destroy=010 locations=010010010"},
    {1157,"F253 tick=40 label=complete keep=010 stop=010 destroy=010 locations=010010010"},
};
/* Original1e0b80 outputs: flags(mask|fog<<1), player, visible/explored. */
static uint8_t const fog253_classification[256]={
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    1,4,4,4,1,4,4,4,1,4,4,4,1,4,4,4,
    1,4,4,4,1,4,4,4,1,4,4,4,1,4,4,4,
    1,4,4,4,1,4,4,4,1,4,4,4,1,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    2,2,4,4,2,2,4,4,2,2,4,4,2,2,4,4,
    2,2,4,4,2,2,4,4,2,2,4,4,2,2,4,4,
    2,2,4,4,2,2,4,4,2,2,4,4,2,2,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    1,2,4,4,1,2,4,4,1,2,4,4,1,2,4,4,
    1,2,4,4,1,2,4,4,1,2,4,4,1,2,4,4,
    1,2,4,4,1,2,4,4,1,2,4,4,1,2,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
};
