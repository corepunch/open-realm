/*
 * g_unitrow.h — Typed DDX row structs for WC3 SLK tables.
 *
 * C field names are semantic gameplay names, not raw archive column labels.
 * Raw labels stay in g_metadata.c DDX schemas as column strings.
 *
 * ROC-only columns are absent in TFT archives and remain zero-initialized;
 * TFT-only columns are absent in ROC archives and likewise remain zero.
 * Fields that moved tables between ROC and TFT have explicit accessors in
 * g_metadata.c so callers do not need macro fallbacks.
 *
 * Access through G_Unit* helpers; unknown IDs resolve to a static zero row.
 */
#ifndef g_unitrow_h
#define g_unitrow_h

#include "common/shared.h"

/* =========================================================================
 * Unit Profile / INI
 * =========================================================================*/
typedef struct {
    uint32_t id;
    cstring_t name, properNames, builds, trains;
    cstring_t animProps, art, itemArt, attachmentAnimProps, attachmentLinkProps, awakenTip;
    cstring_t boneProps, buildingSoundLabel, buttonPosX, buttonPosY;
    cstring_t casterUpgradeArt, casterUpgradeName, casterUpgradeTip, dependencyOr;
    cstring_t description, editorSuffix, hotkey, loopingSoundFadeIn, loopingSoundFadeOut;
    cstring_t makeItems, movementSoundLabel, randomSoundLabel;
    cstring_t requiresCount, requires, requiresLevel[8], requiresAmount;
    cstring_t researches, revive, reviveTip, scoreScreenIcon, sellItems, sellUnits;
    cstring_t specialArt, targetArt, tip, reviveAt, uberTip, upgrade;
    struct { cstring_t art; float arc, speed; bool homing; } attack[2];
} UnitProfile_t;

/* =========================================================================
 * UnitBalance.slk
 * =========================================================================*/
typedef struct {
    uint32_t   id;
    cstring_t  sortBalance, sort2, comments;
    bool    abilTest, InBeta;
    /* economy ---------------------------------------------------------------*/
    int32_t    level;
    int32_t    goldCost, lumberCost;
    int32_t    goldRep, lumberRep;
    int32_t    foodMade, foodUsed;
    /* bounty ----------------------------------------------------------------*/
    int32_t    goldBountyDice, goldBountySides, goldBountyBase;
    int32_t    lumberBountyDice, lumberBountySides, lumberBountyBase;
    /* stock -----------------------------------------------------------------*/
    int32_t    stockMax, stockRegen, stockStart;
    /* HP / mana -------------------------------------------------------------*/
    int32_t    baseHealth;
    float   maxHealth;
    float   healthRegen;
    cstring_t  healthRegenType;
    int32_t    baseMana;
    float   maxMana;
    float   initialMana;
    float   manaRegen;
    /* armor -----------------------------------------------------------------*/
    int32_t    baseArmor;
    int32_t    armorPerUpgrade;
    float   armor;
    cstring_t  defenseType;
    /* movement --------------------------------------------------------------*/
    float   speed;
    float   maxSpeed, minSpeed;
    int32_t    buildTime;
    float   sightRadius, nightSightRadius;
    /* hero attributes -------------------------------------------------------*/
    int32_t    strength, intelligence, agility;
    float   strengthPerLevel, intelligencePerLevel, agilityPerLevel;
    cstring_t  primaryAttribute;
    cstring_t  upgrades;
    /* TFT-only fields -------------------------------------------------------*/
    float   collision;               /* ROC: stored in UnitData.slk        */
    bool    isBuilding;              /* ROC: stored in UnitUI.slk          */
    int32_t    nbrandom;
    cstring_t  preventPlace;
    int32_t    reptm;
    int32_t    repulse;
    int32_t    repulseGroup;
    float   repulseParam;
    int32_t    repulsePrio;
    cstring_t  requirePlace;
    cstring_t  tilesets;
    cstring_t  type;                    /* unit type string                   */
} UnitBalance_t;

/* =========================================================================
 * UpgradeData.slk
 * =========================================================================*/
