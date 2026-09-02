#include "g_local.h"
#include "g_unitrow.h"

/* Register one random authored file from a Warcraft sound-data row.  UI sound
 * aliases use the same FileNames/DirectoryBase schema as UnitAckSounds. */
static int G_RegisterSoundRow(UnitAckSounds_t const *row) {
    LPCSTR files, chosen, comma;
    char file[256], path[512];
    DWORD count = 0, pick;

    if (!row || !row->FileNames || !row->FileNames[0]) return 0;
    files = row->FileNames;
    count = 1;
    for (LPCSTR p = files; (p = strchr(p, ',')) != NULL; p++) count++;
    if (!count) return 0;

    pick = (DWORD)(rand() % count);
    chosen = files;
    while (pick--) {
        chosen = strchr(chosen, ',');
        if (!chosen) return 0;
        chosen++;
    }
    comma = strchr(chosen, ',');
    snprintf(file, sizeof(file), "%.*s",
             comma ? (int)(comma - chosen) : (int)strlen(chosen), chosen);
    if (row->DirectoryBase && row->DirectoryBase[0]) {
        size_t n = strlen(row->DirectoryBase);
        snprintf(path, sizeof(path), "%s%s%s", row->DirectoryBase,
                 row->DirectoryBase[n - 1] == '\\' || row->DirectoryBase[n - 1] == '/'
                     ? "" : "\\",
                 file);
    } else {
        snprintf(path, sizeof(path), "%s", file);
    }
    return gi.SoundIndex(path);
}

static int G_RegisterUISound(LPCSTR alias) {
    UnitAckSounds_t const *row;

    if (!alias || !alias[0]) return 0;
    row = G_UISound(alias);
    return G_RegisterSoundRow(row);
}

void G_PlayUISoundForPlayer(LPEDICT clent, LPCSTR alias) {
    int sound;

    /* UI sounds use the reliable owner-only sound packet and remain non-positional. */
    if (!clent || !clent->client || !clent->client->connected || !alias || !alias[0]) return;
    sound = G_RegisterUISound(alias);
    if (sound) gi.Sound(clent, CHAN_OWNER | CHAN_RELIABLE, sound, 1.0f, 0.0f, 0.0f);
}

static LPCSTR G_CommandErrorKeyForText(LPCSTR text) {
    if (!text) return NULL;
    if (!strcmp(text, "Not enough food") || !strcmp(text, "Not enough food.")) return "Nofood";
    if (!strcmp(text, "Not enough gold") || !strcmp(text, "Not enough gold.")) return "Nogold";
    if (!strcmp(text, "Not enough lumber") || !strcmp(text, "Not enough lumber.")) return "Nolumber";
    if (!strcmp(text, "Not enough mana") || !strcmp(text, "Not enough mana.")) return "Nomana";
    if (!strcmp(text, "Spell is not ready yet") || !strcmp(text, "Spell is not ready yet.")) return "Cooldown";
    if (!strcmp(text, "Unable to build there") || !strcmp(text, "Unable to build there.")) return "Cantplace";
    if (!strcmp(text, "Inventory is full") || !strcmp(text, "Inventory is full.")) return "Inventoryfull";
    return NULL;
}

static void G_PlayCommandErrorSound(LPEDICT clent, LPCSTR error_key) {
    LPGAMECLIENT client;
    LPCSTR alias;
    char skin_key[128];

    if (!clent || !(client = clent->client) || !error_key || !error_key[0]) return;
    snprintf(skin_key, sizeof(skin_key), "%sSound", error_key);
    alias = Theme_PlayerString(client, skin_key, NULL);
    if (!alias || !alias[0]) alias = "InterfaceError";
    G_PlayUISoundForPlayer(clent, alias);
}

void G_ShowCommandErrorText(LPEDICT clent, LPCSTR text) {
    LPCSTR key;

    if (!clent || !text || !text[0]) return;
    UI_ShowTransientText(clent, &MAKE(VECTOR2, 0, 0), text, 2.0f);
    key = G_CommandErrorKeyForText(text);
    if (key) G_PlayCommandErrorSound(clent, key);
    else G_PlayUISoundForPlayer(clent, "InterfaceError");
}

void G_QueueReadySound(LPEDICT ent) {
    if (!ent || !ent->sound.num_ready) return;
    ent->sound.owner_pending = ent->sound.ready[rand() % ent->sound.num_ready];
}

void G_QueueOwnerSoundAlias(LPEDICT ent, LPCSTR alias) {
    int sound;

    if (!ent || ent->s.player >= MAX_PLAYERS || !alias || !alias[0]) return;
    sound = G_RegisterUISound(alias);
    if (sound) ent->sound.owner_pending = sound;
}

void G_QueueOwnerUISound(LPEDICT ent, LPCSTR skin_key) {
    LPGAMECLIENT client;
    LPCSTR alias;

    if (!ent || !skin_key || ent->s.player >= MAX_PLAYERS) return;
    client = G_GetPlayerClientByNumber(ent->s.player);
    if (!client || client->ps.number != ent->s.player) return;
    alias = Theme_PlayerString(client, skin_key, NULL);
    if (!alias || !alias[0]) return;
    G_QueueOwnerSoundAlias(ent, alias);
}
