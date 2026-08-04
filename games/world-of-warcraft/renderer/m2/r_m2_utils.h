#ifndef WOW_R_M2_UTILS_H
#define WOW_R_M2_UTILS_H

#include "r_m2_format.h"
#include <string.h>
#include <strings.h>

typedef struct {
    uint16_t track_type, loop_index;
    BOOL classic;
    m2Array_t ranges, sequence_times, sequence_keys;
} m2TrackView_t;

typedef enum {
    M2_PARTICLE_SPEED, M2_PARTICLE_VARIATION, M2_PARTICLE_VERTICAL_RANGE,
    M2_PARTICLE_HORIZONTAL_RANGE, M2_PARTICLE_GRAVITY, M2_PARTICLE_LIFE,
    M2_PARTICLE_EMISSION_RATE, M2_PARTICLE_WIDTH, M2_PARTICLE_LENGTH,
    M2_PARTICLE_ZSOURCE, M2_PARTICLE_VISIBILITY,
} m2ParticleTrackType_t;

typedef enum {
    M2_RIBBON_COLOR, M2_RIBBON_ALPHA, M2_RIBBON_HEIGHT_ABOVE, M2_RIBBON_HEIGHT_BELOW,
    M2_RIBBON_TEXTURE_SLOT, M2_RIBBON_VISIBILITY,
} m2RibbonTrackType_t;

typedef struct {
    m2Format_t format;
    DWORD sequence_stride, bone_stride, attachment_stride, camera_stride, particle_stride, ribbon_stride;
} m2FormatDef_t;

/* One version convention controls every versioned record family. */
static m2FormatDef_t const *m2_format_def(DWORD version) {
    static m2FormatDef_t const formats[] = {
        { M2_FORMAT_CLASSIC, sizeof(m2SequenceClassic_t), sizeof(m2CompBoneClassic_t),
          sizeof(m2AttachmentClassic_t), sizeof(m2CameraClassic_t), sizeof(m2ParticleClassic_t),
          sizeof(m2RibbonClassic_t) },
        { M2_FORMAT_MODERN, sizeof(m2SequenceModern_t), sizeof(m2CompBoneModern_t),
          sizeof(m2AttachmentModern_t), sizeof(m2CameraModern_t), sizeof(m2Particle_t), sizeof(m2Ribbon_t) },
    };
    return &formats[version <= 263 ? M2_FORMAT_CLASSIC : M2_FORMAT_MODERN];
}

static BOOL m2_path_has_extension(LPCSTR path, LPCSTR extension) {
    size_t path_len, ext_len;
    if (!path || !extension) return false;
    path_len = strlen(path); ext_len = strlen(extension);
    return path_len >= ext_len && !strcasecmp(path + path_len - ext_len, extension);
}

static BYTE const *m2_find_chunk(BYTE const *data, DWORD size, LPCSTR tag, LPDWORD chunk_size) {
    DWORD offset = 0;
    while (offset + 8 <= size) {
        DWORD current_size;
        memcpy(&current_size, data + offset + 4, sizeof(current_size));
        if ((offset += 8) + current_size > size) break;
        if (!memcmp(data + offset - 8, tag, 4)) { *chunk_size = current_size; return data + offset; }
        offset += current_size;
    }
    return NULL;
}

static BOOL m2_copy_with_extension(LPCSTR path, LPCSTR extension, LPSTR out, DWORD out_size) {
    LPCSTR dot;
    size_t stem_len;
    if (!path || !extension || !out || !out_size) return false;
    dot = strrchr(path, '.'); stem_len = dot ? (size_t)(dot - path) : strlen(path);
    if (stem_len + strlen(extension) + 1 > out_size) return false;
    memcpy(out, path, stem_len); snprintf(out + stem_len, out_size - stem_len, "%s", extension);
    return true;
}

/* All file arrays remain offsets until a bounded consumer requests a pointer. */
static BOOL m2_array_range(m2Array_t array, DWORD elem_size, DWORD file_size, LPDWORD offset, LPDWORD bytes) {
    if (array.size <= 0 || array.offset < 0 || !elem_size || (DWORD)array.size > ~(DWORD)0 / elem_size) return false;
    *offset = (DWORD)array.offset; *bytes = (DWORD)array.size * elem_size;
    return *offset <= file_size && *bytes <= file_size - *offset;
}

static void *m2_array_ptr(BYTE const *base, DWORD file_size, m2Array_t array, DWORD elem_size) {
    DWORD offset, bytes;
    return m2_array_range(array, elem_size, file_size, &offset, &bytes) ? (void *)(base + offset) : NULL;
}

static LPCSTR m2_string_ptr(BYTE const *base, DWORD file_size, m2Array_t array) {
    DWORD offset, bytes;
    if (!m2_array_range(array, 1, file_size, &offset, &bytes) || !memchr(base + offset, 0, bytes)) return NULL;
    return (LPCSTR)(base + offset);
}

static m2TrackView_t m2_modern_track(m2Track_t const *track) {
    return (m2TrackView_t){ track ? track->track_type : 0, track ? track->loop_index : 0xffff, false, { 0 },
        track ? track->sequence_times : (m2Array_t){ 0 }, track ? track->sequence_keys : (m2Array_t){ 0 } };
}

