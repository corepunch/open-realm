/* Exercise the production streaming backend with generated audio, without retail archives. */
#define BZ_MUSIC_MP3
#include "client/cl_music.c"
#include "shared/test.h"

static uint32_t queued, total;
static bool nonzero, stereo_difference;

int Cvar_Integer(cstring_t name, int fallback) { (void)name; return fallback; }
float Cvar_Value(cstring_t name, float fallback) { (void)name; return fallback; }
Uint32 SDL_GetTicks(void) { return 0; }
void S_StreamStart(sStreamId_t stream) { (void)stream; queued = 0; }
void S_StreamStop(sStreamId_t stream) { (void)stream; queued = 0; }
void S_StreamSetVolume(sStreamId_t stream, float volume) { (void)stream; (void)volume; }
void S_StreamSetPaused(sStreamId_t stream, bool paused) { (void)stream; (void)paused; }
uint32_t S_StreamBufferedFrames(sStreamId_t stream) { (void)stream; return queued; }
uint32_t S_StreamSamples(sStreamId_t stream, int16_t const *pcm, uint32_t frames) {
    (void)stream;
    for (uint32_t i = 0; i < frames; i++) {
        nonzero |= pcm[i * 2] != 0 || pcm[i * 2 + 1] != 0;
        stereo_difference |= pcm[i * 2] != pcm[i * 2 + 1];
    }
    queued += frames;
    total += frames;
    return frames;
}
handle_t FS_ReadFile(cstring_t path, uint32_t *size) {
    FILE *file = fopen(path, "rb");
    uint8_t *data;
    long length;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    rewind(file);
    data = malloc(length);
    *size = (uint32_t)length;
    if (fread(data, 1, length, file) != (size_t)length) { free(data); data = NULL; }
    fclose(file);
    return data;
}
void FS_FreeFile(void *data) { free(data); }

TEST(music_mp3, stereo_stream_seek_pause_and_eof) {
    uint32_t full_length, offset;
    memset(&cl_music, 0, sizeof(cl_music));
    nonzero = stereo_difference = false;
    total = 0;
    T_ASSERT(CL_MusicOpenTrack("tests/resources/sound-test.mp3", 0));
    while (!cl_music.decoder_eof) { queued = 0; T_ASSERT(CL_MusicPumpDecoder()); }
    full_length = total;
    T_ASSERT(nonzero);
    T_ASSERT(stereo_difference);
    /* MP3 includes encoder delay/padding around the half-second authored signal. */
    T_ASSERT(full_length > 20000 && full_length < 30000);

    T_ASSERT(CL_MusicSeekDecoder(250));
    total = 0;
    cl_music.paused = true;
    offset = cl_music.mp3_offset;
    T_ASSERT(CL_MusicPumpDecoder());
    T_EQ(cl_music.mp3_offset, offset);
    T_EQ(queued, 0);
    cl_music.paused = false;
    while (!cl_music.decoder_eof) { queued = 0; T_ASSERT(CL_MusicPumpDecoder()); }
    T_EQ(total + 11025, full_length);
    CL_MusicCloseDecoder();
    T_ASSERT(!cl_music.mp3_data && !cl_music.decoder_active);
}

TEST(music_mp3, reject_non_audio_and_missing_source) {
    T_ASSERT(!CL_MusicOpenTrack("tests/test_music_mp3.c", 0));
    T_ASSERT(!cl_music.mp3_data && !cl_music.decoder_active);
    T_ASSERT(!CL_MusicOpenTrack("missing-music.mp3", 0));
}
