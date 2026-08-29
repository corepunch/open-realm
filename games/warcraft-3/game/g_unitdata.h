#ifndef g_unitdata_h
#define g_unitdata_h

#include "g_local.h"

/* =========================================================================
 * Unit fields — Profile / INI (name, trains, builds stay string-based)
 * =========================================================================*/
#define UNIT_NAME(UNIT)         UnitStringField(UnitsMetaData, UNIT, "unam")
#define UNIT_PROPER_NAMES(UNIT) UnitStringField(UnitsMetaData, UNIT, "upro")
#define UNIT_BUILDS(UNIT)       UnitStringField(UnitsMetaData, UNIT, "ubui")
#define UNIT_TRAINS(UNIT)       UnitStringField(UnitsMetaData, UNIT, "utra")

/* =========================================================================
 * Unit fields — Profile / INI projectile properties (not typed SLK rows)
 * =========================================================================*/
/* ua1z "Missilespeed" lives in Profile/INI, not UnitWeapons.slk. */
#define UNIT_ATTACK1_PROJECTILE_SPEED(UNIT)      UnitRealField(UnitsMetaData, UNIT, "ua1z")
#define UNIT_ATTACK1_PROJECTILE_ARC(UNIT)        UnitRealField(UnitsMetaData, UNIT, "uma1")
#define UNIT_ATTACK1_PROJECTILE_HOMING_ENABLED(UNIT) UnitBooleanField(UnitsMetaData, UNIT, "umh1")
#define UNIT_ATTACK1_PROJECTILE_ART(UNIT)        UnitStringField(UnitsMetaData, UNIT, "ua1m")
/* ua2z "Missilespeed" lives in Profile/INI, not UnitWeapons.slk. */
#define UNIT_ATTACK2_PROJECTILE_SPEED(UNIT)      UnitRealField(UnitsMetaData, UNIT, "ua2z")
#define UNIT_ATTACK2_PROJECTILE_ARC(UNIT)        UnitRealField(UnitsMetaData, UNIT, "uma2")
#define UNIT_ATTACK2_PROJECTILE_HOMING_ENABLED(UNIT) UnitBooleanField(UnitsMetaData, UNIT, "umh2")
#define UNIT_ATTACK2_PROJECTILE_ART(UNIT)        UnitStringField(UnitsMetaData, UNIT, "ua2m")

sheetRow_t *G_SheetTail(sheetRow_t *rows);

#endif