static m2TrackView_t m2_classic_track(m2TrackClassic_t const *track) {
    return (m2TrackView_t){ track ? track->track_type : 0, track ? track->loop_index : 0xffff, true,
        track ? track->ranges : (m2Array_t){ 0 }, track ? track->times : (m2Array_t){ 0 },
        track ? track->keys : (m2Array_t){ 0 } };
}

/* File-shaped particle records make version selection a single switch instead of offset arithmetic. */
static m2TrackView_t m2_particle_track(m2FormatDef_t const *format, void const *raw,
                                       m2ParticleTrackType_t type) {
    if (format->format == M2_FORMAT_CLASSIC) {
        m2ParticleClassic_t const *p = raw;
        m2TrackClassic_t const *tracks[] = { &p->speed_track, &p->variation_track, &p->latitude_track,
            &p->longitude_track, &p->gravity_track, &p->life_track, &p->emission_rate_track, &p->width_track,
            &p->length_track, NULL, &p->visibility_track };
        return m2_classic_track(tracks[type]);
    }
    m2Particle_t const *p = raw;
    m2Track_t const *tracks[] = { &p->speed_track, &p->variation_track, &p->latitude_track, &p->longitude_track,
        &p->gravity_track, &p->life_track, &p->emission_rate_track, &p->width_track, &p->length_track,
        &p->zsource_track, &p->visibility_track };
    return m2_modern_track(tracks[type]);
}

static m2PartTrack_t const *m2_particle_part_track(m2FormatDef_t const *format, void const *raw, DWORD index) {
    m2Particle_t const *p = raw;
    if (format->format == M2_FORMAT_CLASSIC || index > 2) return NULL;
    return index == 0 ? &p->color_track : index == 1 ? &p->alpha_track : &p->scale_track;
}

static m2TrackView_t m2_ribbon_track(m2FormatDef_t const *format, void const *raw, m2RibbonTrackType_t type) {
    if (format->format == M2_FORMAT_CLASSIC) {
        m2RibbonClassic_t const *r = raw;
        m2TrackClassic_t const *tracks[] = { &r->color_track, &r->alpha_track, &r->height_above_track,
            &r->height_below_track, &r->texture_slot_track, &r->visibility_track };
        return m2_classic_track(tracks[type]);
    }
    m2Ribbon_t const *r = raw;
    m2Track_t const *tracks[] = { &r->color_track, &r->alpha_track, &r->height_above_track,
        &r->height_below_track, &r->texture_slot_track, &r->visibility_track };
    return m2_modern_track(tracks[type]);
}

static FLOAT m2_ribbon_edges_per_second(m2FormatDef_t const *format, void const *raw) {
    return format->format == M2_FORMAT_CLASSIC ? ((m2RibbonClassic_t const *)raw)->edges_per_second
                                               : ((m2Ribbon_t const *)raw)->edges_per_second;
}

static FLOAT m2_ribbon_edge_lifetime(m2FormatDef_t const *format, void const *raw) {
    return format->format == M2_FORMAT_CLASSIC ? ((m2RibbonClassic_t const *)raw)->edge_lifetime
                                               : ((m2Ribbon_t const *)raw)->edge_lifetime;
}

static FLOAT m2_ribbon_gravity(m2FormatDef_t const *format, void const *raw) {
    return format->format == M2_FORMAT_CLASSIC ? ((m2RibbonClassic_t const *)raw)->gravity
                                               : ((m2Ribbon_t const *)raw)->gravity;
}

static WORD m2_ribbon_rows(m2FormatDef_t const *format, void const *raw) {
    return format->format == M2_FORMAT_CLASSIC ? ((m2RibbonClassic_t const *)raw)->texture_rows
                                               : ((m2Ribbon_t const *)raw)->texture_rows;
}

static WORD m2_ribbon_cols(m2FormatDef_t const *format, void const *raw) {
    return format->format == M2_FORMAT_CLASSIC ? ((m2RibbonClassic_t const *)raw)->texture_cols
                                               : ((m2Ribbon_t const *)raw)->texture_cols;
}

/* Vanilla inserts extra header fields, so its render flags do not occupy the modern materials slot. */
static m2Array_t m2_material_array(m2Array_t modern, m2Array_t legacy, BOOL legacy_header) {
    return legacy_header ? legacy : modern;
}

/* WoW M2 blend IDs are ordered differently from the shared engine enum. */
static BLEND_MODE m2_blend_mode(WORD wow_blend) {
    static BLEND_MODE const modes[] = {
        BLEND_MODE_NONE, BLEND_MODE_ALPHAKEY, BLEND_MODE_BLEND, BLEND_MODE_ADD,
        BLEND_MODE_ADDALPHA, BLEND_MODE_MODULATE, BLEND_MODE_MODULATE_2X
    };
    return wow_blend < sizeof(modes) / sizeof(modes[0]) ? modes[wow_blend] : BLEND_MODE_NONE;
}

/* M2 ranges spread an authored +Z launch vector; they are not spherical latitude/longitude angles. */
static VECTOR3 m2_particle_direction(FLOAT vertical_range, FLOAT horizontal_range,
                                     FLOAT random_x, FLOAT random_y, FLOAT random_z) {
    VECTOR3 dir = { random_x * horizontal_range, random_y * horizontal_range,
                    1.0f + random_z * vertical_range };
    Vector3_normalize(&dir);
    return dir;
}

#endif
