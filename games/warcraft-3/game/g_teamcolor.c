#include "g_local.h"

/* Clamp a Warcraft playercolor to the payload width reserved by entityState_t. */
static uint32_t G_ClampTeamColor(uint32_t color) {
    uint32_t const max_color = (EFX_TEAM_COLOR_MASK >> EFX_TEAM_COLOR_SHIFT) - 1u;
    return MIN(color, max_color);
}

/* Resolve a player's configured color while retaining the slot default for unconfigured players. */
static uint32_t G_PlayerTeamColor(uint32_t player) {
    LPGAMECLIENT client = G_GetPlayerClientByNumber(player);
    if (client && client->ps.number == player) return G_ClampTeamColor(client->ps.color);
    return G_ClampTeamColor(player);
}

/* Return whether authored unit data owns the resolved color instead of the player slot. */
static bool G_UnitTeamColorIsAuthored(LPCEDICT unit) {
    return unit && unit->data.UnitUI &&
        (unit->data.UnitUI->teamColor > 0 ||
         (unit->data.UnitUI->teamColor == 0 && unit->data.UnitUI->customTeamColor));
}

/* Resolve the presentation color from an entity payload, authored data, or its owner. */
uint32_t G_GetUnitTeamColor(LPCEDICT unit) {
    uint32_t encoded;

    if (!unit) return 0;
    encoded = (unit->s.effect_flags & EFX_TEAM_COLOR_MASK) >> EFX_TEAM_COLOR_SHIFT;
    if (encoded) return encoded - 1u;
    /* Retail rows omit utco for ordinary units; the SLK decoder represents
     * that missing integer as zero, which is also PLAYER_COLOR_RED.  Only
     * customTeamColor makes zero an authored unit-type color here. */
    if (G_UnitTeamColorIsAuthored(unit))
        return G_ClampTeamColor((uint32_t)unit->data.UnitUI->teamColor);
    return G_PlayerTeamColor(unit->s.player);
}

/* Publish a resolved playercolor without disturbing unrelated entity effect bits. */
void G_SetEntityTeamColor(LPENTITYSTATE state, uint32_t color) {
    uint32_t const encoded = G_ClampTeamColor(color) + 1u;

    if (!state) return;
    state->effect_flags = (state->effect_flags & ~EFX_TEAM_COLOR_MASK) |
        (uint16_t)(encoded << EFX_TEAM_COLOR_SHIFT);
}

/* Publish a resolved playercolor on a unit entity. */
void G_SetUnitTeamColor(LPEDICT unit, uint32_t color) {
    if (unit) G_SetEntityTeamColor(&unit->s, color);
}

/* Copy the resolved presentation color from a source unit to an attached effect. */
void G_InheritUnitTeamColor(LPEDICT entity, LPCEDICT source) {
    if (entity && source) G_SetEntityTeamColor(&entity->s, G_GetUnitTeamColor(source));
}

/* Initialize a spawned unit's presentation color from authored data or its owner. */
void G_InitializeUnitTeamColor(LPEDICT unit) {
    uint32_t color;

    if (!unit) return;
    color = G_UnitTeamColorIsAuthored(unit)
        ? (uint32_t)unit->data.UnitUI->teamColor : G_PlayerTeamColor(unit->s.player);
    G_SetUnitTeamColor(unit, color);
}

/* UnitUI.slk red/green/blue is the authored default vertex tint. Keep the
 * normal white case unset so this presentation extension is only sent for
 * units that actually differ from the renderer's white default. */
void G_InitializeUnitVertexColor(LPEDICT unit) {
    UnitUI_t const *ui;

    if (!unit || !(ui = unit->data.UnitUI)) return;
    if (unit->vertex_color_override_set) return;
    if (ui->tintRed == 255 && ui->tintGreen == 255 && ui->tintBlue == 255) {
        unit->vertex_color = COLOR32_WHITE;
        unit->vertex_color_set = false;
        return;
    }
    unit->vertex_color = MAKE(COLOR32,
        BZ_CLAMP_U8(ui->tintRed), BZ_CLAMP_U8(ui->tintGreen),
        BZ_CLAMP_U8(ui->tintBlue), 255);
    unit->vertex_color_set = true;
}

/* Apply the map placement custom color when the unit type permits authored colors. */
void G_ApplyMapUnitTeamColor(LPEDICT unit, LPCDOODAD placement) {
    int32_t custom_color;

    if (!unit || !placement) return;
    custom_color = (int32_t)placement->color;
    if (unit->data.UnitUI && unit->data.UnitUI->customTeamColor && custom_color >= 0)
        G_SetUnitTeamColor(unit, (uint32_t)custom_color);
    else
        G_InitializeUnitTeamColor(unit);
}

/* Recolor only units whose presentation still belongs to the changing player. */
void G_ChangePlayerTeamColor(LPPLAYER player, uint32_t previous_color, uint32_t new_color) {
    uint32_t const player_num = player ? PLAYER_NUM(player) : MAX_PLAYERS;

    if (!player || previous_color == new_color) return;
    FILTER_EDICTS(unit, (unit->svflags & SVF_MONSTER) && unit->s.player == player_num) {
        if (!G_GetUnitColorOverride(unit, NULL) && !G_UnitTeamColorIsAuthored(unit) &&
            G_GetUnitTeamColor(unit) == G_ClampTeamColor(previous_color))
            G_SetUnitTeamColor(unit, new_color);
    }
}

/* Read an explicit JASS/game-cache unit color, including legacy unflagged values. */
bool G_GetUnitColorOverride(LPCEDICT unit, uint32_t * color) {
    uint32_t stored;

    if (!unit || !(stored = unit->unit_color)) return false;
    if (stored & WC3_UNIT_COLOR_OVERRIDE_FLAG)
        stored = G_ClampTeamColor(stored & WC3_UNIT_COLOR_VALUE_MASK);
    else
        stored = G_ClampTeamColor(stored); /* pre-flag game-cache/save compatibility */
    if (color) *color = stored;
    return true;
}

/* Store an explicit JASS unit color and publish it immediately. */
void G_SetUnitColorOverride(LPEDICT unit, uint32_t color) {
    uint32_t const clamped = G_ClampTeamColor(color);

    if (!unit) return;
    unit->unit_color = WC3_UNIT_COLOR_OVERRIDE_FLAG | clamped;
    G_SetUnitTeamColor(unit, clamped);
}

/* Remove an explicit JASS unit color so future owner changes can apply. */
void G_ClearUnitColorOverride(LPEDICT unit) {
    if (unit) unit->unit_color = 0;
}
