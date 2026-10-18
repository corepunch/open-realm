#ifdef BZ_TESTS
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "shared/test.h"
extern void reset_entities(void),setup_test_world(void);
bool run_test_jass(cstring_t);
extern slkTestData_t *parse_slk_string(cstring_t);
extern void free_slk_rows(slkTestData_t *);

/* Native377 has two identical2908-record transactions and464 public markers
 * also matching a no-hook control. This fixture starts with the already
 * resolved edge words, separating task retention from setter quantization. */
static char const *target377_canonical_tables[]={"UnitData","UnitBalance","UnitUI","UnitAbilities","UnitWeapons"};
static char const *target377_canonical_slk[]={
    "ID;PWXL;N;E\nC;X1;Y1;K\"unitID\"\nC;X2;Y1;K\"sort\"\nC;X3;Y1;K\"race\"\nC;X4;Y1;K\"prio\"\nC;X5;Y1;K\"threat\"\nC;X6;Y1;K\"valid\"\nC;X7;Y1;K\"deathType\"\nC;X8;Y1;K\"death\"\nC;X9;Y1;K\"canSleep\"\nC;X10;Y1;K\"cargoSize\"\nC;X11;Y1;K\"movetp\"\nC;X12;Y1;K\"moveHeight\"\nC;X13;Y1;K\"moveFloor\"\nC;X14;Y1;K\"turnRate\"\nC;X15;Y1;K\"propWin\"\nC;X16;Y1;K\"orientInterp\"\nC;X17;Y1;K\"formation\"\nC;X18;Y1;K\"targType\"\nC;X19;Y1;K\"pathTex\"\nC;X20;Y1;K\"fatLOS\"\nC;X21;Y1;K\"points\"\nC;X22;Y1;K\"buffType\"\nC;X23;Y1;K\"buffRadius\"\nC;X24;Y1;K\"nameCount\"\nC;X25;Y1;K\"canFlee\"\nC;X26;Y1;K\"requireWaterRadius\"\nC;X27;Y1;K\"isBuildOn\"\nC;X28;Y1;K\"canBuildOn\"\nC;X29;Y1;K\"InBeta\"\nC;X30;Y1;K\"version\"\nC;X31;Y1;K\"collision\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"a2\"\nC;X3;Y2;K\"human\"\nC;X4;Y2;K\"6\"\nC;X5;Y2;K\"1\"\nC;X6;Y2;K\"1\"\nC;X7;Y2;K\"3\"\nC;X8;Y2;K\"3.04\"\nC;X9;Y2;K\"0\"\nC;X10;Y2;K\"1\"\nC;X11;Y2;K\"foot\"\nC;X12;Y2;K\"0\"\nC;X13;Y2;K\"0\"\nC;X14;Y2;K\"0.6\"\nC;X15;Y2;K\"60\"\nC;X16;Y2;K\"0\"\nC;X17;Y2;K\"0\"\nC;X18;Y2;K\"ground\"\nC;X19;Y2;K\"_\"\nC;X20;Y2;K\"0\"\nC;X21;Y2;K\"100\"\nC;X22;Y2;K\"_\"\nC;X23;Y2;K\"-\"\nC;X24;Y2;K\"-\"\nC;X25;Y2;K\"1\"\nC;X26;Y2;K\"0\"\nC;X27;Y2;K\"0\"\nC;X28;Y2;K\"0\"\nC;X29;Y2;K\"1\"\nC;X30;Y2;K\"0\"\nC;X31;Y2;K\"31\"\nC;X1;Y3;K\"nwgt\"\nC;X2;Y3;K\"p3\"\nC;X3;Y3;K\"other\"\nC;X4;Y3;K\"1\"\nC;X5;Y3;K\"1\"\nC;X6;Y3;K\"1\"\nC;X7;Y3;K\"2\"\nC;X8;Y3;K\"3\"\nC;X9;Y3;K\"0\"\nC;X10;Y3;K\"-\"\nC;X11;Y3;K\"_\"\nC;X12;Y3;K\"0\"\nC;X13;Y3;K\"0\"\nC;X14;Y3;K\"-\"\nC;X15;Y3;K\"60\"\nC;X16;Y3;K\"0\"\nC;X17;Y3;K\"0\"\nC;X18;Y3;K\"structure\"\nC;X19;Y3;K\"PathTextures\\Waygate.tga\"\nC;X20;Y3;K\"0\"\nC;X21;Y3;K\"100\"\nC;X22;Y3;K\"_\"\nC;X23;Y3;K\"-\"\nC;X24;Y3;K\"-\"\nC;X25;Y3;K\"1\"\nC;X26;Y3;K\"0\"\nC;X27;Y3;K\"0\"\nC;X28;Y3;K\"0\"\nC;X29;Y3;K\"1\"\nC;X30;Y3;K\"0\"\nC;X31;Y3;K\"50\"\nE\n",
    "ID;PWXL;N;E\nC;X1;Y1;K\"unitBalanceID\"\nC;X2;Y1;K\"sortBalance\"\nC;X3;Y1;K\"sort2\"\nC;X4;Y1;K\"level\"\nC;X5;Y1;K\"type\"\nC;X6;Y1;K\"goldcost\"\nC;X7;Y1;K\"lumbercost\"\nC;X8;Y1;K\"goldRep\"\nC;X9;Y1;K\"lumberRep\"\nC;X10;Y1;K\"fmade\"\nC;X11;Y1;K\"fused\"\nC;X12;Y1;K\"bountydice\"\nC;X13;Y1;K\"bountysides\"\nC;X14;Y1;K\"bountyplus\"\nC;X15;Y1;K\"lumberbountydice\"\nC;X16;Y1;K\"lumberbountysides\"\nC;X17;Y1;K\"lumberbountyplus\"\nC;X18;Y1;K\"stockMax\"\nC;X19;Y1;K\"stockRegen\"\nC;X20;Y1;K\"stockStart\"\nC;X21;Y1;K\"HP\"\nC;X22;Y1;K\"realHP\"\nC;X23;Y1;K\"regenHP\"\nC;X24;Y1;K\"regenType\"\nC;X25;Y1;K\"manaN\"\nC;X26;Y1;K\"realM\"\nC;X27;Y1;K\"mana0\"\nC;X28;Y1;K\"regenMana\"\nC;X29;Y1;K\"def\"\nC;X30;Y1;K\"defUp\"\nC;X31;Y1;K\"realdef\"\nC;X32;Y1;K\"defType\"\nC;X33;Y1;K\"spd\"\nC;X34;Y1;K\"minSpd\"\nC;X35;Y1;K\"maxSpd\"\nC;X36;Y1;K\"bldtm\"\nC;X37;Y1;K\"reptm\"\nC;X38;Y1;K\"sight\"\nC;X39;Y1;K\"nsight\"\nC;X40;Y1;K\"STR\"\nC;X41;Y1;K\"INT\"\nC;X42;Y1;K\"AGI\"\nC;X43;Y1;K\"STRplus\"\nC;X44;Y1;K\"INTplus\"\nC;X45;Y1;K\"AGIplus\"\nC;X46;Y1;K\"abilTest\"\nC;X47;Y1;K\"Primary\"\nC;X48;Y1;K\"upgrades\"\nC;X49;Y1;K\"tilesets\"\nC;X50;Y1;K\"nbrandom\"\nC;X51;Y1;K\"isbldg\"\nC;X52;Y1;K\"preventPlace\"\nC;X53;Y1;K\"requirePlace\"\nC;X54;Y1;K\"repulse\"\nC;X55;Y1;K\"repulseParam\"\nC;X56;Y1;K\"repulseGroup\"\nC;X57;Y1;K\"repulsePrio\"\nC;X58;Y1;K\"collision\"\nC;X59;Y1;K\"InBeta\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"a2\"\nC;X3;Y2;K\"me1\"\nC;X4;Y2;K\"2\"\nC;X5;Y2;K\"_\"\nC;X6;Y2;K\"135\"\nC;X7;Y2;K\"0\"\nC;X8;Y2;K\"135\"\nC;X9;Y2;K\"0\"\nC;X10;Y2;K\" - \"\nC;X11;Y2;K\"2\"\nC;X12;Y2;K\"6\"\nC;X13;Y2;K\"3\"\nC;X14;Y2;K\"20\"\nC;X15;Y2;K\"0\"\nC;X16;Y2;K\"0\"\nC;X17;Y2;K\"0\"\nC;X18;Y2;K\"3\"\nC;X19;Y2;K\"30\"\nC;X20;Y2;K\"0\"\nC;X21;Y2;K\"420\"\nC;X22;Y2;K\"420\"\nC;X23;Y2;K\"0.25\"\nC;X24;Y2;K\"always\"\nC;X25;Y2;K\" - \"\nC;X26;Y2;K\" - \"\nC;X27;Y2;K\" - \"\nC;X28;Y2;K\" - \"\nC;X29;Y2;K\"2\"\nC;X30;Y2;K\"2\"\nC;X31;Y2;K\"2\"\nC;X32;Y2;K\"large\"\nC;X33;Y2;K\"270\"\nC;X34;Y2;K\"0\"\nC;X35;Y2;K\"0\"\nC;X36;Y2;K\"20\"\nC;X37;Y2;K\"20\"\nC;X38;Y2;K\"1400\"\nC;X39;Y2;K\"800\"\nC;X40;Y2;K\" - \"\nC;X41;Y2;K\"-\"\nC;X42;Y2;K\"-\"\nC;X43;Y2;K\" - \"\nC;X44;Y2;K\" - \"\nC;X45;Y2;K\" - \"\nC;X46;Y2;K\"-\"\nC;X47;Y2;K\"_\"\nC;X48;Y2;K\"Rhar,Rhme,Rhde,Rhpm,Rguv\"\nC;X49;Y2;K\"*\"\nC;X50;Y2;K\"-\"\nC;X51;Y2;K\"0\"\nC;X52;Y2;K\"_\"\nC;X53;Y2;K\"_\"\nC;X54;Y2;K\"2\"\nC;X55;Y2;K\"17\"\nC;X56;Y2;K\"17\"\nC;X57;Y2;K\"17\"\nC;X58;Y2;K\"31\"\nC;X59;Y2;K\"1\"\nC;X1;Y3;K\"nwgt\"\nC;X2;Y3;K\"p3\"\nC;X3;Y3;K\"zz\"\nC;X4;Y3;K\"-\"\nC;X5;Y3;K\"Mechanical,neutral,standon\"\nC;X6;Y3;K\"50\"\nC;X7;Y3;K\"0\"\nC;X8;Y3;K\"50\"\nC;X9;Y3;K\"0\"\nC;X10;Y3;K\" - \"\nC;X11;Y3;K\"-\"\nC;X12;Y3;K\"0\"\nC;X13;Y3;K\"0\"\nC;X14;Y3;K\"0\"\nC;X15;Y3;K\"0\"\nC;X16;Y3;K\"0\"\nC;X17;Y3;K\"0\"\nC;X18;Y3;K\"-\"\nC;X19;Y3;K\"-\"\nC;X20;Y3;K\"-\"\nC;X21;Y3;K\"150000\"\nC;X22;Y3;K\"150000\"\nC;X23;Y3;K\"-\"\nC;X24;Y3;K\"none\"\nC;X25;Y3;K\" - \"\nC;X26;Y3;K\" - \"\nC;X27;Y3;K\" - \"\nC;X28;Y3;K\" - \"\nC;X29;Y3;K\"0\"\nC;X30;Y3;K\"2\"\nC;X31;Y3;K\"0\"\nC;X32;Y3;K\"fort\"\nC;X33;Y3;K\"-\"\nC;X34;Y3;K\"0\"\nC;X35;Y3;K\"0\"\nC;X36;Y3;K\"3\"\nC;X37;Y3;K\"3\"\nC;X38;Y3;K\"500\"\nC;X39;Y3;K\"500\"\nC;X40;Y3;K\" - \"\nC;X41;Y3;K\" - \"\nC;X42;Y3;K\" - \"\nC;X43;Y3;K\" - \"\nC;X44;Y3;K\" - \"\nC;X45;Y3;K\" - \"\nC;X46;Y3;K\"-\"\nC;X47;Y3;K\"_\"\nC;X48;Y3;K\"_\"\nC;X49;Y3;K\"*\"\nC;X50;Y3;K\"0\"\nC;X51;Y3;K\"1\"\nC;X52;Y3;K\"_\"\nC;X53;Y3;K\"_\"\nC;X54;Y3;K\"0\"\nC;X55;Y3;K\"0\"\nC;X56;Y3;K\"0\"\nC;X57;Y3;K\"0\"\nC;X58;Y3;K\"50\"\nC;X59;Y3;K\"1\"\nE\n",
    "ID;PWXL;N;E\nC;X1;Y1;K\"unitUIID\"\nC;X2;Y1;K\"sortUI\"\nC;X3;Y1;K\"file\"\nC;X4;Y1;K\"fileVerFlags\"\nC;X5;Y1;K\"unitSound\"\nC;X6;Y1;K\"tilesetSpecific\"\nC;X7;Y1;K\"name\"\nC;X8;Y1;K\"unitClass\"\nC;X9;Y1;K\"special\"\nC;X10;Y1;K\"campaign\"\nC;X11;Y1;K\"inEditor\"\nC;X12;Y1;K\"hiddenInEditor\"\nC;X13;Y1;K\"hostilePal\"\nC;X14;Y1;K\"dropItems\"\nC;X15;Y1;K\"nbmmIcon\"\nC;X16;Y1;K\"useClickHelper\"\nC;X17;Y1;K\"hideHeroBar\"\nC;X18;Y1;K\"hideHeroMinimap\"\nC;X19;Y1;K\"hideHeroDeathMsg\"\nC;X20;Y1;K\"hideOnMinimap\"\nC;X21;Y1;K\"blend\"\nC;X22;Y1;K\"scale\"\nC;X23;Y1;K\"scaleBull\"\nC;X24;Y1;K\"maxPitch\"\nC;X25;Y1;K\"maxRoll\"\nC;X26;Y1;K\"elevPts\"\nC;X27;Y1;K\"elevRad\"\nC;X28;Y1;K\"fogRad\"\nC;X29;Y1;K\"walk\"\nC;X30;Y1;K\"run\"\nC;X31;Y1;K\"selZ\"\nC;X32;Y1;K\"weap1\"\nC;X33;Y1;K\"weap2\"\nC;X34;Y1;K\"teamColor\"\nC;X35;Y1;K\"customTeamColor\"\nC;X36;Y1;K\"armor\"\nC;X37;Y1;K\"modelScale\"\nC;X38;Y1;K\"red\"\nC;X39;Y1;K\"green\"\nC;X40;Y1;K\"blue\"\nC;X41;Y1;K\"uberSplat\"\nC;X42;Y1;K\"unitShadow\"\nC;X43;Y1;K\"buildingShadow\"\nC;X44;Y1;K\"shadowW\"\nC;X45;Y1;K\"shadowH\"\nC;X46;Y1;K\"shadowX\"\nC;X47;Y1;K\"shadowY\"\nC;X48;Y1;K\"shadowOnWater\"\nC;X49;Y1;K\"selCircOnWater\"\nC;X50;Y1;K\"occH\"\nC;X51;Y1;K\"InBeta\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"a2\"\nC;X3;Y2;K\"TestUI\\Models\\anim_pulse.mdx\"\nC;X4;Y2;K\"0\"\nC;X5;Y2;K\"Footman\"\nC;X6;Y2;K\"0\"\nC;X7;Y2;K\"footman\"\nC;X8;Y2;K\"HUnit02\"\nC;X9;Y2;K\"0\"\nC;X10;Y2;K\"0\"\nC;X11;Y2;K\"1\"\nC;X12;Y2;K\"0\"\nC;X13;Y2;K\"-\"\nC;X14;Y2;K\"1\"\nC;X15;Y2;K\"-\"\nC;X16;Y2;K\"0\"\nC;X17;Y2;K\"0\"\nC;X18;Y2;K\"0\"\nC;X19;Y2;K\"0\"\nC;X20;Y2;K\"0\"\nC;X21;Y2;K\"0.15\"\nC;X22;Y2;K\"1\"\nC;X23;Y2;K\"1\"\nC;X24;Y2;K\"10\"\nC;X25;Y2;K\"10\"\nC;X26;Y2;K\"-\"\nC;X27;Y2;K\"20\"\nC;X28;Y2;K\"0\"\nC;X29;Y2;K\"210\"\nC;X30;Y2;K\"210\"\nC;X31;Y2;K\"0\"\nC;X32;Y2;K\"MetalMediumSlice\"\nC;X33;Y2;K\"_\"\nC;X34;Y2;K\"-1\"\nC;X35;Y2;K\"0\"\nC;X36;Y2;K\"Metal\"\nC;X37;Y2;K\"1\"\nC;X38;Y2;K\"255\"\nC;X39;Y2;K\"255\"\nC;X40;Y2;K\"255\"\nC;X41;Y2;K\"_\"\nC;X42;Y2;K\"Shadow\"\nC;X43;Y2;K\"_\"\nC;X44;Y2;K\"140\"\nC;X45;Y2;K\"140\"\nC;X46;Y2;K\"50\"\nC;X47;Y2;K\"50\"\nC;X48;Y2;K\"1\"\nC;X49;Y2;K\"0\"\nC;X50;Y2;K\"0\"\nC;X51;Y2;K\"1\"\nC;X1;Y3;K\"nwgt\"\nC;X2;Y3;K\"p3\"\nC;X3;Y3;K\"TestUI\\Models\\anim_pulse.mdx\"\nC;X4;Y3;K\"0\"\nC;X5;Y3;K\"Waygate\"\nC;X6;Y3;K\"0\"\nC;X7;Y3;K\"waygate\"\nC;X8;Y3;K\"Neutral08\"\nC;X9;Y3;K\"0\"\nC;X10;Y3;K\"0\"\nC;X11;Y3;K\"1\"\nC;X12;Y3;K\"0\"\nC;X13;Y3;K\"0\"\nC;X14;Y3;K\"1\"\nC;X15;Y3;K\"1\"\nC;X16;Y3;K\"0\"\nC;X17;Y3;K\"0\"\nC;X18;Y3;K\"0\"\nC;X19;Y3;K\"0\"\nC;X20;Y3;K\"0\"\nC;X21;Y3;K\"0.15\"\nC;X22;Y3;K\"3.5\"\nC;X23;Y3;K\"1\"\nC;X24;Y3;K\"0\"\nC;X25;Y3;K\"0\"\nC;X26;Y3;K\"4\"\nC;X27;Y3;K\"50\"\nC;X28;Y3;K\"0\"\nC;X29;Y3;K\"200\"\nC;X30;Y3;K\"200\"\nC;X31;Y3;K\"0\"\nC;X32;Y3;K\"_\"\nC;X33;Y3;K\"_\"\nC;X34;Y3;K\"-1\"\nC;X35;Y3;K\"1\"\nC;X36;Y3;K\"Stone\"\nC;X37;Y3;K\"1\"\nC;X38;Y3;K\"255\"\nC;X39;Y3;K\"255\"\nC;X40;Y3;K\"255\"\nC;X41;Y3;K\"HMED\"\nC;X42;Y3;K\"_\"\nC;X43;Y3;K\"BuildingShadowLarge\"\nC;X48;Y3;K\"1\"\nC;X49;Y3;K\"0\"\nC;X50;Y3;K\"0\"\nC;X51;Y3;K\"1\"\nE\n",
    "ID;PWXL;N;E\nC;X1;Y1;K\"unitAbilID\"\nC;X2;Y1;K\"sortAbil\"\nC;X3;Y1;K\"auto\"\nC;X4;Y1;K\"abilList\"\nC;X5;Y1;K\"InBeta\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"a2\"\nC;X3;Y2;K\"_\"\nC;X4;Y2;K\"Adef,Aihn\"\nC;X5;Y2;K\"1\"\nC;X1;Y3;K\"nwgt\"\nC;X2;Y3;K\"p3\"\nC;X3;Y3;K\"_\"\nC;X4;Y3;K\"Awrp,Avul\"\nC;X5;Y3;K\"1\"\nE\n",
    "ID;PWXL;N;E\nC;X1;Y1;K\"unitWeapID\"\nC;X2;Y1;K\"sortWeap\"\nC;X3;Y1;K\"sort2\"\nC;X4;Y1;K\"weapsOn\"\nC;X5;Y1;K\"acquire\"\nC;X6;Y1;K\"minRange\"\nC;X7;Y1;K\"castpt\"\nC;X8;Y1;K\"castbsw\"\nC;X9;Y1;K\"launchX\"\nC;X10;Y1;K\"launchY\"\nC;X11;Y1;K\"launchZ\"\nC;X12;Y1;K\"launchSwimZ\"\nC;X13;Y1;K\"impactZ\"\nC;X14;Y1;K\"impactSwimZ\"\nC;X15;Y1;K\"weapType1\"\nC;X16;Y1;K\"targs1\"\nC;X17;Y1;K\"showUI1\"\nC;X18;Y1;K\"rangeN1\"\nC;X19;Y1;K\"RngTst\"\nC;X20;Y1;K\"RngBuff1\"\nC;X21;Y1;K\"atkType1\"\nC;X22;Y1;K\"weapTp1\"\nC;X23;Y1;K\"cool1\"\nC;X24;Y1;K\"mincool1\"\nC;X25;Y1;K\"dice1\"\nC;X26;Y1;K\"sides1\"\nC;X27;Y1;K\"dmgplus1\"\nC;X28;Y1;K\"dmgUp1\"\nC;X29;Y1;K\"mindmg1\"\nC;X30;Y1;K\"avgdmg1\"\nC;X31;Y1;K\"maxdmg1\"\nC;X32;Y1;K\"dmgpt1\"\nC;X33;Y1;K\"backSw1\"\nC;X34;Y1;K\"Farea1\"\nC;X35;Y1;K\"Harea1\"\nC;X36;Y1;K\"Qarea1\"\nC;X37;Y1;K\"Hfact1\"\nC;X38;Y1;K\"Qfact1\"\nC;X39;Y1;K\"splashTargs1\"\nC;X40;Y1;K\"targCount1\"\nC;X41;Y1;K\"damageLoss1\"\nC;X42;Y1;K\"spillDist1\"\nC;X43;Y1;K\"spillRadius1\"\nC;X44;Y1;K\"DmgUpg\"\nC;X45;Y1;K\"dmod1\"\nC;X46;Y1;K\"DPS\"\nC;X47;Y1;K\"weapType2\"\nC;X48;Y1;K\"targs2\"\nC;X49;Y1;K\"showUI2\"\nC;X50;Y1;K\"rangeN2\"\nC;X51;Y1;K\"RngTst2\"\nC;X52;Y1;K\"RngBuff2\"\nC;X53;Y1;K\"atkType2\"\nC;X54;Y1;K\"weapTp2\"\nC;X55;Y1;K\"cool2\"\nC;X56;Y1;K\"mincool2\"\nC;X57;Y1;K\"dice2\"\nC;X58;Y1;K\"sides2\"\nC;X59;Y1;K\"dmgplus2\"\nC;X60;Y1;K\"dmgUp2\"\nC;X61;Y1;K\"mindmg2\"\nC;X62;Y1;K\"avgdmg2\"\nC;X63;Y1;K\"maxdmg2\"\nC;X64;Y1;K\"dmgpt2\"\nC;X65;Y1;K\"backSw2\"\nC;X66;Y1;K\"Farea2\"\nC;X67;Y1;K\"Harea2\"\nC;X68;Y1;K\"Qarea2\"\nC;X69;Y1;K\"Hfact2\"\nC;X70;Y1;K\"Qfact2\"\nC;X71;Y1;K\"splashTargs2\"\nC;X72;Y1;K\"targCount2\"\nC;X73;Y1;K\"damageLoss2\"\nC;X74;Y1;K\"spillDist2\"\nC;X75;Y1;K\"spillRadius2\"\nC;X76;Y1;K\"InBeta\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"a2\"\nC;X3;Y2;K\"me1\"\nC;X4;Y2;K\"1\"\nC;X5;Y2;K\"500\"\nC;X6;Y2;K\"-\"\nC;X7;Y2;K\"0.3\"\nC;X8;Y2;K\"0.51\"\nC;X9;Y2;K\"0\"\nC;X10;Y2;K\"0\"\nC;X11;Y2;K\"60\"\nC;X12;Y2;K\"0\"\nC;X13;Y2;K\"60\"\nC;X14;Y2;K\"0\"\nC;X15;Y2;K\"MetalMediumSlice\"\nC;X16;Y2;K\"ground,structure,debris,item,ward\"\nC;X17;Y2;K\"1\"\nC;X18;Y2;K\"90\"\nC;X19;Y2;K\"-\"\nC;X20;Y2;K\"250\"\nC;X21;Y2;K\"normal\"\nC;X22;Y2;K\"normal\"\nC;X23;Y2;K\"1.35\"\nC;X24;Y2;K\"-\"\nC;X25;Y2;K\"1\"\nC;X26;Y2;K\"2\"\nC;X27;Y2;K\"11\"\nC;X28;Y2;K\"-\"\nC;X29;Y2;K\"12\"\nC;X30;Y2;K\"12.5\"\nC;X31;Y2;K\"13\"\nC;X32;Y2;K\"0.5\"\nC;X33;Y2;K\"0.5\"\nC;X34;Y2;K\" - \"\nC;X35;Y2;K\" - \"\nC;X36;Y2;K\" - \"\nC;X37;Y2;K\"-\"\nC;X38;Y2;K\"-\"\nC;X39;Y2;K\"_\"\nC;X40;Y2;K\"1\"\nC;X41;Y2;K\"0\"\nC;X42;Y2;K\"0\"\nC;X43;Y2;K\"0\"\nC;X44;Y2;K\"-\"\nC;X45;Y2;K\"-\"\nC;X46;Y2;K\"9.25925925925926\"\nC;X47;Y2;K\"_\"\nC;X48;Y2;K\"_\"\nC;X49;Y2;K\"1\"\nC;X50;Y2;K\"-\"\nC;X51;Y2;K\"-\"\nC;X52;Y2;K\"-\"\nC;X53;Y2;K\"normal\"\nC;X54;Y2;K\"_\"\nC;X55;Y2;K\"-\"\nC;X56;Y2;K\"-\"\nC;X57;Y2;K\"-\"\nC;X58;Y2;K\"-\"\nC;X59;Y2;K\"-\"\nC;X60;Y2;K\"-\"\nC;X61;Y2;K\" - \"\nC;X62;Y2;K\"-\"\nC;X63;Y2;K\" - \"\nC;X64;Y2;K\"-\"\nC;X65;Y2;K\"-\"\nC;X66;Y2;K\"-\"\nC;X67;Y2;K\"-\"\nC;X68;Y2;K\"-\"\nC;X69;Y2;K\"-\"\nC;X70;Y2;K\"-\"\nC;X71;Y2;K\"_\"\nC;X72;Y2;K\"1\"\nC;X73;Y2;K\"0\"\nC;X74;Y2;K\"0\"\nC;X75;Y2;K\"0\"\nC;X76;Y2;K\"1\"\nC;X1;Y3;K\"nwgt\"\nC;X2;Y3;K\"p3\"\nC;X3;Y3;K\"zz\"\nC;X4;Y3;K\"0\"\nC;X5;Y3;K\"500\"\nC;X6;Y3;K\"-\"\nC;X7;Y3;K\"-\"\nC;X8;Y3;K\"0.51\"\nC;X9;Y3;K\"0\"\nC;X10;Y3;K\"0\"\nC;X11;Y3;K\"60\"\nC;X12;Y3;K\"0\"\nC;X13;Y3;K\"120\"\nC;X14;Y3;K\"0\"\nC;X15;Y3;K\"_\"\nC;X16;Y3;K\"_\"\nC;X17;Y3;K\"1\"\nC;X18;Y3;K\"-\"\nC;X19;Y3;K\"-\"\nC;X20;Y3;K\"-\"\nC;X21;Y3;K\"normal\"\nC;X22;Y3;K\"_\"\nC;X23;Y3;K\"0\"\nC;X24;Y3;K\"-\"\nC;X25;Y3;K\"-\"\nC;X26;Y3;K\"-\"\nC;X27;Y3;K\"-\"\nC;X28;Y3;K\"-\"\nC;X29;Y3;K\" - \"\nC;X30;Y3;K\"-\"\nC;X31;Y3;K\" - \"\nC;X32;Y3;K\"-\"\nC;X33;Y3;K\"-\"\nC;X34;Y3;K\" - \"\nC;X35;Y3;K\" - \"\nC;X36;Y3;K\" - \"\nC;X37;Y3;K\"-\"\nC;X38;Y3;K\"-\"\nC;X39;Y3;K\"_\"\nC;X40;Y3;K\"1\"\nC;X41;Y3;K\"0\"\nC;X42;Y3;K\"0\"\nC;X43;Y3;K\"0\"\nC;X44;Y3;K\"#VALUE!\"\nC;X45;Y3;K\"#VALUE!\"\nC;X46;Y3;K\"#VALUE!\"\nC;X47;Y3;K\"_\"\nC;X48;Y3;K\"_\"\nC;X49;Y3;K\"1\"\nC;X50;Y3;K\"-\"\nC;X51;Y3;K\"-\"\nC;X52;Y3;K\"-\"\nC;X53;Y3;K\"normal\"\nC;X54;Y3;K\"_\"\nC;X55;Y3;K\"-\"\nC;X56;Y3;K\"-\"\nC;X57;Y3;K\"-\"\nC;X58;Y3;K\"-\"\nC;X59;Y3;K\"-\"\nC;X60;Y3;K\"-\"\nC;X61;Y3;K\" - \"\nC;X62;Y3;K\"-\"\nC;X63;Y3;K\" - \"\nC;X64;Y3;K\"-\"\nC;X65;Y3;K\"-\"\nC;X66;Y3;K\"-\"\nC;X67;Y3;K\"-\"\nC;X68;Y3;K\"-\"\nC;X69;Y3;K\"-\"\nC;X70;Y3;K\"-\"\nC;X71;Y3;K\"_\"\nC;X72;Y3;K\"1\"\nC;X73;Y3;K\"0\"\nC;X74;Y3;K\"0\"\nC;X75;Y3;K\"0\"\nC;X76;Y3;K\"1\"\nE\n"
};
typedef struct {
    mapInfo_t info;
    mapInfo_t const *old_info;
    slkTestData_t *canonical_rows[5],*saved_rows[5];
    slkTestData_t *rows,*old_rows;
    war3map_t terrain;
    war3mapVertex_t vertices[17*17];
    uint8_t cells[64*64];
    edict_t *unit,*gate;
} target377Fixture_t;
static void Target377Begin(target377Fixture_t *f,bool active) {
    reset_entities();setup_test_world();memset(f,0,sizeof(*f));
    f->old_info=level.mapinfo;
    /* Canonical nwgt rows preserve its real footprint, immobility, collision,
     * classification, weapon gating and Awrp/Avul ownership. Only model art
     * uses the test asset; hfoo repulse fields match the captured W3U. */
    FOR_LOOP(i,5) {
        f->canonical_rows[i]=parse_slk_string(target377_canonical_slk[i]);
        f->saved_rows[i]=G_SetSLKRows(target377_canonical_tables[i],f->canonical_rows[i]);
        T_NOT_NULL(f->saved_rows[i]);
    }
    f->info.players[0].used=true;f->info.players[0].playerType=kPlayerTypeHuman;
    level.mapinfo=&f->info;G_SetMapUnitOverrides(&f->info);G_SetMapAbilityOverrides(&f->info);
    game.clients[0].mapplayer=f->info.players;game.clients[0].ps.rdflags|=RDF_NOFOG|RDF_NOFOGMASK;
    f->rows=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"levels\"\nC;X4;K\"DataA1\"\nC;X5;K\"DataB1\"\nC;X1;Y2;K\"Awrp\"\nC;X2;K\"Awrp\"\nC;X3;K1\nC;X4;K400\nC;X5;K400\nE\n");
    f->old_rows=G_SetSLKRows("AbilityData",f->rows);
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,f->cells);
    FOR_LOOP(i,17*17){f->vertices[i].accurate_height=0x2000;f->vertices[i].level=2;}
    f->terrain=(war3map_t){.width=17,.height=17,.center={0,0},.vertices=f->vertices};world.map=&f->terrain;
    gi.ClearWorld();level.time=level.pathing_msec=level.pathing_phase=level.framenum=0;
    level.started=level.scriptsConfigured=level.scriptsStarted=false;
    level.pathing_due=level.pathing_owner_clock_valid=level.timer_clock_valid=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    f->gate=unit_create(0,MAKEFOURCC('n','w','g','t'),&(vec2_t){768,256},0);
    f->unit=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){272,304},0);
    T_NOT_NULL(f->gate);T_NOT_NULL(f->unit);
    if(!f->gate || !f->unit)return;
    f->unit->runtime.acquisition_range=0;
    T_EQ(f->unit->class_id,MAKEFOURCC('h','f','o','o'));
    T_EQ(f->gate->class_id,MAKEFOURCC('n','w','g','t'));T_ASSERT(G_UnitIsStructure(f->gate));
    T_ASSERT(G_ActorHasAbilityCode(f->gate,MAKEFOURCC('A','w','r','p')));
    T_EQ(G_UnitAbilityLevel(f->gate,MAKEFOURCC('A','w','r','p')),1u);
    T_ASSERT(S_UnitCanTranslate(f->unit));T_ASSERT(G_ActorHasAbilityCode(f->unit,MAKEFOURCC('A','m','o','v')));
    T_FEQ(S_UnitMoveSpeed(f->unit),270,0);T_FEQ(S_MoveCollisionRadius(f->unit),31,0);
    T_FEQ(S_MoveCollisionRadius(f->gate),50,0);T_ASSERT(!S_UnitCanTranslate(f->gate));
    T_STREQ(f->gate->data.UnitData->pathingTexture,"PathTextures\\Waygate.tga");
    T_NOT_NULL(f->gate->pathtex);
    if(f->gate->pathtex){T_EQ(f->gate->pathtex->width,20);T_EQ(f->gate->pathtex->height,20);}
    T_FEQ(f->gate->s.origin2.x,768,0);T_FEQ(f->gate->s.origin2.y,256,0);
    /* Retail input1536/304 resolves to these edge-center words. Supplying the
     * resolved state avoids certifying the independent setter quantization. */
    S_WaygateSetDestination(f->gate,&(vec2_t){1568,288});S_WaygateSetActive(f->gate,active);
    T_EQ(S_WaygateIsActive(f->gate),active);
    G_FinishMovePathingInitialization();level.started=level.scriptsConfigured=level.scriptsStarted=true;
}
static void Target377End(target377Fixture_t *f) {
    level.started=false;reset_entities();setup_test_world();
    G_SetMapUnitOverrides(NULL);G_SetMapAbilityOverrides(NULL);level.mapinfo=f->old_info;
    game.clients[0].mapplayer=f->old_info->players;
    G_SetSLKRows("AbilityData",f->old_rows);free_slk_rows(f->rows);
    FOR_LOOP(i,5) {
        slkTestData_t *released=G_SetSLKRows(target377_canonical_tables[i],f->saved_rows[i]);
        free_slk_rows(released);free_slk_rows(f->canonical_rows[i]);free_slk_rows(f->saved_rows[i]);
    }
}
static void Target377Step(void) {level.time+=5;globals.RunFrame();}
static void Target377Restore(target377Fixture_t *f,cstring_t name) {
    uint32_t unit=f->unit-g_edicts,gate=f->gate-g_edicts;
    cstring_t path=Test_TempPath(name);T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
    f->unit=g_edicts+unit;f->gate=g_edicts+gate;
}
static void Target377PointTask(edict_t *unit,vec2_t expected) {
    /* Native produces d016b POINT tasks, not target d0174/Follow or a Warp
     * ability-owned walker. No extra public user command is manufactured. */
    T_ASSERT(unit->currentmove==&move_move_walk);
    moveGroup_t const *group=move_unit_group(unit);T_NOT_NULL(group);
    if(group){T_NULL(group->target);T_EQ(wc3_float_bits(group->members[0].arrival_range),0x3efae148u);}
    T_NOT_NULL(unit->goalentity);
    if(unit->goalentity){T_FEQ(unit->goalentity->s.origin2.x,expected.x,0);T_FEQ(unit->goalentity->s.origin2.y,expected.y,0);}
    T_EQ(G_UnitQueuedOrderCount(unit),0u);
}
TEST(wc3_movement,target377_active_gate_keeps_cached_exit_point_task_through_retarget_and_save) {
    FOR_LOOP(smart,2)FOR_LOOP(saved,2) {
        target377Fixture_t f;Target377Begin(&f,true);
        if(!f.unit || !f.gate){Target377End(&f);continue;}
        cstring_t command=smart ? "smart" : "move";
        T_ASSERT(G_IssueUnitTargetOrder(f.unit,command,f.gate,false,0));
        T_EQ(f.unit->current_order_id,G_OrderId(command));Target377PointTask(f.unit,(vec2_t){768,256});
        while(level.time<500)Target377Step();
        T_ASSERT(f.unit->s.origin2.x<600); /* Retarget while the entrance task remains active. */
        S_WaygateSetDestination(f.gate,&(vec2_t){1568,800});
        if(saved)Target377Restore(&f,"target377-before-entry.bin");
        bool warped=false;vec2_t previous=f.unit->s.origin2;
        while(level.time<10000) {
            Target377Step();vec2_t now=f.unit->s.origin2;
            if(!warped && fabsf(now.x-previous.x)>400) {
                warped=true;
                /* A zero-range entrance task completes near the entrance;
                 * legacy Smart must not jump at the broad rectangle edge. */
                T_ASSERT(Vector2_distance(&previous,&(vec2_t){768,256})<=32*wc3_float(0x3efae148u));
                T_ASSERT(now.x>1400 && now.y>700); /* The live, retargeted exit. */
                T_EQ(f.unit->current_order_id,G_OrderId(command));
                Target377PointTask(f.unit,(vec2_t){1568,288});
                if(saved)Target377Restore(&f,"target377-after-entry.bin");
            }
            previous=f.unit->s.origin2;
        }
        T_ASSERT(warped);T_EQ(f.unit->current_order_id,0u);T_EQ(G_UnitQueuedOrderCount(f.unit),0u);
        /* These separate the cached exit from the512-world-unit retarget.
         * Exact numeric/trajectory parity belongs to the complete row oracle. */
        T_ASSERT(Vector2_distance(&f.unit->s.origin2,&(vec2_t){1568,288})<=32*wc3_float(0x3efae148u));
        Target377End(&f);
    }
}
TEST(wc3_movement,target377_inactive_gate_move_is_one_entrance_point_without_warp) {
    FOR_LOOP(saved,2) {
        target377Fixture_t f;Target377Begin(&f,false);
        if(!f.unit || !f.gate){Target377End(&f);continue;}
        T_ASSERT(G_IssueUnitTargetOrder(f.unit,"move",f.gate,false,0));Target377PointTask(f.unit,(vec2_t){768,256});
        if(saved)Target377Restore(&f,"target377-inactive.bin");
        bool warped=false;vec2_t previous=f.unit->s.origin2;
        while(level.time<10000){Target377Step();vec2_t now=f.unit->s.origin2;warped|=fabsf(now.x-previous.x)>400;previous=now;}
        T_ASSERT(!warped);T_EQ(f.unit->current_order_id,0u);T_EQ(G_UnitQueuedOrderCount(f.unit),0u);
        T_ASSERT(Vector2_distance(&f.unit->s.origin2,&(vec2_t){768,256})<=32*wc3_float(0x3efae148u));
        Target377End(&f);
    }
}
#endif
#ifdef BZ_TESTS
/* Engine callback lifetime protection, not another public retail capture.
 * The existing ability replacement facility observes the real A_MOVE_START
 * dispatch made by ordinary Move startup. All replacement orders use public
 * production admission; the observer never writes movement state directly. */
