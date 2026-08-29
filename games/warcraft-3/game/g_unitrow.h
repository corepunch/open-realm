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
    DWORD id;
    LPCSTR name, properNames, builds, trains;
    LPCSTR animProps, art, itemArt, attachmentAnimProps, attachmentLinkProps, awakenTip;
    LPCSTR boneProps, buildingSoundLabel, buttonPosX, buttonPosY;
    LPCSTR casterUpgradeArt, casterUpgradeName, casterUpgradeTip, dependencyOr;
    LPCSTR description, editorSuffix, hotkey, loopingSoundFadeIn, loopingSoundFadeOut;
    LPCSTR makeItems, movementSoundLabel, randomSoundLabel;
    LPCSTR requiresCount, requires, requiresLevel[8], requiresAmount;
    LPCSTR researches, revive, reviveTip, scoreScreenIcon, sellItems, sellUnits;
    LPCSTR specialArt, targetArt, tip, reviveAt, uberTip, upgrade;
    struct { LPCSTR art; FLOAT arc, speed; BOOL homing; } attack[2];
} UnitProfile_t;

/* =========================================================================
 * UnitBalance.slk
 * =========================================================================*/
typedef struct {
    DWORD   id;
    LPCSTR  sortBalance, sort2, comments;
    BOOL    abilTest, InBeta;
    /* economy ---------------------------------------------------------------*/
    LONG    level;
    LONG    goldCost, lumberCost;
    LONG    goldRep, lumberRep;
    LONG    foodMade, foodUsed;
    /* bounty ----------------------------------------------------------------*/
    LONG    goldBountyDice, goldBountySides, goldBountyBase;
    LONG    lumberBountyDice, lumberBountySides, lumberBountyBase;
    /* stock -----------------------------------------------------------------*/
    LONG    stockMax, stockRegen, stockStart;
    /* HP / mana -------------------------------------------------------------*/
    LONG    baseHealth;
    FLOAT   maxHealth;
    FLOAT   healthRegen;
    LPCSTR  healthRegenType;
    LONG    baseMana;
    FLOAT   maxMana;
    FLOAT   initialMana;
    FLOAT   manaRegen;
    /* armor -----------------------------------------------------------------*/
    LONG    baseArmor;
    LONG    armorPerUpgrade;
    FLOAT   armor;
    LPCSTR  defenseType;
    /* movement --------------------------------------------------------------*/
    FLOAT   speed;
    FLOAT   maxSpeed, minSpeed;
    LONG    buildTime;
    FLOAT   sightRadius, nightSightRadius;
    /* hero attributes -------------------------------------------------------*/
    LONG    strength, intelligence, agility;
    FLOAT   strengthPerLevel, intelligencePerLevel, agilityPerLevel;
    LPCSTR  primaryAttribute;
    LPCSTR  upgrades;
    /* TFT-only fields -------------------------------------------------------*/
    FLOAT   collision;               /* ROC: stored in UnitData.slk        */
    BOOL    isBuilding;              /* ROC: stored in UnitUI.slk          */
    LONG    nbrandom;
    LPCSTR  preventPlace;
    LONG    reptm;
    LONG    repulse;
    LONG    repulseGroup;
    FLOAT   repulseParam;
    LONG    repulsePrio;
    LPCSTR  requirePlace;
    LPCSTR  tilesets;
    LPCSTR  type;                    /* unit type string                   */
} UnitBalance_t;

/* =========================================================================
 * UnitData.slk
 * =========================================================================*/
