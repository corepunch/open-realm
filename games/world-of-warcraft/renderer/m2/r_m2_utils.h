#ifndef WOW_R_M2_UTILS_H
#define WOW_R_M2_UTILS_H

#include "r_m2_format.h"
#include "games/world-of-warcraft/common/wow_character_utils.h"
#include <math.h>
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

typedef enum {
    M2_CHAR_SECTIONS_UNKNOWN, M2_CHAR_SECTIONS_INVALID,
    M2_CHAR_SECTIONS_VARIATION_FIRST, M2_CHAR_SECTIONS_TEXTURE_FIRST,
} m2CharSectionsLayout_t;

typedef struct {
    m2Format_t format;
    DWORD sequence_stride, bone_stride, attachment_stride, camera_stride, particle_stride, ribbon_stride;
} m2FormatDef_t;

typedef struct {
    FLOAT value[3], midpoint, lifespan;
} M2PARTICLECURVE;
typedef M2PARTICLECURVE *LPM2PARTICLECURVE;
typedef M2PARTICLECURVE const *LPCM2PARTICLECURVE;

/* M2 uses fractional, lifetime-normalized scales; encode them without changing the legacy MDX byte/seconds contract. */
static void m2_particle_encode_curve(LPCM2PARTICLECURVE curve, cparticle_t *particle) {
    FLOAT max_value = MAX(curve->value[0], MAX(curve->value[1], curve->value[2]));
    particle->size_value_scale = max_value > 0.0f ? max_value / 255.0f : 1.0f;
    FOR_LOOP(i, 3)
        particle->size[i] = max_value > 0.0f
            ? (BYTE)MIN(255, MAX(0, (int)(curve->value[i] / max_value * 255.0f + 0.5f))) : 0;
    particle->size_time_scale = 1.0f / MAX(curve->lifespan, 0.001f);
    particle->midtime = (BYTE)MIN(254, MAX(1, (int)(curve->midpoint * 255.0f + 0.5f)));
}

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

/* Classic male CharSections hair rows omit strings; their archive contract derives the color texture from the race directory. */
static BOOL m2_classic_hair_texture_path(LPCSTR model_path, DWORD color, PATHSTR out) {
    LPCSTR character, race, end;
    if (!model_path || !out) return false;
    character = strcasestr(model_path, "Character\\");
    if (!character) character = strcasestr(model_path, "Character/");
    if (!character) return false;
    race = character + strlen("Character\\"); end = strpbrk(race, "\\/");
    if (!end || end == race) return false;
    snprintf(out, MAX_PATHLEN, "Character\\%.*s\\Hair00_%02u.blp", (int)(end - race), race, color);
    return true;
}

/* Validate the complete indirect skin range before the renderer's vertex copy loop starts. */
static BOOL m2_validate_skin_vertex_range(WORD const *skin_vertices, DWORD skin_vertex_count,
                                          WORD const *skin_indices, DWORD skin_index_count,
                                          DWORD vertex_count, DWORD index_start, DWORD index_count) {
    if (!skin_vertices || !skin_indices || index_start > skin_index_count ||
        index_count > skin_index_count - index_start) return false;
    FOR_LOOP(i, index_count) {
        DWORD vertex_lookup = skin_indices[index_start + i];
        if (vertex_lookup >= skin_vertex_count || skin_vertices[vertex_lookup] >= vertex_count) return false;
    }
    return true;
}

static BYTE const *m2_find_chunk(BYTE const *data, DWORD size, DWORD fourcc, LPDWORD chunk_size) {
    DWORD offset = 0;
    while (offset + 8 <= size) {
        DWORD current_size;
        memcpy(&current_size, data + offset + 4, sizeof(current_size));
        if ((offset += 8) + current_size > size) break;
        if (*(DWORD const *)(data + offset - 8) == fourcc) { *chunk_size = current_size; return data + offset; }
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

/* The shared particle renderer's legacy ADD names are opposite WoW's modes 3/4; adapt M2 without changing MDX. */
static BLEND_MODE m2_particle_blend_mode(WORD wow_blend) {
    BLEND_MODE mode = m2_blend_mode(wow_blend);
    return mode == BLEND_MODE_ADD ? BLEND_MODE_ADDALPHA :
           mode == BLEND_MODE_ADDALPHA ? BLEND_MODE_ADD : mode;
}

/* M2 stores cone inclination and azimuth ranges in radians around the authored +Z launch axis. */
static VECTOR3 m2_particle_direction(FLOAT vertical_range, FLOAT horizontal_range, VECTOR2 random) {
    FLOAT phi = (random.x + 1.0f) * 0.5f * MAX(0.0f, vertical_range);
    FLOAT theta = random.y * 0.5f * horizontal_range;
    return (VECTOR3){ sinf(phi) * cosf(theta), sinf(phi) * sinf(theta), cosf(phi) };
}

static DWORD m2_read32(BYTE const *p) {
    return ((DWORD)p[0]) | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

/* CharSections keeps ten columns in both layouts, so field-4 values identify which schema the mounted DBC uses. */
static m2CharSectionsLayout_t m2_char_sections_layout(BYTE const *records, DWORD count, DWORD stride) {
    DWORD large = 0, small = 0;
    if (!records || !count || stride < 10 * sizeof(DWORD)) return M2_CHAR_SECTIONS_INVALID;
    FOR_LOOP(i, MIN(count, 20u))
        if (m2_read32(records + i * stride + 4 * sizeof(DWORD)) > 50) large++;
        else small++;
    return large > small ? M2_CHAR_SECTIONS_TEXTURE_FIRST : M2_CHAR_SECTIONS_VARIATION_FIRST;
}

/* Classic and later ItemDisplayInfo schemas place component textures one field apart. */
static DWORD m2_item_display_texture_base(DWORD fields) { return fields >= 25 ? 15 : fields >= 22 ? 14 : 0; }

static void m2_blend_pixel(LPCOLOR32 dst, COLOR32 src) {
    DWORD inv;
    if (src.a == 0) return;
    if (src.a >= 250) { *dst = src; return; }
    inv = 255 - src.a;
    dst->b = (BYTE)((src.b * src.a + dst->b * inv) / 255);
    dst->g = (BYTE)((src.g * src.a + dst->g * inv) / 255);
    dst->r = (BYTE)((src.r * src.a + dst->r * inv) / 255);
    dst->a = (BYTE)MIN(255, src.a + (dst->a * inv) / 255);
}

static void m2_paste_component(LPCOLOR32 dst, DWORD dst_width, DWORD dst_height,
                               LPCOLOR32 src, DWORD src_width, DWORD src_height,
                               DWORD x, DWORD y, DWORD w, DWORD h) {
    if (!dst || !src || !dst_width || !dst_height || !src_width || !src_height ||
        x >= dst_width || y >= dst_height) return;
    w = MIN(w, dst_width - x);
    h = MIN(h, dst_height - y);
    FOR_LOOP(row, h) {
        DWORD src_y = row * src_height / h;
        FOR_LOOP(col, w) {
            DWORD src_x = col * src_width / w;
            m2_blend_pixel(&dst[(y + row) * dst_width + x + col], src[src_y * src_width + src_x]);
        }
    }
}

#endif