static abilityProc_t target377_original_move;
static edict_t *target377_callback_source,*target377_replacement;
static unsigned target377_callback_action,target377_callback_count;
static bool target377_second_task;
static umove_t *target377_replacement_move;
static intptr_t Target377ReentrantMove(edict_t *unit,abilityMsg_t msg,abilityCall_t const *call) {
    if(msg==A_MOVE_START && unit==target377_callback_source && call && call->move_target &&
       (!target377_second_task ||
        (call->move_target->s.origin2.x==1568 && call->move_target->s.origin2.y==288))) {
        target377_callback_source=NULL;target377_callback_count++;
        if(target377_callback_action==0)
            T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){368,656},false,0,0));
        else if(target377_callback_action==1)
            T_ASSERT(unit_issueimmediateorder(unit,"stop"));
        else {
            vec2_t position=unit->s.origin2;
            G_FreeEdict(unit);
            if(target377_callback_action==3) {
                level.time+=2000; /* Respect the actual allocator quarantine. */
                target377_replacement=unit_create(0,MAKEFOURCC('h','f','o','o'),&position,0);
                T_NOT_NULL(target377_replacement);
                if(target377_replacement) {
                    target377_replacement->user_data=377;
                    target377_replacement_move=target377_replacement->currentmove;
                }
            }
            return true;
        }
    }
    return target377_original_move(unit,msg,call);
}
TEST(wc3_movement,target377_point_task_start_keeps_nested_orders_and_incarnation) {
    FOR_LOOP(second,2)FOR_LOOP(saved,2)FOR_LOOP(action,4) {
        target377Fixture_t f;Target377Begin(&f,true);
        if(!f.unit || !f.gate){Target377End(&f);continue;}
        if(second)T_ASSERT(G_IssueUnitTargetOrder(f.unit,"move",f.gate,false,0));
        if(saved)Target377Restore(&f,"target377-reentrant-start.bin");
        ability_t const *owner=FindAbilityByClassname("Awan");T_NOT_NULL(owner);
        if(!owner){Target377End(&f);continue;}
        T_ASSERT(owner->flags&AB_INNATE);
        target377_original_move=owner->proc;target377_callback_source=f.unit;
        target377_replacement=NULL;target377_replacement_move=NULL;
        target377_callback_action=action;target377_callback_count=0;target377_second_task=second;
        uint32_t birth=f.unit->spawn_time;
        S_ReplaceAbilityProcedure(owner,Target377ReentrantMove);
        if(!second)G_IssueUnitTargetOrder(f.unit,"move",f.gate,false,0);
        while(second && !target377_callback_count && level.time<5000)Target377Step();
        S_ReplaceAbilityProcedure(owner,target377_original_move);
        target377_callback_source=NULL;
        T_EQ(target377_callback_count,1u);
        if(action==0) {
            T_EQ(f.unit->current_order_id,G_OrderId("move"));T_NOT_NULL(f.unit->goalentity);
            if(f.unit->goalentity){T_FEQ(f.unit->goalentity->s.origin2.x,368,0);T_FEQ(f.unit->goalentity->s.origin2.y,656,0);}
            moveGroup_t const *group=move_unit_group(f.unit);T_NOT_NULL(group);
            T_ASSERT(!f.unit->movement.point_continuation);
        } else if(action==1) {
            T_EQ(f.unit->current_order_id,0u);
            T_ASSERT(!f.unit->movement.point_continuation);
        } else if(action==2) {
            T_ASSERT(!f.unit->inuse);T_NULL(f.unit->currentmove);T_NULL(f.unit->goalentity);
        } else {
            T_ASSERT(target377_replacement==f.unit);
            T_ASSERT(f.unit->inuse);T_ASSERT(f.unit->spawn_time!=birth);
            T_EQ(f.unit->user_data,377);T_EQ(f.unit->current_order_id,0u);
            T_ASSERT(f.unit->currentmove==target377_replacement_move);T_NULL(f.unit->goalentity);
        }
        Target377End(&f);
    }
}
#endif

