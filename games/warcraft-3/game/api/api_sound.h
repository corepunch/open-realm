extern LPPLAYER currentplayer;

DWORD CreateSound(LPJASS j) {
    LPCSTR fileName = jass_checkstring(j, 1);
    BOOL looping = jass_checkboolean(j, 2);
    BOOL is3D = jass_checkboolean(j, 3);
    BOOL stopwhenoutofrange = jass_checkboolean(j, 4);
    LONG fadeInRate = jass_checkinteger(j, 5);
    LONG fadeOutRate = jass_checkinteger(j, 6);
    LPCSTR eaxSetting = jass_checkstring(j, 7);
    (void)eaxSetting;
    API_ALLOC(gsound_t, sound);
    strlcpy(sound->fileName, fileName, sizeof(sound->fileName));
    sound->looping = looping;
    sound->is3D = is3D;
    sound->stopwhenoutofrange = stopwhenoutofrange;
    sound->fadeInRate = fadeInRate;
    sound->fadeOutRate = fadeOutRate;
    sound->soundIndex = gi.SoundIndex(fileName);
    G_JassSoundRuntimeInit(sound);
    return 1;
}
/* Label constructors resolve WC3 sound-data rows into the same game-owned sound
 * descriptor as CreateSound. Playback is sent through the entity/server sound
 * path; JASS never owns or calls the client mixer directly. */
DWORD CreateSoundFilenameWithLabel(LPJASS j) {
    //LPCSTR fileName = jass_checkstring(j, 1);
    //BOOL looping = jass_checkboolean(j, 2);
    //BOOL is3D = jass_checkboolean(j, 3);
    //BOOL stopwhenoutofrange = jass_checkboolean(j, 4);
    //LONG fadeInRate = jass_checkinteger(j, 5);
    //LONG fadeOutRate = jass_checkinteger(j, 6);
    //LPCSTR SLKEntryName = jass_checkstring(j, 7);
    return jass_pushnullhandle(j, "sound");
}
DWORD CreateSoundFromLabel(LPJASS j) {
    //LPCSTR soundLabel = jass_checkstring(j, 1);
    //BOOL looping = jass_checkboolean(j, 2);
    //BOOL is3D = jass_checkboolean(j, 3);
    //BOOL stopwhenoutofrange = jass_checkboolean(j, 4);
    //LONG fadeInRate = jass_checkinteger(j, 5);
    //LONG fadeOutRate = jass_checkinteger(j, 6);
    return jass_pushnullhandle(j, "sound");
}
DWORD CreateMIDISound(LPJASS j) {
    //LPCSTR soundLabel = jass_checkstring(j, 1);
    //LONG fadeInRate = jass_checkinteger(j, 2);
    //LONG fadeOutRate = jass_checkinteger(j, 3);
    return jass_pushnullhandle(j, "sound");
}
DWORD SetSoundParamsFromLabel(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    //LPCSTR soundLabel = jass_checkstring(j, 2);
    return 0;
}
DWORD SetSoundDistanceCutoff(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    //FLOAT cutoff = jass_checknumber(j, 2);
    return 0;
}
DWORD SetSoundChannel(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    //LONG channel = jass_checkinteger(j, 2);
    return 0;
}
DWORD SetSoundVolume(LPJASS j) {
    gsound_t *sound = jass_checkhandle(j, 1, "sound");
    LONG volume = jass_checkinteger(j, 2);
    if (sound) G_JassSoundSetVolume(sound, (FLOAT)MAX(0, MIN(volume, 127)) / 127.0f);
    return 0;
}
DWORD SetSoundPitch(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    //FLOAT pitch = jass_checknumber(j, 2);
    return 0;
}
DWORD SetSoundDistances(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    //FLOAT minDist = jass_checknumber(j, 2);
    //FLOAT maxDist = jass_checknumber(j, 3);
    return 0;
}
DWORD SetSoundConeAngles(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    //FLOAT inside = jass_checknumber(j, 2);
    //FLOAT outside = jass_checknumber(j, 3);
    //LONG outsideVolume = jass_checkinteger(j, 4);
    return 0;
}
DWORD SetSoundConeOrientation(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    //FLOAT x = jass_checknumber(j, 2);
    //FLOAT y = jass_checknumber(j, 3);
    //FLOAT z = jass_checknumber(j, 4);
    return 0;
}
DWORD SetSoundPosition(LPJASS j) {
    gsound_t *sound = jass_checkhandle(j, 1, "sound");
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    FLOAT z = jass_checknumber(j, 4);
    if (sound) G_JassSoundSetPosition(sound, &MAKE(VECTOR3, x, y, z));
    return 0;
}
DWORD SetSoundVelocity(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    //FLOAT x = jass_checknumber(j, 2);
    //FLOAT y = jass_checknumber(j, 3);
    //FLOAT z = jass_checknumber(j, 4);
    return 0;
}
DWORD AttachSoundToUnit(LPJASS j) {
    gsound_t *sound = jass_checkhandle(j, 1, "sound");
    LPEDICT whichUnit = jass_checkhandle(j, 2, "unit");
    if (sound) G_JassSoundAttach(sound, whichUnit);
    return 0;
}
/* StartSound snapshots the current transient sound-handle presentation state
 * into one generic sound packet. Continuous attachment tracking and stop/fade
 * playback lifetime remain separate mixer work. */
