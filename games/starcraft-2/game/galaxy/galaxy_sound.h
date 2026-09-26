/* galaxy_sound.h — sound and soundtrack natives */

#define MAX_GALAXY_SOUNDS 256  /* links; bounds SoundLink handles retained by live Galaxy scripts */
typedef struct { char id[96]; int32_t asset; } sc2GSound_t;
static sc2GSound_t sc2_gsounds[MAX_GALAXY_SOUNDS];
static int32_t sc2_gsound_n = 1;

static uint32_t sc2_SoundLink(jass_t * j) {
    cstring_t id = jass_checkstring(j, 1);
    int32_t asset = jass_checkinteger(j, 2), h;
    if (!id || !*id || sc2_gsound_n >= MAX_GALAXY_SOUNDS)
        return jass_pushnullhandle(j, "soundlink");
    h = sc2_gsound_n++;
    snprintf(sc2_gsounds[h].id, sizeof(sc2_gsounds[h].id), "%s", id);
    sc2_gsounds[h].asset = asset;
    return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "soundlink");
}
static uint32_t sc2_SoundLinkAsset(jass_t * j)     { return jass_pushnullhandle(j, "soundlink"); }
static uint32_t sc2_SoundLinkId(jass_t * j)        { return jass_pushnullhandle(j, "soundlink"); }
/* SoundPlay(soundlink, bool looping, bool is3d, bool stopIfDeath, int mask) */
static uint32_t sc2_SoundPlay(jass_t * j) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "soundlink");
    if (h > 0 && h < sc2_gsound_n && sc2_galaxy_on_sound)
        sc2_galaxy_on_sound(sc2_gsounds[h].id, sc2_gsounds[h].asset);
    return jass_pushnullhandle(j, "sound");
}
/* SoundPlayAtPoint(soundlink, int mask, point, float height) */
static uint32_t sc2_SoundPlayAtPoint(jass_t * j) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "soundlink");
    if (h > 0 && h < sc2_gsound_n && sc2_galaxy_on_sound)
        sc2_galaxy_on_sound(sc2_gsounds[h].id, sc2_gsounds[h].asset);
    return jass_pushnullhandle(j, "sound");
}
/* SoundPlayOnUnit(soundlink, int mask, unit) */
static uint32_t sc2_SoundPlayOnUnit(jass_t * j) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "soundlink");
    if (h > 0 && h < sc2_gsound_n && sc2_galaxy_on_sound)
        sc2_galaxy_on_sound(sc2_gsounds[h].id, sc2_gsounds[h].asset);
    return jass_pushnullhandle(j, "sound");
}
static uint32_t sc2_SoundPlayScene(jass_t * j)     { return jass_pushnullhandle(j, "sound"); }
static uint32_t sc2_SoundPlaySceneFile(jass_t * j) { return jass_pushnullhandle(j, "sound"); }
static uint32_t sc2_SoundStop(jass_t * j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_SoundWait(jass_t * j) {
    float secs = jass_checknumber(j, 2);
    if (secs > 0.0f) jass_sleep(j, (uint32_t)(secs * 1000.0f));
    return jass_pushnull(j);
}
static float sc2_sound_length(jass_t * j, int arg) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, arg, "soundlink");
    return h > 0 && h < sc2_gsound_n && sc2_galaxy_sound_length
        ? sc2_galaxy_sound_length(sc2_gsounds[h].id, sc2_gsounds[h].asset) : 0.0f;
}
static uint32_t sc2_SoundLengthSync(jass_t * j)    { return jass_pushnumber(j, sc2_sound_length(j, 1)); }
static uint32_t sc2_SoundtrackPlay(jass_t * j)     { (void)j; return jass_pushnull(j); }
static uint32_t sc2_SoundtrackPause(jass_t * j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_SoundtrackDefault(jass_t * j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_SoundChannelSetVolume(jass_t * j) { (void)j; return jass_pushnull(j); }
