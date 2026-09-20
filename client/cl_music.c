/*
 * cl_music.c — client-owned long-form background music.
 *
 * Game modules send reliable presentation commands and already resolve any
 * game-specific skin/metadata aliases.  The client owns playlist lifetime,
 * optional FFmpeg decoding, seeking, fades and the generic music PCM stream.
 */
#include "client.h"
#include "sound/s_local.h"

#ifdef BZ_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#endif

#define CL_MUSIC_MAX_TRACKS 32
#define CL_MUSIC_PLAYLIST_MAX 2048
#define CL_MUSIC_AUDIO_TARGET_FRAMES 22050
#define CL_MUSIC_STOP_FADE_MS 2000

typedef enum {
    CL_MUSIC_SOURCE_NONE,
    CL_MUSIC_SOURCE_MAP,
    CL_MUSIC_SOURCE_EXPLICIT,
    CL_MUSIC_SOURCE_THEMATIC,
    CL_MUSIC_SOURCE_MENU
} clMusicSource_t;

typedef enum {
    CL_MUSIC_FADE_NONE,
    CL_MUSIC_FADE_IN,
    CL_MUSIC_FADE_OUT
} clMusicFade_t;

typedef struct {
    char paths[CL_MUSIC_MAX_TRACKS][MAX_PATHLEN];
    DWORD count;
    BOOL random; /* randomizes only the initial track; progression is sequential */
    DWORD index;
    DWORD played_mask; /* explicit one-pass playlists; max 32 tracks */
} clMusicPlaylist_t;

typedef struct {
    char playlist[CL_MUSIC_PLAYLIST_MAX];
    BOOL random;
    LONG index;
    DWORD session_id;
} clMapMusic_t;

typedef struct {
    clMusicPlaylist_t playlist;
    clMusicSource_t source;
    LONG position_ms;
    DWORD session_id;
    BOOL paused;
    BOOL valid;
} clMusicRestore_t;

typedef struct {
    clMapMusic_t map;
    clMusicPlaylist_t current;
    clMusicRestore_t thematic_restore;
    BOOL map_pending;
    clMusicSource_t source;
    DWORD current_session_id;
    LONG position_base_ms;
    LONG music_volume;
    LONG thematic_volume;
    BOOL paused;
    BOOL suspended;
    BOOL decoder_active;
    DWORD fade_start_ticks;
    DWORD fade_clock_ticks;
    DWORD fade_duration_ms;
    FLOAT fade_start_factor;
    clMusicFade_t fade;
#ifdef BZ_FFMPEG
    BOOL extracted;
    PATHSTR source_path;
    PATHSTR disk_path;
    AVFormatContext *format;
    AVCodecContext *codec;
    AVPacket *packet;
    AVFrame *frame;
    SwrContext *swr;
    int audio_stream;
    BOOL demux_eof;
    BOOL decoder_flushed;
    BOOL decoder_eof;
    BYTE *audio_buffer;
    unsigned int audio_buffer_size;
#endif
} clMusicState_t;

static clMusicState_t cl_music;
static void CL_MusicSendFinished(DWORD session_id);
static void CL_MusicSendSelected(DWORD session_id, DWORD index, LONG position_ms);
static void CL_MusicSendThematicSnapshot(DWORD thematic_session_id);

static BOOL CL_MusicUserEnabled(void) {
    return Cvar_Integer("s_music", 1) != 0;
}

static FLOAT CL_MusicUserVolume(void) {
    return MAX(0.0f, MIN(Cvar_Value("s_musicvolume", 1.0f), 1.0f));
}

static BOOL CL_MusicShouldPause(void) {
    return cl_music.paused || cl_music.suspended || !CL_MusicUserEnabled();
}

static FLOAT CL_MusicTargetVolume(void) {
    LONG volume = cl_music.source == CL_MUSIC_SOURCE_THEMATIC
        ? cl_music.thematic_volume
        : cl_music.music_volume;
    return ((FLOAT)MAX(0, MIN(volume, 127)) / 127.0f) * CL_MusicUserVolume();
}

static FLOAT CL_MusicFadeFactor(void) {
    DWORD elapsed;
    FLOAT fraction;

    if (cl_music.fade == CL_MUSIC_FADE_NONE || !cl_music.fade_duration_ms) return 1.0f;
    elapsed = SDL_GetTicks() - cl_music.fade_start_ticks;
    fraction = MIN(1.0f, (FLOAT)elapsed / (FLOAT)cl_music.fade_duration_ms);
    if (cl_music.fade == CL_MUSIC_FADE_OUT)
        return MAX(0.0f, cl_music.fade_start_factor * (1.0f - fraction));
    return fraction;
}