#ifdef BZ_TESTS
/* The exported target-owned dispatcher must not retain the superseded Warp
 * walker beside the public Move/Smart producer. This is an engine route
 * consistency check against the accepted native point-task contract. */
TEST(wc3_movement,target377_target_owned_dispatch_uses_the_same_point_producer) {
    FOR_LOOP(active,2)FOR_LOOP(saved,2) {
        target377Fixture_t f;Target377Begin(&f,active);
        if(!f.unit || !f.gate){Target377End(&f);continue;}
        T_ASSERT(S_UnitTargetAbilityOrder(f.gate,f.unit,"smart"));
        T_EQ(f.unit->current_order_id,G_OrderId("smart"));
        Target377PointTask(f.unit,(vec2_t){768,256});
        moveGroup_t const *group=move_unit_group(f.unit);
        if(group){T_EQ(f.unit->movement.point_continuation,(bool)active);
            if(active){T_FEQ(f.unit->movement.next_point.x,1568,0);T_FEQ(f.unit->movement.next_point.y,288,0);}}
        T_NULL(f.unit->movement.waygate_target);T_NULL(f.unit->movement.waygate_goal);
        T_EQ(f.unit->movement.waygate_target_spawn_time,0u);
        if(saved){Target377Restore(&f,"target377-target-owned.bin");
            Target377PointTask(f.unit,(vec2_t){768,256});}
        Target377End(&f);
    }
}
#endif