typedef struct {
    uint32_t  id;
    cstring_t comments;
    cstring_t upgradeClass;
    cstring_t race;
    int32_t   flag;
    bool   used;
    int32_t   maxLevel;
    bool   inherit;
    int32_t   goldBase, goldMod;
    int32_t   lumberBase, lumberMod;
    int32_t   timeBase, timeMod;
    uint32_t  effect[4];
    float  effectBase[4];
    float  effectMod[4];
    uint32_t  effectCode[4];
} UpgradeData_t;

/* =========================================================================
 * UnitData.slk
 * =========================================================================*/
typedef struct {
    uint32_t   id;
    cstring_t  sort, comments;
    int32_t    version;
    bool    valid, InBeta;
    /* buffs / targeting -----------------------------------------------------*/
    float   buffRadius;
    cstring_t  buffType;
    cstring_t  targetType;
    /* behaviour -------------------------------------------------------------*/
    bool    canSleep;
    bool    canFlee;                 /* TFT-only                           */
    bool    canBuildOn;              /* TFT-only                           */
    bool    isBuildOn;               /* TFT-only                           */
    int32_t    cargoSize;
    /* death -----------------------------------------------------------------*/
    float   death;                   /* authored death animation time      */
    int32_t    deathType;               /* bit 0 raise, bit 1 decay            */
    /* line-of-sight / formation ---------------------------------------------*/
    bool    useExtendedLineOfSight;
    int32_t    formationRank;
    /* movement --------------------------------------------------------------*/
    float   moveFloor, moveHeight;
    cstring_t  moveTypeName;            /* "foot"/"fly"/"hover"/"float"/"amph"/"horse" */
    float   turnRate;
    /* misc ------------------------------------------------------------------*/
    int32_t    nameCount;
    int32_t    orientationInterpolation;
    cstring_t  pathingTexture;
    int32_t    points;
    int32_t    priority;
    float   propWin;
    cstring_t  race;
    float   requireWaterRadius;      /* TFT-only                           */
    int32_t    threat;
    /* ROC-only columns (moved to UnitWeapons in TFT) ------------------------*/
    float   castBackSwing;           /* ROC: cast back-swing; TFT: UnitWeapons */
    float   castPoint;               /* ROC: cast point;      TFT: UnitWeapons */
    float   collision;               /* ROC: collision radius; TFT: UnitBalance */
    float   impactHeight;            /* ROC: impact Z offset;  TFT: UnitWeapons */
    float   launchOffsetX, launchOffsetY, launchOffsetZ; /* ROC: launch offsets; TFT: UnitWeapons */
    cstring_t  unitClassification;      /* ROC: unit type string; TFT: UnitBalance */
} UnitData_t;

/* =========================================================================
 * UnitUI.slk
 * =========================================================================*/
