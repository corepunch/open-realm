/* Diagnostic of production server response state, not an alternative algorithm.
 * The fixture supplies authored data; no client acknowledgement is
 * synthesized. A queued request has not yet been admitted or started. */
#include "games/warcraft-3/game/g_local.h"
intptr_t CAbilityAttack(LPEDICT ent, abilityMsg_t msg, abilityCall_t const *call);
intptr_t S_AbilityMessage(LPEDICT ent, abilityMsg_t msg, abilityCall_t const *call);
#include "games/warcraft-3/game/g_commands.c"
struct game_import gi;
struct game_export globals;
LPGAMECLIENT G_GetPlayerClientByNumber(DWORD player) { return NULL; }
DWORD G_UnitAckSoundVariantCount(LPCSTR label, LPCSTR suffix) { return 0; }
int G_UnitAckSoundVariantIndex(LPCSTR label, LPCSTR suffix, DWORD variant) { return 0; }
void G_AcceptSoundVariant(int index, DWORD owner) {}
BOOL G_SoundVariantIsLast(int index, DWORD owner) { return false; }
int G_UISoundIndex(LPCSTR alias) { return 0; }
LPCSTR Theme_PlayerString(LPGAMECLIENT client, LPCSTR key, LPCSTR section) { return NULL; }
int main(void) {
    edict_t unit = {0};
    unit.s.number = 24; unit.s.player = 0; unit.spawn_time = 1;
    unit.sound.num_select = 1; unit.sound.select[0] = 1;
    G_ResetSelectionSoundState();
    G_QueueSelectionSound(&unit, true);
    printf("{\"queued\":%d,\"talking_before_client_admission\":%d,\"count_before_client_admission\":%u}\n",
           unit.sound.pending != 0, G_UnitResponseTalking(&unit), selection_sound_state[0].selected_sound_count);
    return 0;
}