#ifdef BZ_TESTS
/* Native377 selected history is shared while each unit retains its own task
 * stack. This composition deliberately gives the two owners different cached
 * exits before the later queued entrance producer inherits its peer cohort. */
TEST(wc3_movement,target377_shared_cohort_keeps_independent_cached_exits_through_queued_activation_and_save) {
    FOR_LOOP(saved,2) {
        target377Fixture_t f;Target377Begin(&f,true);
        if(!f.unit || !f.gate){Target377End(&f);continue;}
        S_SetUnitAxisPosition(f.gate,1,1056);
        vec2_t const entrance=f.gate->s.origin2;
        S_WaygateSetDestination(f.gate,&(vec2_t){544,1568});
        edict_t *peer=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){272,400},0);
        T_NOT_NULL(peer);
        if(!peer){Target377End(&f);continue;}
        peer->runtime.acquisition_range=0;
        S_SetUnitMoveSpeed(f.unit,150);S_SetUnitMoveSpeed(peer,270);
        T_FEQ(S_UnitMoveSpeed(f.unit),150,0);T_FEQ(S_UnitMoveSpeed(peer),270,0);
        groupPointOrder_t request={.count=2,.order="move",.order_id=G_OrderId("move"),.point=&entrance,.issuer_player=0};
        request.units[0]=(typeof(request.units[0])){f.unit,f.unit->spawn_time};
        request.units[1]=(typeof(request.units[0])){peer,peer->spawn_time};
        T_ASSERT(G_IssueGroupPointOrder(&request));
        uint64_t history=f.unit->movement.previous_request_id;
        T_NE(history,0);T_EQ(peer->movement.previous_request_id,history);
        moveGroup_t *initial=move_unit_group(f.unit);T_NOT_NULL(initial);
        T_EQ(move_unit_group(peer),initial);
        T_ASSERT(G_IssueUnitTargetOrder(peer,"move",f.gate,true,0));
        T_EQ(G_UnitQueuedOrderCount(peer),1u);
        T_ASSERT(G_IssueUnitTargetOrder(f.unit,"move",f.gate,false,0));
        T_EQ(f.unit->movement.previous_request_id,history);
        T_EQ(peer->movement.previous_request_id,history);
        /* The first unassociated d016b task also performs the common search. */
        T_EQ(move_unit_group(f.unit),move_unit_group(peer));
        S_WaygateSetDestination(f.gate,&(vec2_t){1056,1568});
        uint32_t peer_number=peer-g_edicts;
        if(saved) {
            Target377Restore(&f,"target377-cohort-before-join.bin");
            peer=g_edicts+peer_number;
            T_EQ(f.unit->movement.previous_request_id,history);
            T_EQ(peer->movement.previous_request_id,history);
        }
        bool joined=false,first_cached=false,peer_cached=false;
        while(level.time<12000) {
            Target377Step();
            if(!joined && G_UnitQueuedOrderCount(peer)==0 && peer->current_order_id==G_OrderId("move")) {
                joined=true;
                T_EQ(move_unit_group(f.unit),move_unit_group(peer));
                T_NOT_NULL(move_unit_group(f.unit));
                T_NOT_NULL(f.unit->goalentity);T_NOT_NULL(peer->goalentity);
                if(f.unit->goalentity){T_FEQ(f.unit->goalentity->s.origin2.x,entrance.x,0);T_FEQ(f.unit->goalentity->s.origin2.y,entrance.y,0);}
                if(saved) {
                    Target377Restore(&f,"target377-cohort-after-join.bin");
                    peer=g_edicts+peer_number;
                    T_EQ(move_unit_group(f.unit),move_unit_group(peer));
                }
            }
            if(f.unit->goalentity && f.unit->currentmove==&move_move_walk &&
               f.unit->goalentity->s.origin2.x==544 && f.unit->goalentity->s.origin2.y==1568)first_cached=true;
            if(peer->goalentity && peer->currentmove==&move_move_walk &&
               peer->goalentity->s.origin2.x==1056 && peer->goalentity->s.origin2.y==1568)peer_cached=true;
        }
        T_ASSERT(joined);T_ASSERT(first_cached);T_ASSERT(peer_cached);
        T_EQ(f.unit->current_order_id,0u);T_EQ(peer->current_order_id,0u);
        T_EQ(G_UnitQueuedOrderCount(f.unit),0u);T_EQ(G_UnitQueuedOrderCount(peer),0u);
        T_ASSERT(Vector2_distance(&f.unit->s.origin2,&(vec2_t){544,1568})<=32*wc3_float(0x3efae148u));
        T_ASSERT(Vector2_distance(&peer->s.origin2,&(vec2_t){1056,1568})<=32*wc3_float(0x3efae148u));
        Target377End(&f);
    }
}
#endif
#ifdef BZ_TESTS
/* 5fa950 requires a valid retained request identity. Independent script
 * commands with no shared history cannot acquire another unit's cohort. */