/* Keep wall-clock fade time from advancing while movie suspension or the user music toggle pauses the stream. */
static void CL_MusicFreezeFade(void) {
    DWORD now = SDL_GetTicks();

    if (cl_music.fade != CL_MUSIC_FADE_NONE)
        cl_music.fade_start_ticks += now - cl_music.fade_clock_ticks;
    cl_music.fade_clock_ticks = now;
}

static void CL_MusicApplyVolume(void) {
    FLOAT volume = CL_MusicTargetVolume() * CL_MusicFadeFactor();
    BOOL finish_stop = false;

    if (cl_music.fade != CL_MUSIC_FADE_NONE && cl_music.fade_duration_ms &&
        SDL_GetTicks() - cl_music.fade_start_ticks >= cl_music.fade_duration_ms) {
        finish_stop = cl_music.fade == CL_MUSIC_FADE_OUT;
        cl_music.fade = CL_MUSIC_FADE_NONE;
        cl_music.fade_duration_ms = 0;
        if (!finish_stop) volume = CL_MusicTargetVolume();
        else volume = 0.0f;
    }
    S_StreamSetVolume(S_STREAM_MUSIC, volume);
    if (finish_stop) {
        cl_music.paused = true;
        S_StreamSetPaused(S_STREAM_MUSIC, true);
    }
}

static LONG CL_MusicCurrentPositionMS(void) {
    uint64_t millis = (uint64_t)MAX(0, cl_music.position_base_ms);

    millis += S_StreamPlayedFrames(S_STREAM_MUSIC) * 1000u / 44100u;
    return (LONG)(millis > 0x7fffffffu ? 0x7fffffffu : millis);
}