typedef struct {
    DWORD   id;
    LPCSTR  sort, comments;
    LONG    version;
    BOOL    valid, InBeta;
    /* buffs / targeting -----------------------------------------------------*/
    FLOAT   buffRadius;
    LPCSTR  buffType;
    LPCSTR  targetType;
    /* behaviour -------------------------------------------------------------*/
    BOOL    canSleep;
    BOOL    canFlee;                 /* TFT-only                           */
    BOOL    canBuildOn;              /* TFT-only                           */
    BOOL    isBuildOn;               /* TFT-only                           */
    LONG    cargoSize;
    /* death -----------------------------------------------------------------*/
    FLOAT   death;                   /* corpse decay time                  */
    LONG    deathType;
    /* line-of-sight / formation ---------------------------------------------*/
    BOOL    useExtendedLineOfSight;
    LONG    formationRank;
    /* movement --------------------------------------------------------------*/
    FLOAT   moveFloor, moveHeight;
    LPCSTR  moveTypeName;            /* "foot"/"fly"/"hover"/"float"/"amph"/"horse" */
    FLOAT   turnRate;
    /* misc ------------------------------------------------------------------*/
    LONG    nameCount;
    LONG    orientationInterpolation;
    LPCSTR  pathingTexture;
    LONG    points;
    LONG    priority;
    FLOAT   propWin;
    LPCSTR  race;
    FLOAT   requireWaterRadius;      /* TFT-only                           */
    LONG    threat;
    /* ROC-only columns (moved to UnitWeapons in TFT) ------------------------*/
    FLOAT   castBackSwing;           /* ROC: cast back-swing; TFT: UnitWeapons */
    FLOAT   castPoint;               /* ROC: cast point;      TFT: UnitWeapons */
    FLOAT   collision;               /* ROC: collision radius; TFT: UnitBalance */
    FLOAT   impactHeight;            /* ROC: impact Z offset;  TFT: UnitWeapons */
    FLOAT   launchOffsetX, launchOffsetY, launchOffsetZ; /* ROC: launch offsets; TFT: UnitWeapons */
    LPCSTR  unitClassification;      /* ROC: unit type string; TFT: UnitBalance */
} UnitData_t;

/* =========================================================================
 * UnitUI.slk
 * =========================================================================*/
typedef struct {
    DWORD   id;
    LPCSTR  sortUI;
    BOOL    InBeta;
    /* model / art -----------------------------------------------------------*/
    LPCSTR  modelFile;               /* MDX model path ("umdl")            */
    FLOAT   modelScale;
    FLOAT   blend;
    /* team colour / tinting -------------------------------------------------*/
    LONG    tintRed, tintGreen, tintBlue;
    LONG    teamColor;
    BOOL    customTeamColor;
    BOOL    hostilePal;
    /* selection / shadow ----------------------------------------------------*/
    FLOAT   selectionScale;          /* selection circle scale             */
    FLOAT   selectionCircleHeight;
    FLOAT   shadowHeight, shadowWidth, shadowCenterX, shadowCenterY;
    LPCSTR  unitShadowTexture;
    BOOL    selectionCircleOnWater;  /* TFT-only                           */
    BOOL    waterShadow;             /* TFT-only                           */
    BOOL    scaleProjectiles;        /* scale projectiles                  */
    /* elevation -------------------------------------------------------------*/
    LONG    elevationSamplePoints;
    FLOAT   elevationSampleRadius;
    FLOAT   fogOfWarSampleRadius;
    FLOAT   occluderHeight;
    /* animation -------------------------------------------------------------*/
    FLOAT   animationRunSpeed, animationWalkSpeed;
    FLOAT   maxPitchDegrees, maxRollDegrees;
    /* sound -----------------------------------------------------------------*/
    LPCSTR  soundLabel;
    /* strings ---------------------------------------------------------------*/
    LPCSTR  name;
    LPCSTR  groundTexture;
    LPCSTR  buildingShadowTexture;
    LPCSTR  special;
    /* flags -----------------------------------------------------------------*/
    LONG    armorType;               /* armor type index for info panel    */
    LONG    unitClass;
    BOOL    neutralBuildingMinimapIcon; /* neutral building minimap icon   */
    BOOL    inEditor;
    BOOL    hiddenInEditor;
    BOOL    dropItems;
    BOOL    useClickHelper;
    BOOL    campaign;                /* TFT-only                           */
    LONG    fileVerFlags;            /* TFT-only                           */
    BOOL    hideHeroBar;             /* TFT-only                           */
    BOOL    hideHeroDeathMsg;        /* TFT-only                           */
    BOOL    hideHeroMinimap;         /* TFT-only                           */
    BOOL    hideOnMinimap;           /* TFT-only                           */
    BOOL    tilesetSpecific;
    LPCSTR  requirePlace, preventPlace, tilesets; /* ROC-only */
    LONG    nbrandom;                             /* ROC-only */
    /* weapon slots (index into UnitWeapons) ---------------------------------*/
    LONG    weaponSlot1, weaponSlot2;
    /* ROC-only fields (moved to UnitBalance in TFT) -------------------------*/
    BOOL    isBuilding;              /* ROC: is building; TFT: UnitBalance */
} UnitUI_t;

