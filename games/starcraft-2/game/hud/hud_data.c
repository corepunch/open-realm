/* HUD localization follows archive precedence, just like Assets.txt. */
#include "hud.h"

#define SC2_HUD_STRINGS 32768 // entries; Core+Liberty+campaign localized keys
static struct { char *key, *value; } sc2_hud_strings[SC2_HUD_STRINGS];
static void *sc2_hud_string_buffers[64];
static uint32_t sc2_hud_string_buffer_count;

static uint32_t sc2_hud_string_hash(cstring_t key) {
    uint32_t hash=2166136261u;
    while (*key) hash=(hash^(uint8_t)*key++)*16777619u;
    return hash;
}
static void sc2_hud_parse_strings(handle_t data, uint32_t size, void *user) {
    (void)user;
    if (sc2_hud_string_buffer_count==(sizeof(sc2_hud_string_buffers)/sizeof(*sc2_hud_string_buffers))) { fprintf(stderr,"SC2 HUD localization layers full\n"); return; }
    char *buffer=malloc(size+1);
    if (!buffer) { fprintf(stderr,"SC2 HUD localization allocation failed\n"); return; }
    memcpy(buffer,data,size); buffer[size]=0;
    sc2_hud_string_buffers[sc2_hud_string_buffer_count++]=buffer;
    for (char *line=buffer,*next; line; line=next) {
        next=strchr(line,'\n'); if (next) *next++=0;
        char *cr=strchr(line,'\r'); if (cr) *cr=0;
        char *eq=strchr(line,'='); if (!eq) continue;
        *eq++=0;
        uint32_t index=sc2_hud_string_hash(line)%SC2_HUD_STRINGS, start=index;
        while (sc2_hud_strings[index].key && strcmp(sc2_hud_strings[index].key,line)) {
            index=(index+1)%SC2_HUD_STRINGS;
            if (index==start) { fprintf(stderr,"SC2 HUD localization table full: '%s'\n",line); return; }
        }
        sc2_hud_strings[index].key=line; sc2_hud_strings[index].value=eq;
    }
}
void SC2_HUD_LoadStrings(void) {
    gi.ReadFileAll("LocalizedData/GameStrings.txt",sc2_hud_parse_strings,NULL);
    gi.ReadFileAll("LocalizedData/GameHotkeys.txt",sc2_hud_parse_strings,NULL);
}
cstring_t SC2_HUD_Localized(cstring_t key) {
    if (!key || !*key) return "";
    uint32_t index=sc2_hud_string_hash(key)%SC2_HUD_STRINGS,start=index;
    while (sc2_hud_strings[index].key) {
        if (!strcmp(sc2_hud_strings[index].key,key)) return sc2_hud_strings[index].value;
        index=(index+1)%SC2_HUD_STRINGS;
        if (index==start) break;
    }
    return key;
}
void SC2_HUD_FreeStrings(void) {
    FOR_LOOP(i,sc2_hud_string_buffer_count) free(sc2_hud_string_buffers[i]);
    sc2_hud_string_buffer_count=0;
    memset(sc2_hud_strings,0,sizeof(sc2_hud_strings));
}
