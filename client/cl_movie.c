/*
 * cl_movie.c — optional full-screen pre-rendered movie playback.
 *
 * The client owns decoding/presentation. Game modules only queue an asset path
 * at a deferred session boundary, matching the Quake cinematic separation:
 * simulation chooses what should play; the client owns media I/O, A/V timing,
 * rendering, and skip input.
 */
#include "client.h"
#include "tr_public.h"
#include "sound/s_local.h"

#ifdef BZ_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <errno.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#endif

#define CL_MOVIE_VIDEO_QUEUE 3
#define CL_MOVIE_AUDIO_TARGET_FRAMES 44100

typedef struct {
    BYTE *rgba;
    int64_t pts_ms;
} clMovieVideoFrame_t;

typedef struct {
    BOOL active;
    BOOL extracted;
    PATHSTR source_path;
    PATHSTR disk_path;
    LPTEXTURE texture;
    DWORD width;
    DWORD height;
    DWORD start_ticks;
#ifdef BZ_FFMPEG
    AVFormatContext *format;
    AVCodecContext *video_codec;
    AVCodecContext *audio_codec;
    AVPacket *packet;
    AVFrame *video_frame;
    AVFrame *audio_frame;
    struct SwsContext *sws;
    SwrContext *swr;
    int video_stream;
    int audio_stream;
    BOOL demux_eof;
    BOOL video_flushed;
    BOOL audio_flushed;
    int64_t video_pts_origin;
    int64_t synthetic_video_pts;
    int64_t frame_duration_ms;
    BYTE *audio_buffer;
    unsigned int audio_buffer_size;
    clMovieVideoFrame_t frames[CL_MOVIE_VIDEO_QUEUE];
    DWORD frame_head;
    DWORD frame_count;
#endif
} clMovieState_t;

static clMovieState_t cl_movie;

#ifdef BZ_FFMPEG
static void CL_MovieReleaseFrames(void) {
    FOR_LOOP(i, CL_MOVIE_VIDEO_QUEUE) {
        av_freep(&cl_movie.frames[i].rgba);
    }
    cl_movie.frame_head = 0;
    cl_movie.frame_count = 0;
}

static void CL_MovieClose(void) {
    S_RawStop();
    SAFE_DELETE(cl_movie.texture, re.ReleaseTexture);
    CL_MovieReleaseFrames();
    sws_freeContext(cl_movie.sws);
    cl_movie.sws = NULL;
    swr_free(&cl_movie.swr);
    av_freep(&cl_movie.audio_buffer);
    cl_movie.audio_buffer_size = 0;
    av_frame_free(&cl_movie.video_frame);
    av_frame_free(&cl_movie.audio_frame);
    av_packet_free(&cl_movie.packet);
    avcodec_free_context(&cl_movie.video_codec);
    avcodec_free_context(&cl_movie.audio_codec);
    avformat_close_input(&cl_movie.format);
    if (cl_movie.extracted && cl_movie.disk_path[0]) remove(cl_movie.disk_path);
    memset(&cl_movie, 0, sizeof(cl_movie));
}

static BOOL CL_MovieResolveDiskPath(LPCSTR path) {
    if (FS_ResolveLoosePath(path, cl_movie.disk_path, sizeof(cl_movie.disk_path))) {
        return true;
    }

    FS_UserPath("openrealm-movie.tmp", cl_movie.disk_path, sizeof(cl_movie.disk_path));
    remove(cl_movie.disk_path);
    if (!FS_ExtractFile(path, cl_movie.disk_path)) {
        cl_movie.disk_path[0] = '\0';
        return false;
    }
    cl_movie.extracted = true;
    return true;
}

static BOOL CL_MovieOpenCodec(int stream_index, AVCodecContext **out) {
    AVStream *stream;
    AVCodec const *codec;
    AVCodecContext *ctx;

    if (stream_index < 0 || !out) return false;
    stream = cl_movie.format->streams[stream_index];
    codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;
    ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;
    if (avcodec_parameters_to_context(ctx, stream->codecpar) < 0 ||
        avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        return false;
    }
    *out = ctx;
    return true;
}

static int64_t CL_MovieFrameDuration(void) {
    AVRational rate = cl_movie.format->streams[cl_movie.video_stream]->avg_frame_rate;
    if (rate.num > 0 && rate.den > 0) {
        return MAX(1, av_rescale_q(1, av_inv_q(rate), (AVRational){1, 1000}));
    }
    return 40;
}