/* =========================================================================
 * UnitWeapons.slk
 * =========================================================================*/
typedef struct {
    LPCSTR attackType, rangeTest, damageUpgrade, damageModifier, weaponType;
    LONG damageDice, damageSides, damageBase, damageUpgradeAmount;
    LONG areaTargets, maxTargets, targetsAllowed, weaponSound;
    FLOAT damagePoint, backswingPoint, cooldown;
    FLOAT minCooldown, minDamage, averageDamage, maxDamage, damagePerSecond;
    FLOAT damageLossFactor, range, rangeBuffer;
    FLOAT areaFull, areaMedium, areaSmall, factorMedium, factorSmall;
    FLOAT spillDistance, spillRadius;
    BOOL showUI;
} UnitWeapon_t;

typedef struct {
    DWORD   id;
    LPCSTR  sortWeap, sort2, comments;
    BOOL    InBeta;
    FLOAT   acquisitionRange;        /* auto-attack trigger range          */
    UnitWeapon_t attack1, attack2;
    /* shared / misc ---------------------------------------------------------*/
    LONG    attacksEnabled;
    FLOAT   minimumAttackRange;
    /* TFT-only columns (moved from UnitData in ROC) -------------------------*/
    FLOAT   castBackSwing;           /* ROC: stored in UnitData.slk        */
    FLOAT   castPoint;               /* ROC: stored in UnitData.slk        */
    FLOAT   impactSwimZ;             /* TFT-only                           */
    FLOAT   impactHeight;            /* ROC: stored in UnitData.slk        */
    FLOAT   launchSwimZ;             /* TFT-only                           */
    FLOAT   attackLaunchX, attackLaunchY, attackLaunchZ; /* ROC: stored in UnitData.slk */
} UnitWeapons_t;

/* =========================================================================
 * UnitAbilities.slk
 * =========================================================================*/
typedef struct {
    DWORD   id;
    LPCSTR  sortAbil, comments;
    LPCSTR  abilList;                /* comma-separated ability codes      */
    LPCSTR  heroAbilList;            /* hero abilities                     */
    BOOL    auto_, InBeta;            /* auto is a keyword, use auto_       */
} UnitAbilities_t;

/* =========================================================================
 * AbilityData.slk
 * =========================================================================*/
typedef struct {
    DWORD id, code, uberAlias;
    LPCSTR comments, sort, race;
    LONG version, levels, reqLevel, levelSkip, priority;
    BOOL useInEditor, hero, item, checkDep, InBeta;
    LPCSTR targs[4];
    FLOAT cast[4], dur[4], heroDur[4], cool[4], cost[4], area[4], range[4];
    FLOAT data[4][9];
    DWORD unitID[4];
    LPCSTR buffID[4], efctID[4];
    LPCSTR castCheck, durCheck, heroDurCheck, coolCheck, costCheck, areaCheck, rangeCheck;
} AbilityData_t;

/* =========================================================================
 * Doodads.slk
 * =========================================================================*/
typedef struct {
    DWORD id;
    LPCSTR category, tilesets, dir, file, comment, Name, doodClass, soundLoop, shadow, pathTex, UserList;
    BOOL tilesetSpecific, canPlaceRandScale, useClickHelper, ignoreModelClick;
    BOOL walkable, onCliffs, onWater, floats, showInFog, animInFog, showInMM, useMMColor, InBeta;
    FLOAT selSize, defScale, minScale, maxScale, maxPitch, maxRoll, visRadius, fixedRot;
    LONG numVar, MMRed, MMGreen, MMBlue, version;
    LONG vert[10][3];
} Doodads_t;

/* =========================================================================
 * UberSplatData.slk
 * =========================================================================*/
