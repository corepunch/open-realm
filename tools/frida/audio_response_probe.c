/* Diagnostic of production server response state, not an alternative algorithm.
 * The fixture supplies authored data; no client acknowledgement is
 * synthesized. A queued request has not yet been admitted or started. */
#include "games/warcraft-3/game/g_local.h"
intptr_t CAbilityAttack(LPEDICT ent, abilityMsg_t msg, abilityCall_t const *call);
intptr_t S_AbilityMessage(LPEDICT ent, abilityMsg_t msg, abilityCall_t const *call);
#include "games/warcraft-3/game/g_commands.c"
struct game_import gi;
struct game_export globals;
LPGAMECLIENT G_GetPlayerClientByNumber(uint32_t player) { return NULL; }
uint32_t G_UnitAckSoundVariantCount(cstring_t label, cstring_t suffix) { return 0; }
int G_UnitAckSoundVariantIndex(cstring_t label, cstring_t suffix, uint32_t variant) { return 0; }
void G_AcceptSoundVariant(int index, uint32_t owner) {}
bool G_SoundVariantIsLast(int index, uint32_t owner) { return false; }
int G_UISoundIndex(cstring_t alias) { return 0; }
cstring_t Theme_PlayerString(LPGAMECLIENT client, cstring_t key, cstring_t section) { return NULL; }
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