typedef struct {
    uint32_t   id;
    cstring_t  sortUI;
    bool    InBeta;
    /* model / art -----------------------------------------------------------*/
    cstring_t  modelFile;               /* MDX model path ("umdl")            */
    float   modelScale;
    float   blend;
    /* team colour / tinting -------------------------------------------------*/
    int32_t    tintRed, tintGreen, tintBlue;
    int32_t    teamColor;
    bool    customTeamColor;
    bool    hostilePal;
    /* selection / shadow ----------------------------------------------------*/
    float   selectionScale;          /* selection circle scale             */
    float   selectionCircleHeight;
    float   shadowHeight, shadowWidth, shadowCenterX, shadowCenterY;
    cstring_t  unitShadowTexture;
    bool    selectionCircleOnWater;  /* TFT-only                           */
    bool    waterShadow;             /* TFT-only                           */
    bool    scaleProjectiles;        /* scale projectiles                  */
    /* elevation -------------------------------------------------------------*/
    int32_t    elevationSamplePoints;
    float   elevationSampleRadius;
    float   fogOfWarSampleRadius;
    float   occluderHeight;
    /* animation -------------------------------------------------------------*/
    float   animationRunSpeed, animationWalkSpeed;
    float   maxPitchDegrees, maxRollDegrees;
    /* sound -----------------------------------------------------------------*/
    cstring_t  soundLabel;
    /* strings ---------------------------------------------------------------*/
    cstring_t  name;
    cstring_t  groundTexture;
    cstring_t  buildingShadowTexture;
    cstring_t  special;
    /* flags -----------------------------------------------------------------*/
    cstring_t  armorSoundType;          /* authored armor token: Flesh/Metal/Wood/... */
    int32_t    armorType;               /* normalized armor type index for info panel/sounds */
    int32_t    unitClass;
    bool    neutralBuildingMinimapIcon; /* neutral building minimap icon   */
    bool    inEditor;
    bool    hiddenInEditor;
    bool    dropItems;
    bool    useClickHelper;
    bool    campaign;                /* TFT-only                           */
    int32_t    fileVerFlags;            /* TFT-only                           */
    bool    hideHeroBar;             /* TFT-only                           */
    bool    hideHeroDeathMsg;        /* TFT-only                           */
    bool    hideHeroMinimap;         /* TFT-only                           */
    bool    hideOnMinimap;           /* TFT-only                           */
    bool    tilesetSpecific;
    cstring_t  requirePlace, preventPlace, tilesets; /* ROC-only */
    int32_t    nbrandom;                             /* ROC-only */
    /* weapon slots (index into UnitWeapons) ---------------------------------*/
    int32_t    weaponSlot1, weaponSlot2;
    /* ROC-only fields (moved to UnitBalance in TFT) -------------------------*/
    bool    isBuilding;              /* ROC: is building; TFT: UnitBalance */
} UnitUI_t;

/* =========================================================================
 * UnitWeapons.slk
 * =========================================================================*/
typedef struct {
    cstring_t attackType, rangeTest, damageUpgrade, damageModifier, weaponType, weaponSound;
    int32_t damageDice, damageSides, damageBase, damageUpgradeAmount;
    int32_t areaTargets, maxTargets, targetsAllowed;
    cstring_t targetsAllowedText; /* UnitWeapons.slk targsN is a comma-separated target list */
    float damagePoint, backswingPoint, cooldown;
    float minCooldown, minDamage, averageDamage, maxDamage, damagePerSecond;
    float damageLossFactor, range, rangeBuffer;
    float areaFull, areaMedium, areaSmall, factorMedium, factorSmall;
    float spillDistance, spillRadius;
    bool showUI;
} UnitWeapon_t;

typedef struct {
    uint32_t   id;
    cstring_t  sortWeap, sort2, comments;
    bool    InBeta;
    float   acquisitionRange;        /* auto-attack trigger range          */
    UnitWeapon_t attack1, attack2;
    /* shared / misc ---------------------------------------------------------*/
    int32_t    attacksEnabled;
    float   minimumAttackRange;
    /* TFT-only columns (moved from UnitData in ROC) -------------------------*/
    float   castBackSwing;           /* ROC: stored in UnitData.slk        */
    float   castPoint;               /* ROC: stored in UnitData.slk        */
    float   impactSwimZ;             /* TFT-only                           */
    float   impactHeight;            /* ROC: stored in UnitData.slk        */
    float   launchSwimZ;             /* TFT-only                           */
    float   attackLaunchX, attackLaunchY, attackLaunchZ; /* ROC: stored in UnitData.slk */
} UnitWeapons_t;

/* =========================================================================
 * UnitAbilities.slk
 * =========================================================================*/
typedef struct {
    uint32_t   id;
    cstring_t  sortAbil, comments;
    cstring_t  abilList;                /* comma-separated ability codes      */
    cstring_t  heroAbilList;            /* hero abilities                     */
    bool    auto_, InBeta;            /* auto is a keyword, use auto_       */
} UnitAbilities_t;

/* =========================================================================
 * AbilityData.slk
 * =========================================================================*/
typedef struct {
    float number;
    uint32_t id;
} abilityDataValue_t;

typedef struct {
    cstring_t targs;
    float cast, dur, heroDur, cool, cost, area, range;
    abilityDataValue_t data[9];
    uint32_t unitID;
    cstring_t buffID, efctID;
} abilityLevel_t;