static int64_t CL_MovieVideoPtsMs(AVFrame const *frame) {
    int64_t pts;
    AVStream *stream = cl_movie.format->streams[cl_movie.video_stream];

    pts = frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) {
        pts = cl_movie.synthetic_video_pts;
        cl_movie.synthetic_video_pts += cl_movie.frame_duration_ms;
        return pts;
    }
    pts = av_rescale_q(pts, stream->time_base, (AVRational){1, 1000});
    if (cl_movie.video_pts_origin == AV_NOPTS_VALUE) {
        cl_movie.video_pts_origin = pts;
    }
    pts = MAX((int64_t)0, pts - cl_movie.video_pts_origin);
    cl_movie.synthetic_video_pts = pts + cl_movie.frame_duration_ms;
    return pts;
}

static BOOL CL_MovieEnsureVideoConversion(AVFrame const *frame) {
    if (cl_movie.sws) return true;
    cl_movie.width = (DWORD)frame->width;
    cl_movie.height = (DWORD)frame->height;
    if (!cl_movie.width || !cl_movie.height) return false;
    cl_movie.sws = sws_getContext(frame->width, frame->height, (enum AVPixelFormat)frame->format,
                                  frame->width, frame->height, AV_PIX_FMT_RGBA,
                                  SWS_BILINEAR, NULL, NULL, NULL);
    if (!cl_movie.sws) return false;
    FOR_LOOP(i, CL_MOVIE_VIDEO_QUEUE) {
        cl_movie.frames[i].rgba = av_malloc((size_t)cl_movie.width * cl_movie.height * 4);
        if (!cl_movie.frames[i].rgba) return false;
    }
    return true;
}

static BOOL CL_MovieQueueVideoFrame(AVFrame const *frame) {
    DWORD index;
    BYTE *dst_data[4] = {0};
    int dst_linesize[4] = {0};

    if (cl_movie.frame_count >= CL_MOVIE_VIDEO_QUEUE) return false;
    if (!CL_MovieEnsureVideoConversion(frame)) return false;
    index = (cl_movie.frame_head + cl_movie.frame_count) % CL_MOVIE_VIDEO_QUEUE;
    dst_data[0] = cl_movie.frames[index].rgba;
    dst_linesize[0] = (int)cl_movie.width * 4;
    sws_scale(cl_movie.sws,
              (uint8_t const * const *)frame->data,
              frame->linesize,
              0,
              frame->height,
              dst_data,
              dst_linesize);
    cl_movie.frames[index].pts_ms = CL_MovieVideoPtsMs(frame);
    cl_movie.frame_count++;
    return true;
}