DWORD StartSound(LPJASS j) {
    gsound_t *sound = jass_checkhandle(j, 1, "sound");
    jassSoundPlayback_t playback;
    FLOAT attenuation;

    if (!sound || !sound->soundIndex) return 0;
    G_JassSoundPlayback(sound, &playback);
    attenuation = sound->is3D ? 1.0f : 0.0f;

    if (currentplayer) {
        LPEDICT recipient = PLAYER_ENT(currentplayer);
        if (!recipient) return 0;
        if (playback.positioned)
            gi.PositionedSound(&playback.origin, recipient, CHAN_OWNER | CHAN_RELIABLE, sound->soundIndex,
                               playback.volume, attenuation, 0.0f);
        else
            gi.Sound(recipient, CHAN_OWNER | CHAN_RELIABLE, sound->soundIndex,
                     playback.volume, attenuation, 0.0f);
        return 0;
    }

    if (playback.positioned)
        gi.PositionedSound(&playback.origin, playback.emitter, CHAN_RELIABLE, sound->soundIndex,
                           playback.volume, attenuation, 0.0f);
    else
        gi.Sound(NULL, CHAN_RELIABLE, sound->soundIndex, playback.volume, attenuation, 0.0f);
    return 0;
}
DWORD StopSound(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    //BOOL killWhenDone = jass_checkboolean(j, 2);
    //BOOL fadeOut = jass_checkboolean(j, 3);
    return 0;
}
DWORD KillSoundWhenDone(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    return 0;
}
DWORD SetMusicVolume(LPJASS j) {
    //LONG volume = jass_checkinteger(j, 1);
    return 0;
}
DWORD PlayMusic(LPJASS j) {
    //LPCSTR musicName = jass_checkstring(j, 1);
    return 0;
}
DWORD SetMapMusic(LPJASS j) {
    //LPCSTR musicName = jass_checkstring(j, 1);
    //BOOL random = jass_checkboolean(j, 2);
    //LONG index = jass_checkinteger(j, 3);
    return 0;
}
DWORD ClearMapMusic(LPJASS j) {
    return 0;
}
DWORD PlayThematicMusic(LPJASS j) {
    //LPCSTR musicFileName = jass_checkstring(j, 1);
    return 0;
}
DWORD EndThematicMusic(LPJASS j) {
    return 0;
}
DWORD StopMusic(LPJASS j) {
    //BOOL fadeOut = jass_checkboolean(j, 1);
    return 0;
}
DWORD ResumeMusic(LPJASS j) {
    return 0;
}
DWORD SetSoundDuration(LPJASS j) {
    gsound_t *soundHandle = jass_checkhandle(j, 1, "sound");
    soundHandle->duration = jass_checkinteger(j, 2);
    return 0;
}
DWORD GetSoundDuration(LPJASS j) {
    gsound_t *soundHandle = jass_checkhandle(j, 1, "sound");
    return jass_pushinteger(j, soundHandle->duration);
}
DWORD GetSoundFileDuration(LPJASS j) {
    //LPCSTR musicFileName = jass_checkstring(j, 1);
    return jass_pushinteger(j, 0);
}
DWORD VolumeGroupSetVolume(LPJASS j) {
    //HANDLE vgroup = jass_checkhandle(j, 1, "volumegroup");
    //FLOAT scale = jass_checknumber(j, 2);
    return 0;
}
DWORD VolumeGroupReset(LPJASS j) {
    return 0;
}
DWORD GetSoundIsPlaying(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    return jass_pushboolean(j, 0);
}
DWORD GetSoundIsLoading(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    return jass_pushboolean(j, 0);
}
DWORD RegisterStackedSound(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    //BOOL byPosition = jass_checkboolean(j, 2);
    //FLOAT rectwidth = jass_checknumber(j, 3);
    //FLOAT rectheight = jass_checknumber(j, 4);
    return 0;
}
DWORD UnregisterStackedSound(LPJASS j) {
    //HANDLE soundHandle = jass_checkhandle(j, 1, "sound");
    //BOOL byPosition = jass_checkboolean(j, 2);
    //FLOAT rectwidth = jass_checknumber(j, 3);
    //FLOAT rectheight = jass_checknumber(j, 4);
    return 0;
}