static void CL_MusicTrimPath(LPCSTR start, size_t length, LPSTR out, size_t out_size) {
    LPCSTR end = start + length;

    while (start < end && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    if (!out || !out_size) return;
    snprintf(out, out_size, "%.*s", (int)MIN((size_t)(end - start), out_size - 1), start);
}

static DWORD CL_MusicParsePlaylist(LPCSTR value, clMusicPlaylist_t *playlist) {
    LPCSTR cursor;

    if (!playlist) return 0;
    memset(playlist, 0, sizeof(*playlist));
    if (!value) return 0;

    cursor = value;
    while (*cursor && playlist->count < CL_MUSIC_MAX_TRACKS) {
        LPCSTR semi = strchr(cursor, ';');
        LPCSTR comma = strchr(cursor, ',');
        LPCSTR separator = NULL;
        size_t length;

        if (semi && comma) separator = semi < comma ? semi : comma;
        else separator = semi ? semi : comma;
        length = separator ? (size_t)(separator - cursor) : strlen(cursor);
        CL_MusicTrimPath(cursor, length, playlist->paths[playlist->count],
                         sizeof(playlist->paths[playlist->count]));
        if (playlist->paths[playlist->count][0]) playlist->count++;
        if (!separator) break;
        cursor = separator + 1;
    }
    return playlist->count;
}

#ifdef BZ_FFMPEG
static void CL_MusicCloseDecoder(void) {
    S_StreamStop(S_STREAM_MUSIC);
    swr_free(&cl_music.swr);
    av_freep(&cl_music.audio_buffer);
    cl_music.audio_buffer_size = 0;
    av_frame_free(&cl_music.frame);
    av_packet_free(&cl_music.packet);
    avcodec_free_context(&cl_music.codec);
    avformat_close_input(&cl_music.format);
    if (cl_music.extracted && cl_music.disk_path[0]) remove(cl_music.disk_path);
    cl_music.extracted = false;
    cl_music.disk_path[0] = '\0';
    cl_music.source_path[0] = '\0';
    cl_music.audio_stream = -1;
    cl_music.demux_eof = false;
    cl_music.decoder_flushed = false;
    cl_music.decoder_eof = false;
    cl_music.decoder_active = false;
}

static BOOL CL_MusicResolveDiskPath(LPCSTR path) {
    if (FS_ResolveLoosePath(path, cl_music.disk_path, sizeof(cl_music.disk_path))) return true;

    FS_UserPath("openrealm-music.tmp", cl_music.disk_path, sizeof(cl_music.disk_path));
    remove(cl_music.disk_path);
    if (!FS_ExtractFile(path, cl_music.disk_path)) {
        cl_music.disk_path[0] = '\0';
        return false;
    }
    cl_music.extracted = true;
    return true;
}

static BOOL CL_MusicOpenCodec(void) {
    AVStream *stream;
    AVCodec const *decoder;
    AVCodecContext *codec;
    int audio_stream;

    audio_stream = av_find_best_stream(cl_music.format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (audio_stream < 0 || !decoder) return false;
    stream = cl_music.format->streams[audio_stream];
    codec = avcodec_alloc_context3(decoder);
    if (!codec) return false;
    if (avcodec_parameters_to_context(codec, stream->codecpar) < 0 || avcodec_open2(codec, decoder, NULL) < 0) {
        avcodec_free_context(&codec);
        return false;
    }
    cl_music.codec = codec;
    cl_music.audio_stream = audio_stream;
    return true;
}

static BOOL CL_MusicEnsureConversion(AVFrame const *frame) {
    AVChannelLayout output_layout = AV_CHANNEL_LAYOUT_STEREO;
    int result;

    if (cl_music.swr) return true;
    if (frame->sample_rate <= 0 || frame->ch_layout.nb_channels <= 0) return false;
    result = swr_alloc_set_opts2(&cl_music.swr,
                                 &output_layout,
                                 AV_SAMPLE_FMT_S16,
                                 44100,
                                 &frame->ch_layout,
                                 (enum AVSampleFormat)frame->format,
                                 frame->sample_rate,
                                 0,
                                 NULL);
    if (result < 0 || !cl_music.swr || swr_init(cl_music.swr) < 0) {
        swr_free(&cl_music.swr);
        return false;
    }
    return true;
}

static BOOL CL_MusicQueueFrame(AVFrame const *frame) {
    int output_frames;
    int required_bytes;
    int converted;
    BYTE *out;

    if (!CL_MusicEnsureConversion(frame)) return false;
    output_frames = (int)av_rescale_rnd(swr_get_delay(cl_music.swr, frame->sample_rate) + frame->nb_samples,
                                        44100, frame->sample_rate, AV_ROUND_UP);
    required_bytes = av_samples_get_buffer_size(NULL, 2, output_frames, AV_SAMPLE_FMT_S16, 1);
    if (required_bytes <= 0) return false;
    av_fast_malloc(&cl_music.audio_buffer, &cl_music.audio_buffer_size, (size_t)required_bytes);
    if (!cl_music.audio_buffer) return false;
    out = cl_music.audio_buffer;
    converted = swr_convert(cl_music.swr,
                            &out,
                            output_frames,
                            (uint8_t const **)frame->extended_data,
                            frame->nb_samples);
    if (converted < 0) return false;
    return S_StreamSamples(S_STREAM_MUSIC, (SHORT const *)cl_music.audio_buffer, (DWORD)converted) == (DWORD)converted;
}

static BOOL CL_MusicDrainDecoder(void) {
    while (S_StreamBufferedFrames(S_STREAM_MUSIC) < CL_MUSIC_AUDIO_TARGET_FRAMES) {
        int result = avcodec_receive_frame(cl_music.codec, cl_music.frame);
        if (result == AVERROR(EAGAIN)) return true;
        if (result == AVERROR_EOF) {
            cl_music.decoder_eof = true;
            return true;
        }
        if (result < 0) return false;
        if (!CL_MusicQueueFrame(cl_music.frame)) return false;
        av_frame_unref(cl_music.frame);
    }
    return true;
}

static BOOL CL_MusicPumpDecoder(void) {
    int guard = 0;

    if (!cl_music.decoder_active || cl_music.paused || cl_music.suspended) return true;
    if (!CL_MusicDrainDecoder()) return false;
    while (!cl_music.demux_eof &&
           S_StreamBufferedFrames(S_STREAM_MUSIC) < CL_MUSIC_AUDIO_TARGET_FRAMES && guard++ < 128) {
        int result = av_read_frame(cl_music.format, cl_music.packet);
        if (result < 0) {
            cl_music.demux_eof = true;
            break;
        }
        if (cl_music.packet->stream_index == cl_music.audio_stream) {
            result = avcodec_send_packet(cl_music.codec, cl_music.packet);
            if (result == AVERROR(EAGAIN)) {
                if (!CL_MusicDrainDecoder()) { av_packet_unref(cl_music.packet); return false; }
                result = avcodec_send_packet(cl_music.codec, cl_music.packet);
            }
            av_packet_unref(cl_music.packet);
            if (result < 0) return false;
            if (!CL_MusicDrainDecoder()) return false;
        } else {
            av_packet_unref(cl_music.packet);
        }
    }
    if (cl_music.demux_eof && !cl_music.decoder_flushed) {
        int result = avcodec_send_packet(cl_music.codec, NULL);
        if (result >= 0 || result == AVERROR_EOF) cl_music.decoder_flushed = true;
        else if (result != AVERROR(EAGAIN)) return false;
    }
    return CL_MusicDrainDecoder();
}

static BOOL CL_MusicSeekDecoder(LONG millisecs) {
    AVStream *stream;
    int64_t target;

    if (!cl_music.decoder_active || !cl_music.format || cl_music.audio_stream < 0) return false;
    stream = cl_music.format->streams[cl_music.audio_stream];
    target = av_rescale_q(MAX(0, millisecs), (AVRational){1, 1000}, stream->time_base);
    if (avformat_seek_file(cl_music.format, cl_music.audio_stream, INT64_MIN, target, INT64_MAX,
                           AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(cl_music.codec);
    av_packet_unref(cl_music.packet);
    av_frame_unref(cl_music.frame);
    swr_free(&cl_music.swr);
    cl_music.demux_eof = false;
    cl_music.decoder_flushed = false;
    cl_music.decoder_eof = false;
    S_StreamStart(S_STREAM_MUSIC);
    cl_music.position_base_ms = MAX(0, millisecs);
    CL_MusicApplyVolume();
    S_StreamSetPaused(S_STREAM_MUSIC, CL_MusicShouldPause());
    return true;
}

static BOOL CL_MusicOpenTrack(LPCSTR path, LONG start_ms) {
    if (!path || !*path) return false;
    CL_MusicCloseDecoder();
    snprintf(cl_music.source_path, sizeof(cl_music.source_path), "%s", path);
    cl_music.audio_stream = -1;
    if (!CL_MusicResolveDiskPath(path)) { CL_MusicCloseDecoder(); return false; }

    av_log_set_level(AV_LOG_ERROR);
    if (avformat_open_input(&cl_music.format, cl_music.disk_path, NULL, NULL) < 0 ||
        avformat_find_stream_info(cl_music.format, NULL) < 0 ||
        !CL_MusicOpenCodec()) {
        CL_MusicCloseDecoder();
        return false;
    }
    cl_music.packet = av_packet_alloc();
    cl_music.frame = av_frame_alloc();
    if (!cl_music.packet || !cl_music.frame) { CL_MusicCloseDecoder(); return false; }

    S_StreamStart(S_STREAM_MUSIC);
    cl_music.position_base_ms = 0;
    cl_music.decoder_active = true;
    if (cl_music.fade != CL_MUSIC_FADE_NONE) {
        cl_music.fade_start_ticks = SDL_GetTicks();
        cl_music.fade_clock_ticks = cl_music.fade_start_ticks;
    }
    CL_MusicApplyVolume();
    S_StreamSetPaused(S_STREAM_MUSIC, CL_MusicShouldPause());
    if (start_ms > 0 && !CL_MusicSeekDecoder(start_ms)) { CL_MusicCloseDecoder(); return false; }
    if (!CL_MusicPumpDecoder()) { CL_MusicCloseDecoder(); return false; }
    return true;
}
#else
static void CL_MusicCloseDecoder(void) {
    S_StreamStop(S_STREAM_MUSIC);
    cl_music.decoder_active = false;
}
static BOOL CL_MusicSeekDecoder(LONG millisecs) { (void)millisecs; return false; }
static BOOL CL_MusicOpenTrack(LPCSTR path, LONG start_ms) { (void)path; (void)start_ms; return false; }
static BOOL CL_MusicPumpDecoder(void) { return true; }
#endif

static DWORD CL_MusicInitialIndex(clMusicPlaylist_t const *playlist) {
    if (!playlist || !playlist->count) return 0;
    if (playlist->random) return (DWORD)(rand() % playlist->count);
    return playlist->index < playlist->count ? playlist->index : 0;
}

static BOOL CL_MusicStartAvailableTrack(DWORD preferred, LONG start_ms, BOOL skip_played, BOOL notify_selected) {
    DWORD count = cl_music.current.count;

    if (!count) return false;
    for (DWORD attempt = 0; attempt < count; attempt++) {
        DWORD index = (preferred + attempt) % count;
        DWORD bit = 1u << index;

        if (skip_played && (cl_music.current.played_mask & bit)) continue;
        if (CL_MusicOpenTrack(cl_music.current.paths[index], attempt == 0 ? start_ms : 0)) {
            cl_music.current.index = index;
            cl_music.current.played_mask |= bit;
            cl_music.current.random = false;
            if (notify_selected)
                CL_MusicSendSelected(cl_music.current_session_id, index, CL_MusicCurrentPositionMS());
            return true;
        }
    }
    return false;
}

static BOOL CL_MusicStartPlaylist(LPCSTR value, BOOL random, LONG index, clMusicSource_t source,
                                  LONG start_ms, LONG fade_ms, DWORD played_mask,
                                  DWORD session_id, BOOL notify_selected) {
    DWORD initial;

    CL_MusicCloseDecoder();
    cl_music.source = source;
    cl_music.current_session_id = session_id;
    cl_music.paused = false;
    cl_music.fade = fade_ms > 0 ? CL_MUSIC_FADE_IN : CL_MUSIC_FADE_NONE;
    cl_music.fade_start_ticks = SDL_GetTicks();
    cl_music.fade_clock_ticks = cl_music.fade_start_ticks;
    cl_music.fade_duration_ms = (DWORD)MAX(0, fade_ms);
    cl_music.fade_start_factor = 1.0f;
    if (!CL_MusicParsePlaylist(value, &cl_music.current)) return false;
    cl_music.current.random = random;
    cl_music.current.index = index >= 0 ? (DWORD)index : 0;
    cl_music.current.played_mask = played_mask;
    initial = CL_MusicInitialIndex(&cl_music.current);
    return CL_MusicStartAvailableTrack(initial, MAX(0, start_ms), false, notify_selected);
}

static void CL_MusicRememberThematicRestore(void) {
    if (cl_music.source == CL_MUSIC_SOURCE_NONE || cl_music.source == CL_MUSIC_SOURCE_THEMATIC ||
        !cl_music.current.count || !cl_music.current_session_id) {
        memset(&cl_music.thematic_restore, 0, sizeof(cl_music.thematic_restore));
        return;
    }
    cl_music.thematic_restore.playlist = cl_music.current;
    cl_music.thematic_restore.source = cl_music.source;
    cl_music.thematic_restore.position_ms = CL_MusicCurrentPositionMS();
    cl_music.thematic_restore.session_id = cl_music.current_session_id;
    cl_music.thematic_restore.paused = cl_music.paused;
    cl_music.thematic_restore.valid = true;
}

static void CL_MusicNotifyCurrentSelected(void) {
    if (cl_music.source == CL_MUSIC_SOURCE_NONE || !cl_music.current_session_id || !cl_music.current.count) return;
    CL_MusicSendSelected(cl_music.current_session_id, cl_music.current.index, CL_MusicCurrentPositionMS());
}

static void CL_MusicFinishThematic(BOOL notify_server) {
    clMusicRestore_t restore;
    DWORD const finished_session_id = cl_music.current_session_id;
    BOOL restored = false;

    if (cl_music.source != CL_MUSIC_SOURCE_THEMATIC) return;
    restore = cl_music.thematic_restore;
    memset(&cl_music.thematic_restore, 0, sizeof(cl_music.thematic_restore));

    if (restore.valid && restore.source != CL_MUSIC_SOURCE_NONE && restore.playlist.count) {
        CL_MusicCloseDecoder();
        cl_music.current = restore.playlist;
        cl_music.source = restore.source;
        cl_music.current_session_id = restore.session_id;
        cl_music.paused = restore.paused;
        cl_music.fade = CL_MUSIC_FADE_NONE;
        cl_music.fade_duration_ms = 0;
        restored = CL_MusicStartAvailableTrack(cl_music.current.index, restore.position_ms, false, false);
        if (restored)
            S_StreamSetPaused(S_STREAM_MUSIC, CL_MusicShouldPause());
    }

    if (!restored) {
        cl_music.map_pending = false;
        if (cl_music.map.playlist[0]) {
            restored = CL_MusicStartPlaylist(cl_music.map.playlist, cl_music.map.random, cl_music.map.index,
                                             CL_MUSIC_SOURCE_MAP, 0, 0, 0, cl_music.map.session_id, false);
        }
        if (!restored) {
            CL_MusicCloseDecoder();
            memset(&cl_music.current, 0, sizeof(cl_music.current));
            cl_music.source = CL_MUSIC_SOURCE_NONE;
            cl_music.current_session_id = 0;
            cl_music.paused = false;
        }
    }

    if (notify_server) {
        CL_MusicSendFinished(finished_session_id);
        CL_MusicNotifyCurrentSelected();
    }
}

static void CL_MusicFinishExplicit(void) {
    DWORD const finished_session_id = cl_music.current_session_id;
    BOOL started = false;

    if (cl_music.source != CL_MUSIC_SOURCE_EXPLICIT) return;
    cl_music.map_pending = false;
    if (cl_music.map.playlist[0]) {
        started = CL_MusicStartPlaylist(cl_music.map.playlist, cl_music.map.random, cl_music.map.index,
                                        CL_MUSIC_SOURCE_MAP, 0, 0, 0, cl_music.map.session_id, false);
    }
    if (!started) {
        CL_MusicCloseDecoder();
        memset(&cl_music.current, 0, sizeof(cl_music.current));
        cl_music.source = CL_MUSIC_SOURCE_NONE;
        cl_music.current_session_id = 0;
        cl_music.paused = false;
    }
    CL_MusicSendFinished(finished_session_id);
    CL_MusicNotifyCurrentSelected();
}

static void CL_MusicAdvancePlaylist(void) {
    DWORD preferred;

    if (cl_music.source == CL_MUSIC_SOURCE_THEMATIC) {
        /* Warcraft thematic music is one-shot: natural EOF restores the
         * interrupted ordinary session just like EndThematicMusic(). */
        CL_MusicFinishThematic(true);
        return;
    }
    if (cl_music.source == CL_MUSIC_SOURCE_MAP && cl_music.map_pending) {
        DWORD const finished_session_id = cl_music.current_session_id;
        BOOL started = false;

        cl_music.map_pending = false;
        if (cl_music.map.playlist[0]) {
            started = CL_MusicStartPlaylist(cl_music.map.playlist, cl_music.map.random, cl_music.map.index,
                                            CL_MUSIC_SOURCE_MAP, 0, 0, 0, cl_music.map.session_id, false);
        }
        if (!started) {
            CL_MusicCloseDecoder();
            memset(&cl_music.current, 0, sizeof(cl_music.current));
            cl_music.source = CL_MUSIC_SOURCE_NONE;
            cl_music.current_session_id = 0;
        }
        CL_MusicSendFinished(finished_session_id);
        CL_MusicNotifyCurrentSelected();
        return;
    }
    if (!cl_music.current.count) { CL_MusicCloseDecoder(); return; }

    preferred = (cl_music.current.index + 1) % cl_music.current.count;
    cl_music.fade = CL_MUSIC_FADE_NONE;
    if (cl_music.source == CL_MUSIC_SOURCE_EXPLICIT) {
        if (!CL_MusicStartAvailableTrack(preferred, 0, true, true)) CL_MusicFinishExplicit();
        return;
    }

    CL_MusicStartAvailableTrack(preferred, 0, false, true);
}

static void CL_MusicSendFinished(DWORD session_id) {
    if (cls.state <= ca_connected || !session_id) return;
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "music_finished %u", (unsigned)session_id);
}

static void CL_MusicSendSelected(DWORD session_id, DWORD index, LONG position_ms) {
    if (cls.state <= ca_connected || !session_id) return;
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "music_selected %u %u %ld %u",
              (unsigned)session_id, (unsigned)index, (long)MAX(0, position_ms),
              (unsigned)cl_music.current.played_mask);
}

static void CL_MusicSendThematicSnapshot(DWORD thematic_session_id) {
    clMusicRestore_t const *restore = &cl_music.thematic_restore;

    if (cls.state <= ca_connected || !thematic_session_id || !restore->valid || !restore->session_id) return;
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "music_snapshot %u %u %u %ld %u",
              (unsigned)thematic_session_id, (unsigned)restore->session_id,
              (unsigned)restore->playlist.index, (long)MAX(0, restore->position_ms),
              (unsigned)restore->playlist.played_mask);
}

void CL_MusicInit(void) {
    Cvar_Get("s_music", "1", CVAR_ARCHIVE);
    Cvar_Get("s_musicvolume", "1", CVAR_ARCHIVE);
    memset(&cl_music, 0, sizeof(cl_music));
    cl_music.music_volume = 127;
    cl_music.thematic_volume = 127;
#ifdef BZ_FFMPEG
    cl_music.audio_stream = -1;
#endif
}

void CL_MusicReset(void) {
    LONG music_volume = cl_music.music_volume;
    LONG thematic_volume = cl_music.thematic_volume;
    CL_MusicCloseDecoder();
    memset(&cl_music, 0, sizeof(cl_music));
    cl_music.music_volume = music_volume;
    cl_music.thematic_volume = thematic_volume;
#ifdef BZ_FFMPEG
    cl_music.audio_stream = -1;
#endif
}

void CL_MusicShutdown(void) {
    CL_MusicCloseDecoder();
    memset(&cl_music, 0, sizeof(cl_music));
}

void CL_MusicPlayMenu(LPCSTR playlist) {
    clMusicPlaylist_t parsed;

    if (!playlist || !*playlist) { CL_MusicStopMenu(); return; }
    if (cl_music.source == CL_MUSIC_SOURCE_MENU &&
        CL_MusicParsePlaylist(playlist, &parsed) && parsed.count == cl_music.current.count) {
        BOOL same = true;
        FOR_LOOP(i, parsed.count)
            if (strcmp(parsed.paths[i], cl_music.current.paths[i])) { same = false; break; }
        if (same) return;
    }
    memset(&cl_music.thematic_restore, 0, sizeof(cl_music.thematic_restore));
    cl_music.map_pending = false;
    CL_MusicStartPlaylist(playlist, false, 0, CL_MUSIC_SOURCE_MENU, 0, 0, 0, 0, false);
}

void CL_MusicStopMenu(void) {
    if (cl_music.source != CL_MUSIC_SOURCE_MENU) return;
    CL_MusicCloseDecoder();
    memset(&cl_music.current, 0, sizeof(cl_music.current));
    cl_music.source = CL_MUSIC_SOURCE_NONE;
    cl_music.current_session_id = 0;
    cl_music.paused = false;
    cl_music.fade = CL_MUSIC_FADE_NONE;
    cl_music.fade_duration_ms = 0;
}

void CL_MusicSetMap(LPCSTR playlist, BOOL random, LONG index, DWORD session_id) {
    DWORD previous_session_id = cl_music.map.session_id;

    if (!playlist) playlist = "";
    index = MAX(0, index);
    strlcpy(cl_music.map.playlist, playlist, sizeof(cl_music.map.playlist));
    cl_music.map.random = random;
    cl_music.map.index = index;
    cl_music.map.session_id = session_id;

    if (cl_music.source == CL_MUSIC_SOURCE_NONE || cl_music.source == CL_MUSIC_SOURCE_MENU) {
        BOOL replacing_silent = cl_music.source == CL_MUSIC_SOURCE_NONE &&
                                previous_session_id && previous_session_id != session_id;

        cl_music.map_pending = false;
        if (!CL_MusicStartPlaylist(cl_music.map.playlist, cl_music.map.random, cl_music.map.index,
                                   CL_MUSIC_SOURCE_MAP, 0, 0, 0, cl_music.map.session_id,
                                   !replacing_silent)) {
            CL_MusicCloseDecoder();
            memset(&cl_music.current, 0, sizeof(cl_music.current));
            cl_music.source = CL_MUSIC_SOURCE_NONE;
            cl_music.current_session_id = 0;
        }
        /* A previously configured map list may already be silent because none
         * of its files decoded.  There will be no EOF to retire that session. */
        if (replacing_silent) {
            CL_MusicSendFinished(previous_session_id);
            CL_MusicNotifyCurrentSelected();
        }
    } else if (cl_music.source == CL_MUSIC_SOURCE_MAP) {
        /* The same session id may update the persistent map-list policy after
         * sync selected an exact current track.  Only a new session is pending. */
        cl_music.map_pending = session_id && session_id != cl_music.current_session_id;
    } else if (cl_music.source == CL_MUSIC_SOURCE_THEMATIC &&
               cl_music.thematic_restore.valid &&
               cl_music.thematic_restore.source == CL_MUSIC_SOURCE_MAP) {
        cl_music.map_pending = session_id && session_id != cl_music.thematic_restore.session_id;
    }
}

void CL_MusicClearMap(void) {
    BOOL had_map = cl_music.map.playlist[0] != '\0';
    DWORD previous_session_id = cl_music.map.session_id;

    memset(&cl_music.map, 0, sizeof(cl_music.map));
    if (had_map && cl_music.source == CL_MUSIC_SOURCE_NONE && previous_session_id) {
        /* An all-invalid map playlist has no future EOF, so ClearMapMusic can
         * retire its retained server session immediately. */
        CL_MusicSendFinished(previous_session_id);
    } else if (had_map &&
        (cl_music.source == CL_MUSIC_SOURCE_MAP ||
         (cl_music.source == CL_MUSIC_SOURCE_THEMATIC &&
          cl_music.thematic_restore.valid &&
          cl_music.thematic_restore.source == CL_MUSIC_SOURCE_MAP))) {
        /* The audible map track finishes before ClearMapMusic becomes silence. */
        cl_music.map_pending = true;
    }
}

void CL_MusicPlay(LPCSTR playlist, BOOL random, LONG index, LONG start_ms, LONG fade_ms,
                  DWORD played_mask, DWORD session_id) {
    memset(&cl_music.thematic_restore, 0, sizeof(cl_music.thematic_restore));
    cl_music.map_pending = false;
    if (!CL_MusicStartPlaylist(playlist, random, index, CL_MUSIC_SOURCE_EXPLICIT,
                               start_ms, fade_ms, played_mask, session_id, true))
        CL_MusicFinishExplicit();
}

void CL_MusicStop(BOOL fade_out) {
    FLOAT start_factor;

    if (cl_music.source == CL_MUSIC_SOURCE_NONE) return;
    if (fade_out && cl_music.fade == CL_MUSIC_FADE_OUT) return;
    if (!fade_out || cl_music.paused || cl_music.suspended || !cl_music.decoder_active) {
        cl_music.fade = CL_MUSIC_FADE_NONE;
        cl_music.fade_duration_ms = 0;
        cl_music.paused = true;
        S_StreamSetPaused(S_STREAM_MUSIC, true);
        return;
    }

    /* Warcraft exposes only a fade/no-fade boolean.  Use the best-evidence
     * two-second compatibility estimate and preserve the decoder for ResumeMusic. */
    start_factor = CL_MusicFadeFactor();
    cl_music.fade = CL_MUSIC_FADE_OUT;
    cl_music.fade_start_factor = start_factor;
    cl_music.fade_start_ticks = SDL_GetTicks();
    cl_music.fade_clock_ticks = cl_music.fade_start_ticks;
    cl_music.fade_duration_ms = CL_MUSIC_STOP_FADE_MS;
    CL_MusicApplyVolume();
}

void CL_MusicResume(void) {
    if (cl_music.source == CL_MUSIC_SOURCE_NONE || !cl_music.decoder_active) return;
    cl_music.fade = CL_MUSIC_FADE_NONE;
    cl_music.fade_duration_ms = 0;
    cl_music.paused = false;
    CL_MusicApplyVolume();
    S_StreamSetPaused(S_STREAM_MUSIC, CL_MusicShouldPause());
}

void CL_MusicPlayThematic(LPCSTR playlist, LONG index, LONG start_ms, DWORD session_id) {
    BOOL started;

    if (cl_music.source != CL_MUSIC_SOURCE_THEMATIC) CL_MusicRememberThematicRestore();
    started = CL_MusicStartPlaylist(playlist, false, index, CL_MUSIC_SOURCE_THEMATIC,
                                    start_ms, 0, 0, session_id, true);
    CL_MusicSendThematicSnapshot(session_id);
    if (!started) CL_MusicFinishThematic(true);
}

void CL_MusicEndThematic(void) {
    CL_MusicFinishThematic(false);
}

void CL_MusicSetVolume(LONG volume) {
    cl_music.music_volume = MAX(0, MIN(volume, 127));
    if (cl_music.source != CL_MUSIC_SOURCE_THEMATIC) CL_MusicApplyVolume();
}

void CL_MusicSetThematicVolume(LONG volume) {
    cl_music.thematic_volume = MAX(0, MIN(volume, 127));
    if (cl_music.source == CL_MUSIC_SOURCE_THEMATIC) CL_MusicApplyVolume();
}

void CL_MusicSetPosition(LONG millisecs) {
    if (cl_music.source == CL_MUSIC_SOURCE_NONE) return;
    CL_MusicSeekDecoder(MAX(0, millisecs));
}

void CL_MusicSetThematicPosition(LONG millisecs) {
    if (cl_music.source != CL_MUSIC_SOURCE_THEMATIC) return;
    CL_MusicSeekDecoder(MAX(0, millisecs));
}

void CL_MusicSuspend(void) {
    if (cl_music.source == CL_MUSIC_SOURCE_NONE || cl_music.suspended) return;
    CL_MusicFreezeFade();
    cl_music.suspended = true;
    S_StreamSetPaused(S_STREAM_MUSIC, true);
}

void CL_MusicResumeFromSuspend(void) {
    if (!cl_music.suspended) return;
    cl_music.suspended = false;
    if (!cl_music.paused) CL_MusicApplyVolume();
    S_StreamSetPaused(S_STREAM_MUSIC, CL_MusicShouldPause());
}

void CL_MusicUpdate(void) {
    BOOL paused;

    if (cl_music.source == CL_MUSIC_SOURCE_NONE) return;
    paused = CL_MusicShouldPause();
    if (paused) CL_MusicFreezeFade();
    else cl_music.fade_clock_ticks = SDL_GetTicks();
    CL_MusicApplyVolume();
    S_StreamSetPaused(S_STREAM_MUSIC, paused);
    if (paused) return;
    if (!CL_MusicPumpDecoder()) {
        if (cl_music.fade != CL_MUSIC_FADE_OUT) CL_MusicAdvancePlaylist();
        return;
    }
#ifdef BZ_FFMPEG
    if (cl_music.decoder_active && cl_music.decoder_eof && S_StreamBufferedFrames(S_STREAM_MUSIC) == 0 &&
        cl_music.fade != CL_MUSIC_FADE_OUT) {
        CL_MusicAdvancePlaylist();
    }
#endif
}
