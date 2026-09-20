/*
 * g_audio_duration.c — synchronous WC3 sound-file duration metadata.
 *
 * GetSoundFileDuration is a server/JASS query, so it cannot depend on the
 * optional client FFmpeg decoder.  Read the already-mounted VFS asset and
 * inspect the container/frame metadata only; no PCM decode or client round
 * trip is required.
 */
#include "g_local.h"

static DWORD Audio_ReadLE32(BYTE const *p) {
    return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static uint64_t Audio_ReadLE64(BYTE const *p) {
    uint64_t value = 0;
    FOR_LOOP(i, 8) value |= (uint64_t)p[i] << (i * 8);
    return value;
}

static LONG Audio_Millis(uint64_t numerator, uint64_t denominator) {
    uint64_t millis;

    if (!denominator) return 0;
    millis = numerator / denominator;
    return (LONG)MIN(millis, (uint64_t)0x7fffffff);
}

static LONG Audio_WavDuration(BYTE const *data, DWORD size) {
    DWORD offset = 12;
    DWORD byte_rate = 0;
    DWORD sample_rate = 0;
    DWORD fact_samples = 0;
    uint64_t data_bytes = 0;

    if (!data || size < 12 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4)) return 0;
    while (offset + 8 <= size) {
        DWORD chunk_size = Audio_ReadLE32(data + offset + 4);
        DWORD payload = offset + 8;
        uint64_t next = (uint64_t)payload + chunk_size + (chunk_size & 1u);

        if ((uint64_t)payload + chunk_size > size) break;
        if (!memcmp(data + offset, "fmt ", 4) && chunk_size >= 12) {
            sample_rate = Audio_ReadLE32(data + payload + 4);
            byte_rate = Audio_ReadLE32(data + payload + 8);
        } else if (!memcmp(data + offset, "fact", 4) && chunk_size >= 4) {
            fact_samples = Audio_ReadLE32(data + payload);
        } else if (!memcmp(data + offset, "data", 4)) {
            data_bytes += chunk_size;
        }
        if (next > size || next <= offset) break;
        offset = (DWORD)next;
    }
    if (sample_rate && fact_samples)
        return Audio_Millis((uint64_t)fact_samples * 1000u, sample_rate);
    return byte_rate && data_bytes ? Audio_Millis(data_bytes * 1000u, byte_rate) : 0;
}

static DWORD Audio_Mp3Synchsafe(BYTE const *p) {
    if ((p[0] | p[1] | p[2] | p[3]) & 0x80) return 0;
    return ((DWORD)p[0] << 21) | ((DWORD)p[1] << 14) | ((DWORD)p[2] << 7) | p[3];
}

typedef struct {
    DWORD length;
    DWORD samples;
    DWORD sample_rate;
} mp3FrameInfo_t;