typedef struct {
    uint32_t id, code, uberAlias;
    cstring_t comments, sort, race;
    int32_t version, levels, reqLevel, levelSkip, priority;
    bool useInEditor, hero, item, checkDep, InBeta;
    abilityLevel_t level[4];
    cstring_t castCheck, durCheck, heroDurCheck, coolCheck, costCheck, areaCheck, rangeCheck;
} AbilityData_t;

/* =========================================================================
 * AbilityBuffData.slk
 * =========================================================================*/
typedef struct {
    uint32_t id, code;
    cstring_t comments, sort, race;
    int32_t version;
    bool isEffect, useInEditor, InBeta;
    cstring_t buffArt, buffTip, buffUberTip;
    cstring_t targetArt, specialArt, effectArt, missileArt;
    cstring_t effectSound, effectSoundLooped;
} AbilityBuffData_t;

/* =========================================================================
 * Doodads.slk
 * =========================================================================*/
typedef struct {
    uint32_t id;
    cstring_t category, tilesets, dir, file, comment, Name, doodClass, soundLoop, shadow, pathTex, UserList;
    bool tilesetSpecific, canPlaceRandScale, useClickHelper, ignoreModelClick;
    bool walkable, onCliffs, onWater, floats, showInFog, animInFog, showInMM, useMMColor, InBeta;
    float selSize, defScale, minScale, maxScale, maxPitch, maxRoll, visRadius, fixedRot;
    int32_t numVar, MMRed, MMGreen, MMBlue, version;
    int32_t vert[10][3];
} Doodads_t;

/* =========================================================================
 * UberSplatData.slk
 * =========================================================================*/
typedef struct {
    uint32_t id;
    cstring_t Name, comment, Dir, file, BlendMode, Sound;
    float Scale, BirthTime, PauseTime, Decay;
    float StartR, StartG, StartB, StartA;
    float MiddleR, MiddleG, MiddleB, MiddleA;
    float EndR, EndG, EndB, EndA;
    int32_t version;
    bool InBeta;
} UberSplatData_t;

/* =========================================================================
 * UnitAckSounds.slk
 * =========================================================================*/
typedef struct {
    uint32_t key;
    cstring_t name, FileNames, DirectoryBase, Channel, Flags, EAXFlags;
    float Volume, Pitch, PitchVariance, Priority, MinDistance, MaxDistance, DistanceCutoff;
    float InsideAngle, OutsideAngle, OutsideVolume, OrientationX, OrientationY, OrientationZ;
    int32_t version;
    bool InBeta;
} UnitAckSounds_t;

/* UI\SoundInfo\Music.slk only needs the row key and authored file list for
 * playlist expansion.  Keep this separate from one-shot sound metadata. */
typedef struct {
    cstring_t name;
    cstring_t FileNames;
} MusicData_t;

/* =========================================================================
 * ItemData.slk
 * =========================================================================*/
typedef struct {
    uint32_t   id;
    cstring_t  scriptname;
    int32_t    version;
    bool    InBeta;
    cstring_t  displayName;             /* display name (column "comment")    */
    cstring_t  file;                    /* MDX model path                     */
    cstring_t  abilList;
    cstring_t  itemClass;               /* TFT: item class; ROC: itemClass    */
    int32_t    armor;
    int32_t    goldcost, lumbercost;
    int32_t    HP;
    int32_t    level;
    int32_t    prio;
    int32_t    stockMax, stockRegen, stockStart;
    int32_t    uses;
    float   scale;                   /* TFT-only                           */
    float   selectionSize;           /* TFT-only                           */
    bool    droppable, drop;
    bool    perishable;
    bool    sellable;                /* TFT-only                           */
    bool    pawnable;                /* TFT-only                           */
    bool    usable;
    bool    pickRandom;              /* TFT-only                           */
    bool    powerup;                 /* TFT-only                           */
    bool    morph;                   /* TFT-only                           */
    bool    ignoreCD;                /* TFT-only                           */
    cstring_t  cooldownID;              /* TFT-only                           */
    int32_t    oldLevel;                /* TFT-only                           */
    int32_t    colorR, colorG, colorB;  /* TFT-only                           */
} ItemData_t;