static BOOL CL_MovieDrainVideo(void) {
    int result;

    while (cl_movie.frame_count < CL_MOVIE_VIDEO_QUEUE) {
        result = avcodec_receive_frame(cl_movie.video_codec, cl_movie.video_frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
        if (result < 0) return false;
        if (!CL_MovieQueueVideoFrame(cl_movie.video_frame)) return false;
        av_frame_unref(cl_movie.video_frame);
    }
    return true;
}

static BOOL CL_MovieEnsureAudioConversion(AVFrame const *frame) {
    AVChannelLayout output_layout = AV_CHANNEL_LAYOUT_STEREO;
    int result;

    if (cl_movie.swr) return true;
    if (frame->sample_rate <= 0 || frame->ch_layout.nb_channels <= 0) return false;
    result = swr_alloc_set_opts2(&cl_movie.swr,
                                 &output_layout,
                                 AV_SAMPLE_FMT_S16,
                                 44100,
                                 &frame->ch_layout,
                                 (enum AVSampleFormat)frame->format,
                                 frame->sample_rate,
                                 0,
                                 NULL);
    if (result < 0 || !cl_movie.swr || swr_init(cl_movie.swr) < 0) {
        swr_free(&cl_movie.swr);
        return false;
    }
    return true;
}

static BOOL CL_MovieQueueAudioFrame(AVFrame const *frame) {
    int output_frames;
    int required_bytes;
    int converted;
    BYTE *out;

    if (!CL_MovieEnsureAudioConversion(frame)) return false;
    output_frames = (int)av_rescale_rnd(swr_get_delay(cl_movie.swr, frame->sample_rate) + frame->nb_samples,
                                        44100,
                                        frame->sample_rate,
                                        AV_ROUND_UP);
    required_bytes = av_samples_get_buffer_size(NULL, 2, output_frames, AV_SAMPLE_FMT_S16, 1);
    if (required_bytes <= 0) return false;
    av_fast_malloc(&cl_movie.audio_buffer, &cl_movie.audio_buffer_size, (size_t)required_bytes);
    if (!cl_movie.audio_buffer) return false;
    out = cl_movie.audio_buffer;
    converted = swr_convert(cl_movie.swr,
                            &out,
                            output_frames,
                            (uint8_t const **)frame->extended_data,
                            frame->nb_samples);
    if (converted < 0) return false;
    S_RawSamples((SHORT const *)cl_movie.audio_buffer, (DWORD)converted);
    return true;
}

static BOOL CL_MovieDrainAudio(void) {
    int result;

    if (!cl_movie.audio_codec) return true;
    while (true) {
        result = avcodec_receive_frame(cl_movie.audio_codec, cl_movie.audio_frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
        if (result < 0) return false;
        if (!CL_MovieQueueAudioFrame(cl_movie.audio_frame)) return false;
        av_frame_unref(cl_movie.audio_frame);
        if (S_RawBufferedFrames() >= CL_MOVIE_AUDIO_TARGET_FRAMES) return true;
    }
}

static BOOL CL_MovieFlushDecoders(void) {
    if (!cl_movie.video_flushed) {
        int result = avcodec_send_packet(cl_movie.video_codec, NULL);
        if (result >= 0 || result == AVERROR_EOF) cl_movie.video_flushed = true;
    }
    if (cl_movie.audio_codec && !cl_movie.audio_flushed) {
        int result = avcodec_send_packet(cl_movie.audio_codec, NULL);
        if (result >= 0 || result == AVERROR_EOF) cl_movie.audio_flushed = true;
    }
    return CL_MovieDrainVideo() && CL_MovieDrainAudio();
}

static BOOL CL_MoviePump(void) {
    int guard = 0;

    if (!CL_MovieDrainVideo() || !CL_MovieDrainAudio()) return false;
    while (!cl_movie.demux_eof && guard++ < 128) {
        int result;
        BOOL video_ready = cl_movie.frame_count >= CL_MOVIE_VIDEO_QUEUE;
        BOOL audio_ready = !cl_movie.audio_codec || S_RawBufferedFrames() >= CL_MOVIE_AUDIO_TARGET_FRAMES;

        if (video_ready || (cl_movie.frame_count > 0 && audio_ready)) break;
        result = av_read_frame(cl_movie.format, cl_movie.packet);
        if (result < 0) {
            cl_movie.demux_eof = true;
            break;
        }
        if (cl_movie.packet->stream_index == cl_movie.video_stream) {
            result = avcodec_send_packet(cl_movie.video_codec, cl_movie.packet);
            av_packet_unref(cl_movie.packet);
            if (result < 0 && result != AVERROR(EAGAIN)) return false;
            if (!CL_MovieDrainVideo()) return false;
        } else if (cl_movie.packet->stream_index == cl_movie.audio_stream && cl_movie.audio_codec) {
            result = avcodec_send_packet(cl_movie.audio_codec, cl_movie.packet);
            av_packet_unref(cl_movie.packet);
            if (result < 0 && result != AVERROR(EAGAIN)) return false;
            if (!CL_MovieDrainAudio()) return false;
        } else {
            av_packet_unref(cl_movie.packet);
        }
    }
    if (cl_movie.demux_eof) return CL_MovieFlushDecoders();
    return true;
}

static void CL_MoviePresentFrames(void) {
    DWORD elapsed = SDL_GetTicks() - cl_movie.start_ticks;

    while (cl_movie.frame_count) {
        clMovieVideoFrame_t *frame = &cl_movie.frames[cl_movie.frame_head];
        if ((DWORD)frame->pts_ms > elapsed && cl_movie.texture) break;
        if (!cl_movie.texture) {
            cl_movie.texture = re.CreateTextureRGBA(cl_movie.width, cl_movie.height, frame->rgba);
        } else {
            re.UpdateTextureRGBA(cl_movie.texture, cl_movie.width, cl_movie.height, frame->rgba);
        }
        cl_movie.frame_head = (cl_movie.frame_head + 1) % CL_MOVIE_VIDEO_QUEUE;
        cl_movie.frame_count--;
    }
}
#endif

void CL_MovieInit(void) {
    Cmd_AddCommand("playmovie", CL_Movie_f);
}

void CL_MovieShutdown(void) {
    Cmd_RemoveCommand("playmovie");
#ifdef BZ_FFMPEG
    if (cl_movie.active) CL_MovieClose();
#else
    memset(&cl_movie, 0, sizeof(cl_movie));
#endif
}

BOOL CL_PlayMovie(LPCSTR path) {
#ifndef BZ_FFMPEG
    (void)path;
    CON_printf("Movie playback is disabled in this build (rebuild with FFMPEG=1).");
    return false;
#else
    AVCodec const *video_decoder;
    int video_stream;
    int audio_stream;

    if (!path || !*path) return false;
    if (cl_movie.active) CL_MovieClose();
    memset(&cl_movie, 0, sizeof(cl_movie));
    snprintf(cl_movie.source_path, sizeof(cl_movie.source_path), "%s", path);
    cl_movie.video_stream = -1;
    cl_movie.audio_stream = -1;
    cl_movie.video_pts_origin = AV_NOPTS_VALUE;
    if (!CL_MovieResolveDiskPath(path)) {
        CON_printf("Movie not found: %s", path);
        CL_MovieClose();
        return false;
    }

    av_log_set_level(AV_LOG_ERROR);
    if (avformat_open_input(&cl_movie.format, cl_movie.disk_path, NULL, NULL) < 0 ||
        avformat_find_stream_info(cl_movie.format, NULL) < 0) {
        CON_printf("Unable to open movie: %s", path);
        CL_MovieClose();
        return false;
    }
    video_stream = av_find_best_stream(cl_movie.format, AVMEDIA_TYPE_VIDEO, -1, -1, &video_decoder, 0);
    if (video_stream < 0 || !CL_MovieOpenCodec(video_stream, &cl_movie.video_codec)) {
        CON_printf("Movie has no supported video stream: %s", path);
        CL_MovieClose();
        return false;
    }
    cl_movie.video_stream = video_stream;
    audio_stream = av_find_best_stream(cl_movie.format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream >= 0 && CL_MovieOpenCodec(audio_stream, &cl_movie.audio_codec)) {
        cl_movie.audio_stream = audio_stream;
    }
    cl_movie.packet = av_packet_alloc();
    cl_movie.video_frame = av_frame_alloc();
    cl_movie.audio_frame = av_frame_alloc();
    if (!cl_movie.packet || !cl_movie.video_frame || !cl_movie.audio_frame) {
        CL_MovieClose();
        return false;
    }
    cl_movie.frame_duration_ms = CL_MovieFrameDuration();
    S_StopAllSounds();
    S_RawStart();
    cl_movie.start_ticks = SDL_GetTicks();
    cl_movie.active = true;
    if (!CL_MoviePump() || cl_movie.frame_count == 0) {
        CON_printf("Unable to decode movie: %s", path);
        CL_MovieClose();
        return false;
    }
    CL_MoviePresentFrames();
    return true;
#endif
}

BOOL CL_MovieActive(void) {
    return cl_movie.active;
}

void CL_MovieUpdate(void) {
#ifdef BZ_FFMPEG
    if (!cl_movie.active) return;
    CL_MoviePresentFrames();
    if (!CL_MoviePump()) {
        CON_printf("Movie decode failed: %s", cl_movie.source_path);
        CL_MovieClose();
        return;
    }
    CL_MoviePresentFrames();
    if (cl_movie.demux_eof && cl_movie.frame_count == 0 && S_RawBufferedFrames() == 0) {
        CL_MovieClose();
    }
#endif
}

void CL_MovieDraw(void) {
    RECT scene;
    RECT movie;
    RECT uv = {0, 0, 1, 1};
    FLOAT scene_aspect;
    FLOAT movie_aspect;

    if (!cl_movie.active) return;
    scene = re.GetUISceneRect();
    re.DrawFill(&scene, COLOR32_BLACK);
    if (!cl_movie.texture || !cl_movie.width || !cl_movie.height) return;

    scene_aspect = scene.w / scene.h;
    movie_aspect = (FLOAT)cl_movie.width / (FLOAT)cl_movie.height;
    movie = scene;
    if (movie_aspect > scene_aspect) {
        movie.h = scene.w / movie_aspect;
        movie.y = scene.y + (scene.h - movie.h) * 0.5f;
    } else {
        movie.w = scene.h * movie_aspect;
        movie.x = scene.x + (scene.w - movie.w) * 0.5f;
    }
    re.DrawImage(cl_movie.texture, &movie, &uv, COLOR32_WHITE);
}

BOOL CL_MovieKeyEvent(keyCode_t key, bool down) {
    if (!cl_movie.active) return false;
    if (down && key == K_ESCAPE) {
#ifdef BZ_FFMPEG
        CL_MovieClose();
#else
        cl_movie.active = false;
#endif
    }
    return true;
}

void CL_Movie_f(void) {
    char path[MAX_PATHLEN];
    LPCSTR arg;

    if (Cmd_Argc() != 2) {
        CON_printf("usage: playmovie <name-or-path>");
        return;
    }
    arg = Cmd_Argv(1);
    if (strchr(arg, '/') || strchr(arg, '\\') || strchr(arg, '.')) {
        snprintf(path, sizeof(path), "%s", arg);
    } else {
        snprintf(path, sizeof(path), "Movies\\%s.mpq", arg);
    }
    CL_PlayMovie(path);
}
