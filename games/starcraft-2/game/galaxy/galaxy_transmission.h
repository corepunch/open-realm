/* galaxy_transmission.h — transmission natives */
static uint32_t sc2_TransmissionSource(jass_t * j)        { return jass_pushnullhandle(j, "transmissionsource"); }
static uint32_t sc2_TransmissionSourceFromModel(jass_t * j){ return jass_pushnullhandle(j, "transmissionsource"); }
static uint32_t sc2_TransmissionSourceFromUnit(jass_t * j) { return jass_pushnullhandle(j, "transmissionsource"); }
/* TransmissionSend: send a transmission to players; sleep if waitUntilDone and duration > 0. */
static uint32_t sc2_TransmissionSend(jass_t * j) {
    /* args: playergroup, source, camerainfo, string anim, soundlink, text speaker,
     *       text msg, fixed duration, int durationType, bool waitUntilDone */
    float sound_dur  = sc2_sound_length(j, 5);
    float dur        = jass_checknumber(j, 8);
    int32_t  dur_type   = jass_checkinteger(j, 9);
    bool  wait_done  = jass_checkboolean(j, 10);
    int32_t  sound_h    = (int32_t)(uintptr_t)jass_checkhandle(j, 5, "soundlink");
    /* Native SC2 derives transmission time from the linked asset before applying the requested modifier. */
    if (dur_type == 0) dur = sound_dur;
    else if (dur_type == 1) dur += sound_dur;
    else if (dur_type == 2) dur = MAX(sound_dur - dur, 0.0f);
    if (sound_h > 0 && sound_h < sc2_gsound_n && sc2_galaxy_on_sound)
        sc2_galaxy_on_sound(sc2_gsounds[sound_h].id, sc2_gsounds[sound_h].asset);
    fprintf(stderr, "TransmissionSend: sound=%s asset=%ld duration=%.2f wait=%d\n",
            sound_h > 0 && sound_h < sc2_gsound_n ? sc2_gsounds[sound_h].id : "(null)",
            sound_h > 0 && sound_h < sc2_gsound_n ? (long)sc2_gsounds[sound_h].asset : -1L, dur, wait_done);
    if (wait_done && dur > 0.0f)
        jass_sleep(j, (uint32_t)(dur * 1000.0f));
    return jass_pushnullhandle(j, "sound");
}
static uint32_t sc2_TransmissionLastSent(jass_t * j)      { return jass_pushinteger(j, 0); }
static uint32_t sc2_TransmissionClear(jass_t * j)         { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TransmissionClearAll(jass_t * j)      { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TransmissionWait(jass_t * j) {
    float secs = jass_checknumber(j, 2);
    if (secs > 0.0f) jass_sleep(j, (uint32_t)(secs * 1000.0f));
    return jass_pushnull(j);
}
static uint32_t sc2_TransmissionSetOption(jass_t * j)     { (void)j; return jass_pushnull(j); }