typedef struct {
    DWORD id;
    LPCSTR Name, comment, Dir, file, BlendMode, Sound;
    FLOAT Scale, BirthTime, PauseTime, Decay;
    FLOAT StartR, StartG, StartB, StartA;
    FLOAT MiddleR, MiddleG, MiddleB, MiddleA;
    FLOAT EndR, EndG, EndB, EndA;
    LONG version;
    BOOL InBeta;
} UberSplatData_t;

/* =========================================================================
 * UnitAckSounds.slk
 * =========================================================================*/
typedef struct {
    DWORD key;
    LPCSTR name, FileNames, DirectoryBase, Channel, Flags, EAXFlags;
    FLOAT Volume, Pitch, PitchVariance, Priority, MinDistance, MaxDistance, DistanceCutoff;
    FLOAT InsideAngle, OutsideAngle, OutsideVolume, OrientationX, OrientationY, OrientationZ;
    LONG version;
    BOOL InBeta;
} UnitAckSounds_t;

/* =========================================================================
 * ItemData.slk
 * =========================================================================*/
typedef struct {
    DWORD   id;
    LPCSTR  scriptname;
    LONG    version;
    BOOL    InBeta;
    LPCSTR  displayName;             /* display name (column "comment")    */
    LPCSTR  file;                    /* MDX model path                     */
    LPCSTR  abilList;
    LPCSTR  itemClass;               /* TFT: item class; ROC: itemClass    */
    LONG    armor;
    LONG    goldcost, lumbercost;
    LONG    HP;
    LONG    level;
    LONG    prio;
    LONG    stockMax, stockRegen, stockStart;
    LONG    uses;
    FLOAT   scale;                   /* TFT-only                           */
    FLOAT   selectionSize;           /* TFT-only                           */
    BOOL    droppable, drop;
    BOOL    perishable;
    BOOL    sellable;                /* TFT-only                           */
    BOOL    pawnable;                /* TFT-only                           */
    BOOL    usable;
    BOOL    pickRandom;              /* TFT-only                           */
    BOOL    powerup;                 /* TFT-only                           */
    BOOL    morph;                   /* TFT-only                           */
    BOOL    ignoreCD;                /* TFT-only                           */
    LPCSTR  cooldownID;              /* TFT-only                           */
    LONG    oldLevel;                /* TFT-only                           */
    LONG    colorR, colorG, colorB;  /* TFT-only                           */
} ItemData_t;

/* =========================================================================
 * DestructableData.slk
 * =========================================================================*/
typedef struct {
    DWORD   id;
    LPCSTR  category, tilesets, comment, doodClass;
    LONG    version;
    BOOL    InBeta;
    LPCSTR  file;                    /* MDX model path                     */
    LPCSTR  dir;                     /* subdirectory prefix (ROC-only)     */
    LPCSTR  displayName;             /* TFT (ROC used "name" → lowercase)  */
    LPCSTR  EditorSuffix;            /* TFT-only                           */
    LPCSTR  textureFile;
    LPCSTR  texID;
    LPCSTR  shadow;
    LPCSTR  pathingTexture;
    LPCSTR  deathPathingTexture;
    LPCSTR  deathSnd;
    LPCSTR  targetType;
    LPCSTR  portraitmodel;           /* TFT-only                           */
    LPCSTR  UserList;                /* TFT-only                           */
    LONG    maxHealth;
    LONG    armor;
    LONG    numVar;
    FLOAT   selSize;
    FLOAT   minScale, maxScale;
    FLOAT   maxPitch, maxRoll;
    FLOAT   radius;
    FLOAT   fogRadius;
    FLOAT   occluderHeight, flyHeight;
    FLOAT   cliffHeight;
    BOOL    fogVisible;
    BOOL    fatLOS;
    BOOL    walkable;
    BOOL    onCliffs, onWater;
    BOOL    canPlaceDead;
    BOOL    canPlaceRandScale;
    BOOL    lightweight;
    BOOL    tilesetSpecific;
    BOOL    useClickHelper;
    BOOL    showInMM;
    BOOL    useMMColor;
    BOOL    fixedRot;
    BOOL    selectable;              /* TFT-only                           */
    LONG    MMRed, MMGreen, MMBlue;
    LONG    buildTime;               /* TFT-only                           */
    LONG    repairTime;              /* TFT-only                           */
    LONG    goldRep;                 /* TFT-only                           */
    LONG    lumberRep;               /* TFT-only                           */
    FLOAT   selcircsize;             /* TFT-only                           */
    LONG    colorR, colorG, colorB;  /* TFT-only                           */
} DestructableData_t;

