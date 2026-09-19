#include "g_local.h"

static DWORD G_ClampTeamColor(DWORD color) {
    DWORD const max_color = (EFX_TEAM_COLOR_MASK >> EFX_TEAM_COLOR_SHIFT) - 1u;
    return MIN(color, max_color);
}

static DWORD G_PlayerTeamColor(DWORD player) {
    LPGAMECLIENT client = G_GetPlayerClientByNumber(player);
    if (client && client->ps.number == player) return G_ClampTeamColor(client->ps.color);
    return G_ClampTeamColor(player);
}

DWORD G_GetUnitTeamColor(LPCEDICT unit) {
    DWORD encoded;

    if (!unit) return 0;
    encoded = (unit->s.effect_flags & EFX_TEAM_COLOR_MASK) >> EFX_TEAM_COLOR_SHIFT;
    if (encoded) return encoded - 1u;
    /* Retail rows omit utco for ordinary units; the SLK decoder represents
     * that missing integer as zero, which is also PLAYER_COLOR_RED.  Only
     * customTeamColor makes zero an authored unit-type color here. */
    if (unit->data.UnitUI && (unit->data.UnitUI->teamColor > 0 ||
                              (unit->data.UnitUI->teamColor == 0 && unit->data.UnitUI->customTeamColor)))
        return G_ClampTeamColor((DWORD)unit->data.UnitUI->teamColor);
    return G_PlayerTeamColor(unit->s.player);
}

void G_SetEntityTeamColor(LPENTITYSTATE state, DWORD color) {
    DWORD const encoded = G_ClampTeamColor(color) + 1u;

    if (!state) return;
    state->effect_flags = (state->effect_flags & ~EFX_TEAM_COLOR_MASK) |
        (USHORT)(encoded << EFX_TEAM_COLOR_SHIFT);
}

void G_SetUnitTeamColor(LPEDICT unit, DWORD color) {
    if (unit) G_SetEntityTeamColor(&unit->s, color);
}

void G_InheritUnitTeamColor(LPEDICT entity, LPCEDICT source) {
    if (entity && source) G_SetEntityTeamColor(&entity->s, G_GetUnitTeamColor(source));
}

void G_InitializeUnitTeamColor(LPEDICT unit) {
    DWORD color;

    if (!unit) return;
    color = unit->data.UnitUI && (unit->data.UnitUI->teamColor > 0 ||
                              (unit->data.UnitUI->teamColor == 0 && unit->data.UnitUI->customTeamColor))
        ? (DWORD)unit->data.UnitUI->teamColor : G_PlayerTeamColor(unit->s.player);
    G_SetUnitTeamColor(unit, color);
}

void G_ApplyMapUnitTeamColor(LPEDICT unit, LPCDOODAD placement) {
    LONG custom_color;

    if (!unit || !placement) return;
    custom_color = (LONG)placement->customTeamColor;
    if (unit->data.UnitUI && unit->data.UnitUI->customTeamColor && custom_color >= 0)
        G_SetUnitTeamColor(unit, (DWORD)custom_color);
    else
        G_InitializeUnitTeamColor(unit);
}

void G_ChangePlayerTeamColor(LPPLAYER player, DWORD previous_color, DWORD new_color) {
    DWORD const player_num = player ? PLAYER_NUM(player) : MAX_PLAYERS;

    if (!player || previous_color == new_color) return;
    FILTER_EDICTS(unit, (unit->svflags & SVF_MONSTER) && unit->s.player == player_num) {
        if (G_GetUnitTeamColor(unit) == G_ClampTeamColor(previous_color))
            G_SetUnitTeamColor(unit, new_color);
    }
}

BOOL G_GetUnitColorOverride(LPCEDICT unit, LPDWORD color) {
    DWORD stored;

    if (!unit || !(stored = unit->unit_color)) return false;
    if (stored & WC3_UNIT_COLOR_OVERRIDE_FLAG)
        stored = G_ClampTeamColor(stored & WC3_UNIT_COLOR_VALUE_MASK);
    else
        stored = G_ClampTeamColor(stored); /* pre-flag game-cache/save compatibility */
    if (color) *color = stored;
    return true;
}

void G_SetUnitColorOverride(LPEDICT unit, DWORD color) {
    DWORD const clamped = G_ClampTeamColor(color);

    if (!unit) return;
    unit->unit_color = WC3_UNIT_COLOR_OVERRIDE_FLAG | clamped;
    G_SetUnitTeamColor(unit, clamped);
}

void G_ClearUnitColorOverride(LPEDICT unit) {
    if (unit) unit->unit_color = 0;
}