/* =========================================================================
 * DestructableData.slk
 * =========================================================================*/
typedef struct {
    uint32_t   id;
    cstring_t  category, tilesets, comment, doodClass;
    int32_t    version;
    bool    InBeta;
    cstring_t  file;                    /* MDX model path                     */
    cstring_t  dir;                     /* subdirectory prefix (ROC-only)     */
    cstring_t  displayName;             /* TFT (ROC used "name" → lowercase)  */
    cstring_t  EditorSuffix;            /* TFT-only                           */
    cstring_t  textureFile;
    cstring_t  texID;
    cstring_t  shadow;
    cstring_t  pathingTexture;
    cstring_t  deathPathingTexture;
    cstring_t  deathSnd;
    cstring_t  targetType;
    cstring_t  portraitmodel;           /* TFT-only                           */
    cstring_t  UserList;                /* TFT-only                           */
    int32_t    maxHealth;
    cstring_t  armorSoundType;          /* authored armor token: Flesh/Metal/Wood/... */
    int32_t    armor;                    /* normalized armor type index */
    int32_t    numVar;
    float   selSize;
    float   minScale, maxScale;
    float   maxPitch, maxRoll;
    float   radius;
    float   fogRadius;
    float   occluderHeight, flyHeight;
    float   cliffHeight;
    bool    fogVisible;
    bool    fatLOS;
    bool    walkable;
    bool    onCliffs, onWater;
    bool    canPlaceDead;
    bool    canPlaceRandScale;
    bool    lightweight;
    bool    tilesetSpecific;
    bool    useClickHelper;
    bool    showInMM;
    bool    useMMColor;
    bool    fixedRot;
    bool    selectable;              /* TFT-only                           */
    int32_t    MMRed, MMGreen, MMBlue;
    int32_t    buildTime;               /* TFT-only                           */
    int32_t    repairTime;              /* TFT-only                           */
    int32_t    goldRep;                 /* TFT-only                           */
    int32_t    lumberRep;               /* TFT-only                           */
    float   selcircsize;             /* TFT-only                           */
    int32_t    colorR, colorG, colorB;  /* TFT-only                           */
} DestructableData_t;

/* =========================================================================
 * Global arrays and accessor declarations — defined in g_metadata.c.
 *
 * All helpers take a unit/destructable/item FOURCC and return an immutable
 * typed row, or a zero row when the ID is unknown. G_UnitBalance, G_UnitProfile,
 * and G_UnitUI may return stable per-map war3map.w3u overlay rows; the global
 * arrays remain the base decoded data. G_UnitCollision handles the ROC
 * (UnitData) / TFT split.
 * =========================================================================*/
extern UnitBalance_t *g_UnitBalance; extern uint32_t g_UnitBalanceCount;
extern UnitProfile_t *g_UnitProfile; extern uint32_t g_UnitProfileCount;
extern UnitData_t *g_UnitData; extern uint32_t g_UnitDataCount;
extern UnitUI_t *g_UnitUI; extern uint32_t g_UnitUICount;
extern UnitWeapons_t *g_UnitWeapons; extern uint32_t g_UnitWeaponsCount;
extern UpgradeData_t *g_UpgradeData; extern uint32_t g_UpgradeDataCount;
extern UnitAbilities_t *g_UnitAbilities; extern uint32_t g_UnitAbilitiesCount;
extern AbilityData_t *g_AbilityData; extern uint32_t g_AbilityDataCount;
extern AbilityBuffData_t *g_AbilityBuffData; extern uint32_t g_AbilityBuffDataCount;
extern Doodads_t *g_Doodads; extern uint32_t g_DoodadsCount;
extern UberSplatData_t *g_UberSplatData; extern uint32_t g_UberSplatDataCount;
extern UnitAckSounds_t *g_UnitAckSounds; extern uint32_t g_UnitAckSoundsCount;
extern UnitAckSounds_t *g_UnitCombatSounds; extern uint32_t g_UnitCombatSoundsCount;
extern UnitAckSounds_t *g_UISounds; extern uint32_t g_UISoundsCount;
extern UnitAckSounds_t *g_AbilitySounds; extern uint32_t g_AbilitySoundsCount;
extern UnitAckSounds_t *g_AmbienceSounds; extern uint32_t g_AmbienceSoundsCount;
extern UnitAckSounds_t *g_AnimSounds; extern uint32_t g_AnimSoundsCount;
extern UnitAckSounds_t *g_DialogSounds; extern uint32_t g_DialogSoundsCount;
extern MusicData_t *g_MusicData; extern uint32_t g_MusicDataCount;
extern ItemData_t *g_ItemData; extern uint32_t g_ItemDataCount;
extern DestructableData_t *g_DestructableData; extern uint32_t g_DestructableDataCount;