TEST(wc3_movement,target377_independent_gate_tasks_without_shared_history_keep_separate_owners) {
    FOR_LOOP(saved,2) {
        target377Fixture_t f;Target377Begin(&f,true);
        if(!f.unit || !f.gate){Target377End(&f);continue;}
        edict_t *peer=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){272,400},0);
        T_NOT_NULL(peer);if(!peer){Target377End(&f);continue;}
        peer->runtime.acquisition_range=0;
        T_EQ(f.unit->movement.previous_request_id,0u);T_EQ(peer->movement.previous_request_id,0u);
        T_ASSERT(G_IssueUnitTargetOrder(f.unit,"move",f.gate,false,0));
        T_ASSERT(G_IssueUnitTargetOrder(peer,"move",f.gate,false,0));
        T_NOT_NULL(move_unit_group(f.unit));T_NOT_NULL(move_unit_group(peer));
        T_NE(move_unit_group(f.unit),move_unit_group(peer));
        uint32_t peer_number=peer-g_edicts;
        if(saved){Target377Restore(&f,"target377-independent-tasks.bin");peer=g_edicts+peer_number;
            T_NE(move_unit_group(f.unit),move_unit_group(peer));}
        T_ASSERT(f.unit->movement.point_continuation);T_ASSERT(peer->movement.point_continuation);
        T_ASSERT(unit_issueimmediateorder(f.unit,"stop"));
        T_ASSERT(!f.unit->movement.point_continuation);T_ASSERT(peer->movement.point_continuation);
        T_NOT_NULL(move_unit_group(peer));T_EQ(peer->current_order_id,G_OrderId("move"));
        Target377End(&f);
    }
}
#endif