/* =========================================================================
 * Global arrays and accessor declarations — defined in g_metadata.c.
 *
 * All helpers take a unit/destructable/item FOURCC and return a pointer
 * into the decoded row array, or a zero row when the ID is unknown.
 * G_UnitCollision handles the ROC (UnitData) / TFT (UnitBalance) split.
 * =========================================================================*/
extern UnitBalance_t *g_UnitBalance; extern DWORD g_UnitBalanceCount;
extern UnitProfile_t *g_UnitProfile; extern DWORD g_UnitProfileCount;
extern UnitData_t *g_UnitData; extern DWORD g_UnitDataCount;
extern UnitUI_t *g_UnitUI; extern DWORD g_UnitUICount;
extern UnitWeapons_t *g_UnitWeapons; extern DWORD g_UnitWeaponsCount;
extern UnitAbilities_t *g_UnitAbilities; extern DWORD g_UnitAbilitiesCount;
extern AbilityData_t *g_AbilityData; extern DWORD g_AbilityDataCount;
extern Doodads_t *g_Doodads; extern DWORD g_DoodadsCount;
extern UberSplatData_t *g_UberSplatData; extern DWORD g_UberSplatDataCount;
extern UnitAckSounds_t *g_UnitAckSounds; extern DWORD g_UnitAckSoundsCount;
extern ItemData_t *g_ItemData; extern DWORD g_ItemDataCount;
extern DestructableData_t *g_DestructableData; extern DWORD g_DestructableDataCount;

UnitBalance_t const *G_UnitBalance(DWORD id);
UnitProfile_t const *G_UnitProfile(DWORD id);
UnitData_t    const *G_UnitData(DWORD id);
UnitUI_t      const *G_UnitUI(DWORD id);
UnitWeapons_t const *G_UnitWeapons(DWORD id);
UnitAbilities_t    const *G_UnitAbil(DWORD id);
AbilityData_t const *G_AbilityData(DWORD id);
AbilityData_t const *G_AbilityDataName(LPCSTR name);
DWORD G_AbilityCode(DWORD id);
DWORD G_AbilityCodeName(LPCSTR name);
LPCSTR G_AbilityDataText(LPCSTR name, LPCSTR column);
Doodads_t const *G_Doodad(DWORD id);
UberSplatData_t const *G_UberSplat(DWORD id);
UnitAckSounds_t const *G_UnitAckSound(LPCSTR name);
ItemData_t    const *G_ItemData(DWORD id);
ItemData_t    const *G_ItemDataRows(DWORD *count);
DestructableData_t const *G_DestructableData(DWORD id);
FLOAT G_UnitCollision(DWORD id); /* TFT UnitBalance.collision → ROC UnitData.collision */
LONG G_UnitClassification(DWORD id); /* TFT UnitBalance.type (string enum) → ROC UnitData.type */
FLOAT G_UnitCastBackSwing(DWORD id); /* TFT UnitWeapons.castbsw → ROC UnitData.castbsw */
FLOAT G_UnitCastPoint(DWORD id); /* TFT UnitWeapons.castpt → ROC UnitData.castpt */
FLOAT G_UnitAttack1LaunchX(DWORD id); /* TFT UnitWeapons.launchX → ROC UnitData.launchX */
FLOAT G_UnitAttack1LaunchY(DWORD id); /* TFT UnitWeapons.launchY → ROC UnitData.launchY */
FLOAT G_UnitAttack1LaunchZ(DWORD id); /* TFT UnitWeapons.launchZ → ROC UnitData.launchZ */
BOOL G_UnitIsBuilding(DWORD id); /* TFT UnitBalance.isbldg → ROC UnitUI.isbldg */

#endif /* g_unitrow_h */