UnitBalance_t const *G_UnitBalance(uint32_t id);
UpgradeData_t const *G_UpgradeData(uint32_t id);
UnitProfile_t const *G_UnitProfile(uint32_t id);
UnitData_t    const *G_UnitData(uint32_t id);
UnitUI_t      const *G_UnitUI(uint32_t id);
UnitWeapons_t const *G_UnitWeapons(uint32_t id);
UnitAbilities_t    const *G_UnitAbil(uint32_t id);
AbilityData_t const *G_AbilityData(uint32_t id);
uint32_t G_AbilityDataGeneration(void);
AbilityData_t const *G_AbilityDataName(cstring_t name);
abilityLevel_t const *G_AbilityLevel(uint32_t id, uint32_t level);
AbilityBuffData_t const *G_AbilityBuffData(uint32_t id);
uint32_t G_AbilityCode(uint32_t id);
uint32_t G_AbilityCodeName(cstring_t name);
cstring_t G_AbilityDataText(cstring_t name, cstring_t column);
Doodads_t const *G_Doodad(uint32_t id);
UberSplatData_t const *G_UberSplat(uint32_t id);
UnitAckSounds_t const *G_UnitAckSound(cstring_t name);
UnitAckSounds_t const *G_UnitCombatSound(cstring_t name);
UnitAckSounds_t const *G_UISound(cstring_t name);
UnitAckSounds_t const *G_AbilitySound(cstring_t name);
UnitAckSounds_t const *G_AmbienceSound(cstring_t name);
UnitAckSounds_t const *G_AnimSound(cstring_t name);
UnitAckSounds_t const *G_DialogSound(cstring_t name);
UnitAckSounds_t const *G_KeyedSound(cstring_t name);
MusicData_t const *G_MusicData(cstring_t name);
ItemData_t    const *G_ItemData(uint32_t id);
ItemData_t    const *G_ItemDataRows(uint32_t *count);
DestructableData_t const *G_DestructableData(uint32_t id);
float G_UnitCollision(uint32_t id); /* TFT UnitBalance.collision → ROC UnitData.collision */
int32_t G_UnitClassification(uint32_t id); /* TFT UnitBalance.type (string enum) → ROC UnitData.type */
float G_UnitCastBackSwing(uint32_t id); /* TFT UnitWeapons.castbsw → ROC UnitData.castbsw */
float G_UnitCastPoint(uint32_t id); /* TFT UnitWeapons.castpt → ROC UnitData.castpt */
float G_UnitAttack1LaunchX(uint32_t id); /* TFT UnitWeapons.launchX → ROC UnitData.launchX */
float G_UnitAttack1LaunchY(uint32_t id); /* TFT UnitWeapons.launchY → ROC UnitData.launchY */
float G_UnitAttack1LaunchZ(uint32_t id); /* TFT UnitWeapons.launchZ → ROC UnitData.launchZ */
float G_UnitImpactZ(uint32_t id); /* TFT UnitWeapons.impactZ → ROC UnitData.impactZ */
bool G_UnitIsBuilding(uint32_t id); /* TFT UnitBalance.isbldg → ROC UnitUI.isbldg */

#endif /* g_unitrow_h */
