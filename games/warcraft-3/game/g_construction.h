#ifndef G_CONSTRUCTION_H
#define G_CONSTRUCTION_H

#include "g_unit_type.h"

/* Construction order is observable to ability callbacks and nested creation.
 * Captured rows stay borrowed for this invocation; later live reads remain
 * live even if a callback rebinds the unit. */
typedef enum {
    UNIT_CONSTRUCT_ALLOCATED,
    UNIT_CONSTRUCT_REQUESTED_POSE,
    UNIT_CONSTRUCT_BOUND_DATA,
    UNIT_CONSTRUCT_OWNER,
    UNIT_CONSTRUCT_VISUALS,
    UNIT_CONSTRUCT_ABILITIES_BEGIN,
    UNIT_CONSTRUCT_ABILITIES_END,
    UNIT_CONSTRUCT_STATS,
    UNIT_CONSTRUCT_LIFECYCLE,
    UNIT_CONSTRUCT_COMBAT,
    UNIT_CONSTRUCT_ASSETS,
    UNIT_CONSTRUCT_SUPPORT,
    UNIT_CONSTRUCT_SOUNDS,
    UNIT_CONSTRUCT_AUTOCAST,
    UNIT_CONSTRUCT_MONSTER,
    UNIT_CONSTRUCT_SERVER_LINK,
    UNIT_CONSTRUCT_FINE_PUBLICATION,
    UNIT_CONSTRUCT_HERO,
    UNIT_CONSTRUCT_BIRTH,
    UNIT_CONSTRUCT_UI,
    UNIT_CONSTRUCT_PLACEMENT,
    UNIT_CONSTRUCT_PUBLIC_STAND,
    UNIT_CONSTRUCT_FACING,
    UNIT_CONSTRUCT_FOOD,
    UNIT_CONSTRUCT_AI,
    UNIT_CONSTRUCT_CHECKPOINT
} unitConstructionStage_t;

typedef struct {
    edict_t *unit;
    unitRuntimeType_t *type;
    edictData_s captured;
    cstring_t path_texture;
    unitConstructionStage_t stage;
    bool fresh;
} unitConstruction_t;

#ifdef BZ_TESTS
typedef void (*unitConstructionTrace_t)(unitConstructionStage_t, edict_t const *, edictData_s const *);
void G_TestSetConstructionTrace(unitConstructionTrace_t);
void G_TestTraceConstruction(unitConstructionStage_t, edict_t const *, edictData_s const *);
#define G_CONSTRUCTION_TRACE(stage, unit, captured) G_TestTraceConstruction(stage, unit, captured)
#else
#define G_CONSTRUCTION_TRACE(stage, unit, captured) ((void)0)
#endif

#endif