static BOOL Audio_Mp3Frame(BYTE const *p, DWORD remaining, mp3FrameInfo_t *out) {
    static WORD const bitrate_mpeg1[3][15] = {
        /* Layer III */ { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 },
        /* Layer II  */ { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384 },
        /* Layer I   */ { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
    };
    static WORD const bitrate_mpeg2[3][15] = {
        /* Layer III */ { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 },
        /* Layer II  */ { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 },
        /* Layer I   */ { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256 },
    };
    static DWORD const sample_rates[3][3] = {
        { 11025, 12000, 8000 },  /* MPEG 2.5 */
        { 22050, 24000, 16000 }, /* MPEG 2 */
        { 44100, 48000, 32000 }, /* MPEG 1 */
    };
    DWORD header, version_bits, version_index, layer_bits, layer_index;
    DWORD bitrate_index, rate_index, padding, bitrate, sample_rate, length, samples;

    if (!p || remaining < 4 || !out) return false;
    header = ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | p[3];
    if ((header & 0xffe00000u) != 0xffe00000u) return false;
    version_bits = (header >> 19) & 3u;
    layer_bits = (header >> 17) & 3u;
    bitrate_index = (header >> 12) & 15u;
    rate_index = (header >> 10) & 3u;
    padding = (header >> 9) & 1u;
    if (version_bits == 1 || layer_bits == 0 || bitrate_index == 0 || bitrate_index == 15 || rate_index == 3)
        return false;

    version_index = version_bits == 3 ? 2 : (version_bits == 2 ? 1 : 0);
    layer_index = layer_bits == 1 ? 0 : (layer_bits == 2 ? 1 : 2);
    bitrate = (version_bits == 3 ? bitrate_mpeg1[layer_index][bitrate_index]
                                 : bitrate_mpeg2[layer_index][bitrate_index]) * 1000u;
    sample_rate = sample_rates[version_index][rate_index];
    if (!bitrate || !sample_rate) return false;

    if (layer_bits == 3) {
        length = ((12u * bitrate) / sample_rate + padding) * 4u;
        samples = 384;
    } else if (layer_bits == 2) {
        length = (144u * bitrate) / sample_rate + padding;
        samples = 1152;
    } else {
        length = ((version_bits == 3 ? 144u : 72u) * bitrate) / sample_rate + padding;
        samples = version_bits == 3 ? 1152 : 576;
    }
    if (length < 4 || length > remaining) return false;
    out->length = length;
    out->samples = samples;
    out->sample_rate = sample_rate;
    return true;
}

static LONG Audio_Mp3Duration(BYTE const *data, DWORD size) {
    DWORD offset = 0;
    uint64_t micros = 0;
    DWORD frames = 0;

    if (!data || size < 4) return 0;
    if (size >= 10 && !memcmp(data, "ID3", 3)) {
        DWORD tag_size = Audio_Mp3Synchsafe(data + 6);
        uint64_t skip = 10u + (uint64_t)tag_size + ((data[5] & 0x10) ? 10u : 0u);
        if (skip >= size) return 0;
        offset = (DWORD)skip;
    }

    while (offset + 4 <= size) {
        mp3FrameInfo_t frame;

        if (!Audio_Mp3Frame(data + offset, size - offset, &frame)) {
            offset++;
            continue;
        }
        micros += ((uint64_t)frame.samples * 1000000u) / frame.sample_rate;
        frames++;
        offset += frame.length;
    }
    return frames ? Audio_Millis(micros + 500u, 1000u) : 0;
}

static LONG Audio_OggVorbisDuration(BYTE const *data, DWORD size) {
    DWORD sample_rate = 0;
    uint64_t last_granule = 0;
    DWORD offset = 0;

    if (!data || size < 27 || memcmp(data, "OggS", 4)) return 0;
    /* The Vorbis identification packet is the first packet and is normally
     * wholly contained in the first page.  Parse the page lacing rather than
     * scanning arbitrary payload bytes for the signature. */
    {
        DWORD segments = data[26];
        DWORD payload = 27u + segments;
        DWORD packet_size = 0;

        if (payload <= size && 27u + segments <= size) {
            FOR_LOOP(i, segments) {
                packet_size += data[27 + i];
                if (data[27 + i] < 255) break;
            }
            if ((uint64_t)payload + packet_size <= size && packet_size >= 16 &&
                data[payload] == 1 && !memcmp(data + payload + 1, "vorbis", 6))
                sample_rate = Audio_ReadLE32(data + payload + 12);
        }
    }
    if (!sample_rate) return 0;

    while (offset + 27 <= size) {
        DWORD segments, payload_size = 0, page_size;
        uint64_t granule;

        if (memcmp(data + offset, "OggS", 4)) { offset++; continue; }
        segments = data[offset + 26];
        if ((uint64_t)offset + 27u + segments > size) break;
        FOR_LOOP(i, segments) payload_size += data[offset + 27 + i];
        page_size = 27u + segments + payload_size;
        if ((uint64_t)offset + page_size > size) break;
        granule = Audio_ReadLE64(data + offset + 6);
        if (granule != ~(uint64_t)0) last_granule = granule;
        offset += page_size;
    }
    return last_granule ? Audio_Millis(last_granule * 1000u, sample_rate) : 0;
}

static LONG Audio_FlacDuration(BYTE const *data, DWORD size) {
    DWORD sample_rate;
    uint64_t total_samples;

    if (!data || size < 42 || memcmp(data, "fLaC", 4)) return 0;
    if ((data[4] & 0x7f) != 0 || (((DWORD)data[5] << 16) | ((DWORD)data[6] << 8) | data[7]) < 34)
        return 0;
    sample_rate = ((DWORD)data[18] << 12) | ((DWORD)data[19] << 4) | (data[20] >> 4);
    total_samples = ((uint64_t)(data[21] & 0x0f) << 32) |
                    ((uint64_t)data[22] << 24) | ((uint64_t)data[23] << 16) |
                    ((uint64_t)data[24] << 8) | data[25];
    return sample_rate && total_samples ? Audio_Millis(total_samples * 1000u, sample_rate) : 0;
}

LONG G_AudioDurationFromMemory(LPCSTR filename, BYTE const *data, DWORD size) {
    LONG duration;

    (void)filename;
    if (!data || !size) return 0;
    if (size >= 12 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WAVE", 4))
        duration = Audio_WavDuration(data, size);
    else if (size >= 4 && !memcmp(data, "OggS", 4))
        duration = Audio_OggVorbisDuration(data, size);
    else if (size >= 4 && !memcmp(data, "fLaC", 4))
        duration = Audio_FlacDuration(data, size);
    else
        duration = Audio_Mp3Duration(data, size);
    return MAX(0, duration);
}

LONG G_SoundFileDuration(LPCSTR filename) {
    DWORD size = 0;
    BYTE *data;
    LONG duration;

    if (!filename || !*filename) return 0;
    data = gi.ReadFile(filename, &size);
    if (!data || !size) {
        if (data) gi.MemFree(data);
        return 0;
    }
    duration = G_AudioDurationFromMemory(filename, data, size);
    gi.MemFree(data);
    return duration;
}
