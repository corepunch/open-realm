#ifndef WOW_CHARACTER_SAVE_H
#define WOW_CHARACTER_SAVE_H

/* Load returns 1 on success, 0 for a missing file, and -1 for invalid/I/O
 * errors. Invalid files are left untouched and never partially applied. */
int Wow_CharacterLoadPath(char const *path, wowClient_t *client, wowEntityLocal_t *player);
bool Wow_CharacterSavePath(char const *path, wowClient_t const *client, wowEntityLocal_t const *player);
#endif
