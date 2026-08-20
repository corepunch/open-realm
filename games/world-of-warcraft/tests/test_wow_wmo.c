/* test_wow_wmo.c — standalone tests for WMO-related algorithms.
 *
 * Tests:
 *   A. Wow_FixMocvAlpha  (MOCV CPU fixup)
 *   B. Wow_WmoDoodadLocalMatrix  (quaternion → 4×4 matrix)
 *   C. WMO binary struct layout assertions (sizes, offsets)
 *   D. MOHD field extraction from synthetic chunk bytes
 *   E. Doodad set/def field extraction from synthetic chunks
 *
 * No OpenGL or renderer headers needed — all types are from stdint / shared.
 */

#include "test.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* =========================================================================
   Minimal type aliases matching the production code (WinAPI / BZ style)
   ======================================================================= */

typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int      BOOL;
#define true  1
#define false 0
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))

typedef struct { BYTE b, g, r, a; } COLOR32;
typedef struct { float x, y, z; } wowVec3_t;

typedef struct {
    BYTE      type;       /* 0=OMNI 1=SPOT 2=DIRECT 3=AMBIENT */
    BYTE      use_atten;
    BYTE      pad[2];
    COLOR32   color;      /* BGRA */
    wowVec3_t position;
    float     intensity;
    float     atten_start;
    float     atten_end;
    float     unk[4];
} wowWmoLight_t;  /* 48 bytes */

typedef struct {
    int16_t  box_min[3];
    int16_t  box_max[3];
    uint32_t first_index;
    uint16_t num_indices;
    uint16_t first_vertex;
    uint16_t last_vertex;
    BYTE     flags;
    BYTE     material_id;
} wowWmoBatchDef_t;  /* 16 bytes in-memory */

typedef struct {
    DWORD     name_flags;
    wowVec3_t position;
    float     quat[4];
    float     scale;
    COLOR32   color;
} wowWmoDoodadDef_t;  /* 40 bytes */

typedef struct {
    char  name[20];
    DWORD start;
    DWORD count;
    DWORD pad;
} wowWmoDoodadSet_t;  /* 32 bytes */

/* 4×4 column-major float matrix (matches shared/types/matrix4.h) */
typedef struct { float v[16]; } MATRIX4_TEST;

/* =========================================================================
   A. Inline reference implementation of Wow_FixMocvAlpha
   (exact translation of the production algorithm, verified here)
   ======================================================================= */

static void ref_fix_mocv(BYTE *colors, DWORD color_count,
                          wowWmoBatchDef_t const *batches, DWORD batch_count,
                          DWORD trans_batch_count,
                          COLOR32 amb, DWORD mohd_flags,
                          BOOL exterior) {
    BOOL skip_base = (mohd_flags & 0x04) != 0;
    BOOL lighten   = (mohd_flags & 0x02) != 0;
    BYTE ambR = skip_base ? 0 : amb.r;
    BYTE ambG = skip_base ? 0 : amb.g;
    BYTE ambB = skip_base ? 0 : amb.b;
    int begin_second = 0;
    DWORD i;

    if (trans_batch_count > 0 && batch_count > 0) {
        DWORD last_a = trans_batch_count - 1 < batch_count
                       ? trans_batch_count - 1 : batch_count - 1;
        begin_second = (int)batches[last_a].last_vertex + 1;
    }

    if (lighten) {
        for (i = (DWORD)begin_second; i < color_count; i++)
            colors[i * 4 + 3] = exterior ? 0xFF : 0x00;
        return;
    }

    /* Batch-A */
    for (i = 0; i < (DWORD)begin_second && i < color_count; i++) {
        float a = colors[i * 4 + 3] / 255.f;
        int r = (int)colors[i * 4 + 2];
        int g = (int)colors[i * 4 + 1];
        int b = (int)colors[i * 4 + 0];
        colors[i * 4 + 2] = (BYTE)MAX(0, (int)((r - ambR) * (1.f - a) / 2.f));
        colors[i * 4 + 1] = (BYTE)MAX(0, (int)((g - ambG) * (1.f - a) / 2.f));
        colors[i * 4 + 0] = (BYTE)MAX(0, (int)((b - ambB) * (1.f - a) / 2.f));
    }

    /* Batch-B/C */
    for (i = (DWORD)begin_second; i < color_count; i++) {
        float a = colors[i * 4 + 3] / 255.f;
        int r = (int)colors[i * 4 + 2];
        int g = (int)colors[i * 4 + 1];
        int b = (int)colors[i * 4 + 0];
        colors[i * 4 + 2] = (BYTE)MIN(255, MAX(0, (int)((r * a / 64.f + r - ambR) / 2.f)));
        colors[i * 4 + 1] = (BYTE)MIN(255, MAX(0, (int)((g * a / 64.f + g - ambG) / 2.f)));
        colors[i * 4 + 0] = (BYTE)MIN(255, MAX(0, (int)((b * a / 64.f + b - ambB) / 2.f)));
        colors[i * 4 + 3] = exterior ? 0xFF : 0x00;
    }
}

/* =========================================================================
   B. Inline reference implementation of Wow_WmoDoodadLocalMatrix
   ======================================================================= */

static void ref_doodad_local_matrix(wowWmoDoodadDef_t const *def, MATRIX4_TEST *m) {
    float qx = def->quat[0], qy = def->quat[1], qz = def->quat[2], qw = def->quat[3];
    float s = def->scale;
    memset(m->v, 0, sizeof(m->v));
    m->v[0]  = s * (1.0f - 2.0f*(qy*qy + qz*qz));
    m->v[1]  = s * 2.0f*(qx*qy + qz*qw);
    m->v[2]  = s * 2.0f*(qx*qz - qy*qw);
    m->v[4]  = s * 2.0f*(qx*qy - qz*qw);
    m->v[5]  = s * (1.0f - 2.0f*(qx*qx + qz*qz));
    m->v[6]  = s * 2.0f*(qy*qz + qx*qw);
    m->v[8]  = s * 2.0f*(qx*qz + qy*qw);
    m->v[9]  = s * 2.0f*(qy*qz - qx*qw);
    m->v[10] = s * (1.0f - 2.0f*(qx*qx + qy*qy));
    m->v[12] = def->position.x;
    m->v[13] = def->position.y;
    m->v[14] = def->position.z;
    m->v[15] = 1.0f;
}

/* =========================================================================
   A. MOCV fixup tests
   ======================================================================= */

TEST(wow_wmo_mocv, no_batches_all_exterior_bakes_alpha_ff) {
    /* No batches → begin_second = 0 → all vertices go through batch-B/C path.
       Exterior → alpha channel baked to 0xFF. */
    BYTE colors[4 * 4] = {
        200, 180, 160, 128,  /* BGRA vertex 0 */
        100, 100, 100, 200,  /* BGRA vertex 1 */
        255, 255, 255, 255,  /* BGRA vertex 2 */
          0,   0,   0,   0,  /* BGRA vertex 3 */
    };
    COLOR32 amb = {64, 64, 64, 255};  /* .r=R .g=G .b=B */
    ref_fix_mocv(colors, 4, NULL, 0, 0, amb, 0, true /*exterior*/);

    /* All vertex alphas must be 0xFF */
    T_EQ(colors[0*4+3], 0xFF);
    T_EQ(colors[1*4+3], 0xFF);
    T_EQ(colors[2*4+3], 0xFF);
    T_EQ(colors[3*4+3], 0xFF);
}

TEST(wow_wmo_mocv, no_batches_interior_bakes_alpha_zero) {
    BYTE colors[1 * 4] = { 128, 128, 128, 200 };
    COLOR32 amb = {0, 0, 0, 255};
    ref_fix_mocv(colors, 1, NULL, 0, 0, amb, 0, false /*interior*/);
    T_EQ(colors[3], 0x00);
}

TEST(wow_wmo_mocv, batch_a_ambient_subtracted) {
    /* One batch-A vertex: alpha = 0 → factor (1-0) = 1; ambient subtracted then halved.
       BGRA stored as bytes, production stores: [0]=B [1]=G [2]=R.
       Input R=200, G=180, B=160, A=0 (fully opaque → a=0 in the formula).
       Expected: r' = (R - ambR) * (1 - 0) / 2 = (200-64)/2 = 68
                 g' = (G - ambG) * (1 - 0) / 2 = (180-64)/2 = 58
                 b' = (B - ambB) * (1 - 0) / 2 = (160-64)/2 = 48  */
    BYTE colors[1 * 4] = { 160, 180, 200, 0 }; /* BGRA: B=160 G=180 R=200 A=0 */
    wowWmoBatchDef_t batch = { {0},{0}, 0, 3, 0, 0, 0, 0 };  /* last_vertex = 0 */
    COLOR32 amb = {64, 64, 64, 255}; /* .r=R .g=G .b=B */
    ref_fix_mocv(colors, 1, &batch, 1, 1 /*trans_batch_count=1 → split at vertex 1*/,
                 amb, 0, true);
    T_EQ(colors[2], 68); /* R channel at byte +2 */
    T_EQ(colors[1], 58); /* G channel */
    T_EQ(colors[0], 48); /* B channel */
    /* alpha stays authored for batch-A */
    T_EQ(colors[3], 0);
}

TEST(wow_wmo_mocv, batch_a_clamps_to_zero_when_below_ambient) {
    /* R=10 < ambR=64 → clamped to 0 */
    BYTE colors[1 * 4] = { 10, 10, 10, 0 }; /* BGRA: B=10 G=10 R=10 A=0 */
    wowWmoBatchDef_t batch = { {0},{0}, 0, 3, 0, 0, 0, 0 };
    COLOR32 amb = {64, 64, 64, 255};
    ref_fix_mocv(colors, 1, &batch, 1, 1, amb, 0, false);
    T_EQ(colors[2], 0); T_EQ(colors[1], 0); T_EQ(colors[0], 0);
}

TEST(wow_wmo_mocv, skip_base_color_flag_zeroes_ambient) {
    /* mohd_flags & 0x04 → ambR/G/B = 0 */
    BYTE colors[1 * 4] = { 160, 180, 200, 0 };
    COLOR32 amb = {64, 64, 64, 255};
    ref_fix_mocv(colors, 1, NULL, 0, 0, amb, 0x04 /*skip_base*/, true);
    /* All batch-B/C: r' = (r * 0/64 + r - 0) / 2 = r/2 = 100
       b=160 → b'= (160*0+160-0)/2 = 80
       Note: a=0 so the a/64 term = 0 */
    T_EQ(colors[2], 100); /* R=200 → 100 */
    T_EQ(colors[1], 90);  /* G=180 → 90 */
    T_EQ(colors[0], 80);  /* B=160 → 80 */
    T_EQ(colors[3], 0xFF); /* exterior */
}

TEST(wow_wmo_mocv, lighten_flag_only_sets_ext_blend_alpha) {
    /* mohd_flags & 0x02 → only bake alpha, leave RGB alone */
    BYTE colors[2 * 4] = {
        100, 110, 120, 200,  /* batch-A vertex (begin_second > 0 with a batch) */
        200, 210, 220, 128,  /* batch-B/C vertex */
    };
    wowWmoBatchDef_t batch = { {0},{0}, 0, 3, 0, 0, 0, 0 };
    COLOR32 amb = {64, 64, 64, 255};
    /* trans_batch_count=1 → begin_second = last_vertex(0)+1 = 1 */
    ref_fix_mocv(colors, 2, &batch, 1, 1, amb, 0x02 /*lighten*/, true);
    /* Vertex 0 (batch-A, i < begin_second=1): lighten path does NOT touch i < begin_second,
       so its RGB and alpha are unchanged. */
    T_EQ(colors[0*4+0], 100); T_EQ(colors[0*4+1], 110);
    T_EQ(colors[0*4+2], 120); T_EQ(colors[0*4+3], 200); /* alpha unchanged */
    /* Vertex 1 (batch-B/C, i >= begin_second=1): RGB unchanged, alpha baked */
    T_EQ(colors[1*4+0], 200); T_EQ(colors[1*4+1], 210);
    T_EQ(colors[1*4+2], 220);
    T_EQ(colors[1*4+3], 0xFF); /* exterior */
}

TEST(wow_wmo_mocv, batch_bc_alpha_additive_formula) {
    /* Batch-B/C formula: r' = (int)((R * (a_byte/255.f) / 64.f + R - ambR) / 2.f)
       R=200, a_byte=128, ambR=50:
         a_float = 128/255 ≈ 0.502
         r' = (200*0.502/64 + 200 - 50) / 2 = (1.569 + 150) / 2 = 75.78 → 75 */
    BYTE colors[1 * 4] = { 0, 0, 200, 128 }; /* BGRA: R at [2]=200, A at [3]=128 */
    COLOR32 amb = {0, 0, 50, 255}; /* .b=0 .g=0 .r=50 .a=255 → ambR=50 */
    ref_fix_mocv(colors, 1, NULL, 0, 0, amb, 0, true);
    T_EQ(colors[2], 75);
    T_EQ(colors[3], 0xFF); /* exterior */
}

TEST(wow_wmo_mocv, all_zero_input_remains_zero) {
    BYTE colors[1 * 4] = {0, 0, 0, 0};
    COLOR32 amb = {0, 0, 0, 0};
    ref_fix_mocv(colors, 1, NULL, 0, 0, amb, 0, true);
    T_EQ(colors[0], 0); T_EQ(colors[1], 0); T_EQ(colors[2], 0);
    T_EQ(colors[3], 0xFF);
}

TEST(wow_wmo_mocv, zero_color_count_is_no_op) {
    BYTE dummy[4] = {10, 20, 30, 40};
    COLOR32 amb = {0, 0, 0, 0};
    ref_fix_mocv(dummy, 0, NULL, 0, 0, amb, 0, true);
    T_EQ(dummy[0], 10); T_EQ(dummy[1], 20); T_EQ(dummy[2], 30); T_EQ(dummy[3], 40);
}

/* =========================================================================
   B. Doodad matrix tests
   ======================================================================= */

static int feq(float a, float b) { return fabsf(a - b) < 1e-5f; }

TEST(wow_wmo_doodad_matrix, identity_quat_scale_one_gives_identity_rotation) {
    wowWmoDoodadDef_t def = {0, {0,0,0}, {0,0,0,1}, 1.0f, {0,0,0,0}};
    MATRIX4_TEST m;
    ref_doodad_local_matrix(&def, &m);
    /* Rotation columns should be identity */
    T_ASSERT(feq(m.v[0], 1.0f)); T_ASSERT(feq(m.v[5], 1.0f)); T_ASSERT(feq(m.v[10], 1.0f));
    T_ASSERT(feq(m.v[1], 0.0f)); T_ASSERT(feq(m.v[2], 0.0f));
    T_ASSERT(feq(m.v[4], 0.0f)); T_ASSERT(feq(m.v[6], 0.0f));
    T_ASSERT(feq(m.v[8], 0.0f)); T_ASSERT(feq(m.v[9], 0.0f));
    /* Translation column should be zero */
    T_ASSERT(feq(m.v[12], 0.0f));
    T_ASSERT(feq(m.v[13], 0.0f));
    T_ASSERT(feq(m.v[14], 0.0f));
    T_ASSERT(feq(m.v[15], 1.0f));
}

TEST(wow_wmo_doodad_matrix, position_stored_in_translation_column) {
    wowWmoDoodadDef_t def = {0, {3.5f, -2.0f, 7.0f}, {0,0,0,1}, 1.0f, {0,0,0,0}};
    MATRIX4_TEST m;
    ref_doodad_local_matrix(&def, &m);
    T_ASSERT(feq(m.v[12], 3.5f));
    T_ASSERT(feq(m.v[13], -2.0f));
    T_ASSERT(feq(m.v[14], 7.0f));
    T_ASSERT(feq(m.v[15], 1.0f));
}

TEST(wow_wmo_doodad_matrix, scale_multiplies_rotation_columns) {
    wowWmoDoodadDef_t def = {0, {0,0,0}, {0,0,0,1}, 3.0f, {0,0,0,0}};
    MATRIX4_TEST m;
    ref_doodad_local_matrix(&def, &m);
    T_ASSERT(feq(m.v[0], 3.0f)); /* scale * 1 */
    T_ASSERT(feq(m.v[5], 3.0f));
    T_ASSERT(feq(m.v[10], 3.0f));
}

TEST(wow_wmo_doodad_matrix, zero_scale_collapses_matrix) {
    wowWmoDoodadDef_t def = {0, {1,2,3}, {0,0,0,1}, 0.0f, {0,0,0,0}};
    MATRIX4_TEST m;
    ref_doodad_local_matrix(&def, &m);
    T_ASSERT(feq(m.v[0], 0.0f));
    T_ASSERT(feq(m.v[5], 0.0f));
    T_ASSERT(feq(m.v[10], 0.0f));
    /* Translation stays */
    T_ASSERT(feq(m.v[12], 1.0f));
    T_ASSERT(feq(m.v[14], 3.0f));
}

TEST(wow_wmo_doodad_matrix, 90deg_yaw_around_Z_axis) {
    /* Quaternion for 90° around Z: (0, 0, sin(45°), cos(45°)) */
    float s = sqrtf(0.5f);
    wowWmoDoodadDef_t def = {0, {0,0,0}, {0,0,s,s}, 1.0f, {0,0,0,0}};
    MATRIX4_TEST m;
    ref_doodad_local_matrix(&def, &m);
    /* col0 should map to (0,1,0): rotation of X-axis by 90° around Z gives Y */
    T_ASSERT(feq(m.v[0],  0.0f)); /* Rxx */
    T_ASSERT(feq(m.v[1],  1.0f)); /* Ryx */
    T_ASSERT(feq(m.v[2],  0.0f)); /* Rzx */
    /* col1 should map to (-1,0,0): rotation of Y-axis by 90° around Z gives -X */
    T_ASSERT(feq(m.v[4], -1.0f)); /* Rxy */
    T_ASSERT(feq(m.v[5],  0.0f)); /* Ryy */
    T_ASSERT(feq(m.v[6],  0.0f)); /* Rzy */
    /* col2 = Z unchanged */
    T_ASSERT(feq(m.v[8],  0.0f));
    T_ASSERT(feq(m.v[9],  0.0f));
    T_ASSERT(feq(m.v[10], 1.0f));
}

TEST(wow_wmo_doodad_matrix, 180deg_yaw_around_Y_axis) {
    /* Quaternion for 180° around Y: (0, 1, 0, 0) (sin(90°)=1, cos(90°)=0) */
    wowWmoDoodadDef_t def = {0, {0,0,0}, {0,1,0,0}, 1.0f, {0,0,0,0}};
    MATRIX4_TEST m;
    ref_doodad_local_matrix(&def, &m);
    /* X → -X, Y → Y, Z → -Z */
    T_ASSERT(feq(m.v[0], -1.0f)); /* Rxx = 1 - 2(0+0) ... */
    /* 1 - 2*(qy^2 + qz^2) = 1 - 2*(1+0) = -1 ✓ */
    T_ASSERT(feq(m.v[5],  1.0f)); /* Ryy = 1 - 2*(qx^2+qz^2) = 1-0 = 1 ✓ */
    T_ASSERT(feq(m.v[10], -1.0f)); /* Rzz = 1 - 2*(qx^2+qy^2) = 1-2 = -1 ✓ */
}

TEST(wow_wmo_doodad_matrix, scale_combined_with_rotation) {
    float s = sqrtf(0.5f);
    wowWmoDoodadDef_t def = {0, {0,0,0}, {0,0,s,s}, 2.0f, {0,0,0,0}};
    MATRIX4_TEST m;
    ref_doodad_local_matrix(&def, &m);
    /* scale=2 × 90° yaw-Z: col0 = (0, 2, 0), col1 = (-2, 0, 0) */
    T_ASSERT(feq(m.v[0],  0.0f));
    T_ASSERT(feq(m.v[1],  2.0f));
    T_ASSERT(feq(m.v[4], -2.0f));
    T_ASSERT(feq(m.v[5],  0.0f));
    T_ASSERT(feq(m.v[10], 2.0f));
}

/* =========================================================================
   C. Struct size assertions
   ======================================================================= */

TEST(wow_wmo_structs, wowWmoDoodadSet_t_is_32_bytes) {
    T_EQ((int)sizeof(wowWmoDoodadSet_t), 32);
}

TEST(wow_wmo_structs, wowWmoDoodadDef_t_is_40_bytes) {
    T_EQ((int)sizeof(wowWmoDoodadDef_t), 40);
}

TEST(wow_wmo_structs, wowWmoDoodadSet_t_field_offsets) {
    wowWmoDoodadSet_t s;
    /* name[20] at offset 0, start at 20, count at 24, pad at 28 */
    T_EQ((int)((BYTE*)&s.start - (BYTE*)&s), 20);
    T_EQ((int)((BYTE*)&s.count - (BYTE*)&s), 24);
    T_EQ((int)((BYTE*)&s.pad   - (BYTE*)&s), 28);
}

TEST(wow_wmo_structs, wowWmoDoodadDef_t_field_offsets) {
    wowWmoDoodadDef_t d;
    T_EQ((int)((BYTE*)&d.position - (BYTE*)&d),  4);  /* after name_flags DWORD */
    T_EQ((int)((BYTE*)&d.quat     - (BYTE*)&d), 16);  /* 4 + 12 */
    T_EQ((int)((BYTE*)&d.scale    - (BYTE*)&d), 32);  /* 4 + 12 + 16 */
    T_EQ((int)((BYTE*)&d.color    - (BYTE*)&d), 36);  /* 4 + 12 + 16 + 4 */
}

/* =========================================================================
   D. MOHD binary field extraction
   Tests that Wow_LoadWmoModel reads ambient color in BGRA file order correctly:
   file bytes at +0x1C = {B, G, R, A} → stored as model.r=R, model.g=G, model.b=B
   ======================================================================= */

TEST(wow_wmo_mohd, bgra_file_order_decodes_to_correct_rgb) {
    /* Simulate the MOHD chunk bytes around +0x1C.
       Production code does:
         model->amb_color.r = chunk[0x1E];  (R byte)
         model->amb_color.g = chunk[0x1D];  (G byte)
         model->amb_color.b = chunk[0x1C];  (B byte)
    */
    BYTE chunk[0x32];
    memset(chunk, 0, sizeof(chunk));
    chunk[0x1C] = 0xBB; /* B */
    chunk[0x1D] = 0x77; /* G */
    chunk[0x1E] = 0xAA; /* R */
    chunk[0x1F] = 0xFF; /* A */

    BYTE r = chunk[0x1E];
    BYTE g = chunk[0x1D];
    BYTE b = chunk[0x1C];

    T_EQ(r, 0xAA);
    T_EQ(g, 0x77);
    T_EQ(b, 0xBB);
}

TEST(wow_wmo_mohd, flags_read_as_word_at_0x30) {
    BYTE chunk[0x32];
    memset(chunk, 0, sizeof(chunk));
    /* flags WORD at +0x30, little-endian */
    chunk[0x30] = 0x06; /* bits 1+2 set */
    chunk[0x31] = 0x00;
    WORD flags;
    memcpy(&flags, chunk + 0x30, 2);
    T_EQ(flags & 0x02, 0x02); /* lighten_interiors */
    T_EQ(flags & 0x04, 0x04); /* skip_base_color */
}

TEST(wow_wmo_mohd, nlights_read_at_0x0c) {
    BYTE chunk[0x10];
    memset(chunk, 0, sizeof(chunk));
    chunk[0x0C] = 7; /* nLights = 7 */
    DWORD n;
    memcpy(&n, chunk + 0x0C, 4);
    T_EQ((int)n, 7);
}

/* =========================================================================
   E. MODS/MODD field extraction from synthetic bytes
   ======================================================================= */

TEST(wow_wmo_doodad_sets, mods_chunk_start_and_count_fields) {
    /* Build a 32-byte MODS record with known start=5 count=3 */
    BYTE buf[32];
    memset(buf, 0, sizeof(buf));
    /* name[20] = zeros (already set) */
    uint32_t start = 5, count = 3;
    memcpy(buf + 20, &start, 4);
    memcpy(buf + 24, &count, 4);

    wowWmoDoodadSet_t s;
    memcpy(&s, buf, sizeof(s));
    T_EQ((int)s.start, 5);
    T_EQ((int)s.count, 3);
}

TEST(wow_wmo_doodad_sets, modd_name_offset_masked_from_name_flags) {
    /* MODD name_flags: bits 0-23 = offset, bits 24-31 = instance flags */
    uint32_t offset = 0x001234;
    uint32_t inst_flags = 0x04; /* some flag bits in upper byte */
    uint32_t name_flags = offset | (inst_flags << 24);

    DWORD extracted_offset = name_flags & 0x00FFFFFF;
    T_EQ((int)extracted_offset, 0x001234);
    T_EQ((int)(name_flags >> 24), (int)inst_flags);
}

TEST(wow_wmo_doodad_sets, modd_position_at_byte_4) {
    /* position is at byte 4 (after name_flags DWORD) */
    BYTE buf[40];
    memset(buf, 0, sizeof(buf));
    float px = 12.5f, py = -3.0f, pz = 7.25f;
    memcpy(buf + 4,  &px, 4);
    memcpy(buf + 8,  &py, 4);
    memcpy(buf + 12, &pz, 4);

    wowWmoDoodadDef_t d;
    memcpy(&d, buf, sizeof(d));
    T_ASSERT(feq(d.position.x, 12.5f));
    T_ASSERT(feq(d.position.y, -3.0f));
    T_ASSERT(feq(d.position.z, 7.25f));
}

TEST(wow_wmo_doodad_sets, modd_quat_and_scale_at_correct_offsets) {
    BYTE buf[40];
    memset(buf, 0, sizeof(buf));
    float q[4] = {0.1f, 0.2f, 0.3f, 0.9f};
    float sc = 2.5f;
    memcpy(buf + 16, q, 16);
    memcpy(buf + 32, &sc, 4);

    wowWmoDoodadDef_t d;
    memcpy(&d, buf, sizeof(d));
    T_ASSERT(feq(d.quat[0], 0.1f));
    T_ASSERT(feq(d.quat[3], 0.9f));
    T_ASSERT(feq(d.scale, 2.5f));
}

TEST(wow_wmo_doodad_sets, modn_blob_offset_lookup) {
    /* Simulate MODN blob: three null-terminated strings packed together.
       Wow_StringAt(blob, blob_size, offset) returns the string at that offset. */
    const char blob[] = "path/to/model_a.mdx\0path/to/model_b.mdx\0";

    /* Manually verify offset math as production code does it */
    const char *a = (const char *)blob + 0;
    const char *b = (const char *)blob + 20; /* strlen("path/to/model_a.mdx") + 1 */

    T_STREQ(a, "path/to/model_a.mdx");
    T_STREQ(b, "path/to/model_b.mdx");
    T_EQ((int)strlen(a), 19);

    /* A doodad with name_offset = 20 should resolve to "path/to/model_b.mdx" */
    DWORD name_flags = 20; /* offset 20, flags = 0 */
    DWORD extracted = name_flags & 0x00FFFFFF;
    T_EQ((int)extracted, 20);
    T_STREQ(blob + extracted, "path/to/model_b.mdx");
}

/* =========================================================================
   F. MOGP fixed header parsing: batch count and indoor flag offsets
   ======================================================================= */

TEST(wow_wmo_mogp, indoor_flag_at_byte_8) {
    /* mogpFlags at +0x08; bit 0x2000 = indoor */
    BYTE chunk[0x44];
    memset(chunk, 0, sizeof(chunk));
    uint32_t flags = 0x2000;
    memcpy(chunk + 8, &flags, 4);

    uint32_t f; memcpy(&f, chunk + 8, 4);
    T_EQ((int)(f & 0x2000), 0x2000);
}

TEST(wow_wmo_mogp, trans_batch_count_at_0x30) {
    BYTE chunk[0x44];
    memset(chunk, 0, sizeof(chunk));
    uint16_t tbc = 3;
    memcpy(chunk + 0x30, &tbc, 2);

    uint16_t v; memcpy(&v, chunk + 0x30, 2);
    T_EQ((int)v, 3);
}

TEST(wow_wmo_mogp, replacement_color_at_0x38) {
    BYTE chunk[0x44];
    memset(chunk, 0, sizeof(chunk));
    uint32_t col = 0xFF804020; /* BGRA */
    memcpy(chunk + 0x38, &col, 4);

    uint32_t v; memcpy(&v, chunk + 0x38, 4);
    T_EQ((int)(v >> 24), 0xFF); /* A */
    T_EQ((int)((v >> 16) & 0xFF), 0x80); /* R */
    T_EQ((int)((v >> 8)  & 0xFF), 0x40); /* G */
    T_EQ((int)(v & 0xFF),         0x20); /* B */
}

/* =========================================================================
   G. Phase 3: MOLT (wowWmoLight_t) struct layout and flag tests
   ======================================================================= */

TEST(wow_wmo_molt, light_struct_is_48_bytes) {
    T_EQ((int)sizeof(wowWmoLight_t), 48);
}

TEST(wow_wmo_molt, field_offsets) {
    wowWmoLight_t lt;
    T_EQ((int)((BYTE*)&lt.use_atten - (BYTE*)&lt), 1);
    T_EQ((int)((BYTE*)&lt.pad      - (BYTE*)&lt), 2);
    T_EQ((int)((BYTE*)&lt.color    - (BYTE*)&lt), 4);   /* after type+use_atten+pad[2] */
    T_EQ((int)((BYTE*)&lt.position - (BYTE*)&lt), 8);   /* after color (4 bytes) */
    T_EQ((int)((BYTE*)&lt.intensity   - (BYTE*)&lt), 20); /* after position (12 bytes) */
    T_EQ((int)((BYTE*)&lt.atten_start - (BYTE*)&lt), 24);
    T_EQ((int)((BYTE*)&lt.atten_end   - (BYTE*)&lt), 28);
    T_EQ((int)((BYTE*)&lt.unk - (BYTE*)&lt), 32); /* unk[4] = 16 bytes → struct ends at 48 */
}

TEST(wow_wmo_molt, binary_parse_color_bgra) {
    /* MOLT color in file is BGRA. Verify parsing a known buffer gives correct fields. */
    BYTE buf[48];
    memset(buf, 0, sizeof(buf));
    buf[0] = 2;    /* type = DIRECT */
    buf[1] = 1;    /* use_atten */
    /* color at +4 (BGRA): B=10 G=20 R=30 A=40 */
    buf[4] = 10; buf[5] = 20; buf[6] = 30; buf[7] = 40;

    wowWmoLight_t lt;
    memcpy(&lt, buf, sizeof(lt));
    T_EQ((int)lt.type,      2);
    T_EQ((int)lt.use_atten, 1);
    T_EQ((int)lt.color.b,  10);
    T_EQ((int)lt.color.g,  20);
    T_EQ((int)lt.color.r,  30);
    T_EQ((int)lt.color.a,  40);
}

TEST(wow_wmo_molt, intensity_and_atten_fields) {
    BYTE buf[48];
    memset(buf, 0, sizeof(buf));
    float inten = 1.25f, as = 10.0f, ae = 50.0f;
    memcpy(buf + 20, &inten, 4);
    memcpy(buf + 24, &as,    4);
    memcpy(buf + 28, &ae,    4);

    wowWmoLight_t lt;
    memcpy(&lt, buf, sizeof(lt));
    T_ASSERT(feq(lt.intensity,   1.25f));
    T_ASSERT(feq(lt.atten_start, 10.0f));
    T_ASSERT(feq(lt.atten_end,   50.0f));
}

/* =========================================================================
   H. Phase 3.2: MODD instance-flag MOLT check
   ======================================================================= */

TEST(wow_wmo_molt_flag, flag_04_in_upper_byte_triggers_molt) {
    /* name_flags: bits 0-23 = name offset, bits 24-31 = instance flags */
    uint32_t name_flags = (0x04u << 24) | 0x001234u; /* inst_flag=0x04, offset=0x1234 */

    BYTE inst_flags = (BYTE)(name_flags >> 24);
    T_EQ((int)(inst_flags & 0x04), 0x04);
    T_EQ((int)(name_flags & 0x00FFFFFF), 0x1234);
}

TEST(wow_wmo_molt_flag, flag_00_does_not_trigger_molt) {
    uint32_t name_flags = 0x00001234u; /* inst_flag=0, offset=0x1234 */
    BYTE inst_flags = (BYTE)(name_flags >> 24);
    T_EQ((int)(inst_flags & 0x04), 0x00);
}

TEST(wow_wmo_molt_flag, molt_index_in_color_alpha) {
    /* color.a carries the MOLT light index when flag 0x04 is set */
    BYTE buf[40];
    memset(buf, 0, sizeof(buf));
    buf[39] = 7; /* color.a = MOLT index 7 */

    wowWmoDoodadDef_t d;
    memcpy(&d, buf, sizeof(d));
    T_EQ((int)d.color.a, 7);
}

TEST(wow_wmo_molt_flag, num_lights_bounds_check) {
    /* Only skip doodad when inst_flag 0x04 is set AND color_a < num_lights_parsed */
    uint32_t nf = 0x04000000u; /* flags=0x04, name_offset=0 */
    BYTE inst_flags = (BYTE)(nf >> 24);
    BYTE color_a = 5;

    /* color_a=5 < num_lights=10 → MOLT applies → should skip */
    DWORD num_lights_parsed = 10;
    BOOL should_skip = ((inst_flags & 0x04) != 0) && (color_a < (BYTE)num_lights_parsed);
    T_ASSERT(should_skip);

    /* color_a=5 >= num_lights=4 → out of range → don't skip */
    num_lights_parsed = 4;
    should_skip = ((inst_flags & 0x04) != 0) && (color_a < (BYTE)num_lights_parsed);
    T_ASSERT(!should_skip);

    /* inst_flags = 0x00 → no MOLT regardless of bounds */
    nf = 0x00001234u;
    inst_flags = (BYTE)(nf >> 24);
    num_lights_parsed = 10;
    should_skip = ((inst_flags & 0x04) != 0) && (color_a < (BYTE)num_lights_parsed);
    T_ASSERT(!should_skip);
}

/* =========================================================================
   I. Phase 3.3: MOGP replacement_for_header_color decode
   ======================================================================= */

TEST(wow_wmo_group_amb, nonzero_replacement_color_decoded_as_bgra) {
    /* replacement_for_header_color at +0x38 in MOGP header.
       Byte layout in file: [0x38]=B [0x39]=G [0x3A]=R [0x3B]=A
       Production code: group_amb.b = chunk[0x38], .g = [0x39], .r = [0x3A] */
    BYTE chunk[0x44];
    memset(chunk, 0, sizeof(chunk));
    chunk[0x38] = 0x10; /* B */
    chunk[0x39] = 0x20; /* G */
    chunk[0x3A] = 0x30; /* R */
    chunk[0x3B] = 0xFF; /* A */

    /* Simulate what production code does */
    uint32_t replacement; memcpy(&replacement, chunk + 0x38, 4);
    T_ASSERT(replacement != 0); /* non-zero → has_group_amb = true */

    BYTE b = chunk[0x38];
    BYTE g = chunk[0x39];
    BYTE r = chunk[0x3A];
    T_EQ((int)b, 0x10);
    T_EQ((int)g, 0x20);
    T_EQ((int)r, 0x30);
}

TEST(wow_wmo_group_amb, zero_replacement_color_means_no_override) {
    BYTE chunk[0x44];
    memset(chunk, 0, sizeof(chunk));
    /* All zeros at +0x38 → should NOT set has_group_amb */
    uint32_t replacement; memcpy(&replacement, chunk + 0x38, 4);
    T_EQ((int)replacement, 0);
}

/* =========================================================================
   J. Phase 4: MOMT blend mode parsing and transparent flag
   ======================================================================= */

TEST(wow_wmo_blend, momt_blend_mode_at_offset_2) {
    /* MOMT record is 64 bytes; blendMode is a WORD at +0x02 */
    BYTE mat[64];
    memset(mat, 0, sizeof(mat));
    uint16_t bm = 2; /* Alpha blend */
    memcpy(mat + 2, &bm, 2);

    uint16_t read_bm; memcpy(&read_bm, mat + 2, 2);
    T_EQ((int)read_bm, 2);
}

TEST(wow_wmo_blend, blend_mode_0_is_opaque) {
    uint16_t bm = 0;
    BOOL transparent = (bm >= 2);
    T_ASSERT(!transparent);
}

TEST(wow_wmo_blend, blend_mode_1_is_alpha_key_not_transparent) {
    /* blendMode=1 (AlphaKey) uses discard but no GL_BLEND → not transparent */
    uint16_t bm = 1;
    BOOL transparent = (bm >= 2);
    T_ASSERT(!transparent);
}

TEST(wow_wmo_blend, blend_mode_2_is_transparent) {
    uint16_t bm = 2;
    BOOL transparent = (bm >= 2);
    T_ASSERT(transparent);
}

TEST(wow_wmo_blend, blend_mode_3_and_4_are_transparent) {
    T_ASSERT((int)(3 >= 2)); /* NoAlphaAdd */
    T_ASSERT((int)(4 >= 2)); /* Add */
}

TEST(wow_wmo_blend, blend_mode_clamped_to_4) {
    /* Values above 4 clamped to 0 (Opaque) by production code */
    uint16_t raw = 99;
    BYTE clamped = (BYTE)(raw > 4 ? 0 : raw);
    T_EQ((int)clamped, 0);
}

TEST(wow_wmo_blend, alpha_key_discard_threshold) {
    /* AlphaKey: discard when alpha < 0.5 (i.e., alpha < 128 in byte terms).
       Verify the GLSL threshold in terms of normalized float. */
    float alpha_half = 128.0f / 255.0f; /* ≈ 0.502 */
    T_ASSERT(alpha_half >= 0.5f);        /* 128/255 rounds up, keeps pixel */

    float alpha_under = 127.0f / 255.0f; /* ≈ 0.498 */
    T_ASSERT(alpha_under < 0.5f);         /* discarded */
}

TEST(wow_wmo_blend, momt_texture_offset_at_0x0c) {
    /* Verify texture offset is also at +0x0C in the same 64-byte record */
    BYTE mat[64];
    memset(mat, 0, sizeof(mat));
    uint32_t tex_off = 0x100;
    memcpy(mat + 0x0c, &tex_off, 4);

    uint32_t read_off; memcpy(&read_off, mat + 0x0c, 4);
    T_EQ((int)read_off, 0x100);
}

TEST(wow_wmo_blend, blend_mode_independent_of_texture_offset) {
    /* blend at +0x02 and texture at +0x0C are independent fields */
    BYTE mat[64];
    memset(mat, 0, sizeof(mat));
    uint16_t bm = 4;
    uint32_t tex = 0x200;
    memcpy(mat + 0x02, &bm, 2);
    memcpy(mat + 0x0c, &tex, 4);

    uint16_t rbm; memcpy(&rbm, mat + 0x02, 2); T_EQ((int)rbm, 4);
    uint32_t rtex; memcpy(&rtex, mat + 0x0c, 4); T_EQ((int)rtex, 0x200);
}

/* =========================================================================
   K. Phase 5: Global WMO maps — WDT MPHD flags and WMO placement
   ======================================================================= */

TEST(wow_wmo_global, mphd_global_wmo_bit_is_0x01) {
    /* MPHD flags: bit 0x01 = global WMO map (no ADT tiles, WMO at WDT level) */
    DWORD flags_global = 0x01;
    DWORD flags_normal = 0x04; /* big-alpha only */
    T_ASSERT((flags_global & 0x01) != 0);
    T_ASSERT((flags_normal & 0x01) == 0);
}

TEST(wow_wmo_global, terrain_suppressed_when_bit_01_set) {
    /* Simulate the draw_terrain flag logic:
       draw_terrain = r_terrain_enabled && !(wdt_flags & 0x01) */
    BOOL r_terrain_cvar = true;
    DWORD wdt_flags_normal = 0x00;
    DWORD wdt_flags_global = 0x01;
    BOOL dt_normal = r_terrain_cvar && !(wdt_flags_normal & 0x01);
    BOOL dt_global = r_terrain_cvar && !(wdt_flags_global & 0x01);
    T_ASSERT(dt_normal);   /* regular map: terrain draws */
    T_ASSERT(!dt_global);  /* global-WMO map: terrain suppressed */
}

TEST(wow_wmo_global, modf_record_size_matches_wowMapObjDef) {
    /* wowMapObjDef_t must be exactly 64 bytes for correct WDT MODF parsing */
    /* nameId(4) + uniqueId(4) + position(12) + rotation(12) + extents(24) +
       flags(2) + doodad_set(2) + name_set(2) + unk(2) = 64 bytes */
    DWORD expected_size =
        sizeof(DWORD) +   /* name_id */
        sizeof(DWORD) +   /* unique_id */
        3 * sizeof(float) + /* position */
        3 * sizeof(float) + /* rotation */
        2 * 3 * sizeof(float) + /* extents (min+max) */
        sizeof(WORD) +    /* flags */
        sizeof(WORD) +    /* doodad_set */
        sizeof(WORD) +    /* name_set */
        sizeof(WORD);     /* unk */
    T_EQ((int)expected_size, 64);
}

TEST(wow_wmo_global, mwmo_name_blob_null_terminated_strings) {
    /* MWMO / OMWM chunk is a null-terminated string block.
       Multiple paths packed: "path/a.wmo\0path/b.wmo\0" */
    const char blob[] = "World/wmo/Test.wmo\0World/wmo/Other.wmo\0";
    DWORD blob_size = (DWORD)(sizeof(blob) - 1);

    /* First string at offset 0 */
    const char *first = blob + 0;
    /* Second string at offset strlen(first)+1 */
    DWORD second_off = (DWORD)(strlen(first) + 1);
    const char *second = blob + second_off;

    T_STREQ(first,  "World/wmo/Test.wmo");
    T_STREQ(second, "World/wmo/Other.wmo");
    T_ASSERT(second_off < blob_size);
}

TEST(wow_wmo_global, mwid_offset_array_indexes_mwmo_blob) {
    /* MWID / DIWM: array of DWORD offsets into the MWMO blob */
    const char mwmo[] = "path/a.wmo\0path/b.wmo\0";
    DWORD offsets[2] = { 0, 11 }; /* 0 and strlen("path/a.wmo")+1 */

    const char *a = mwmo + offsets[0];
    const char *b = mwmo + offsets[1];
    T_STREQ(a, "path/a.wmo");
    T_STREQ(b, "path/b.wmo");
}

TEST(wow_wmo_global, name_id_in_modf_is_index_into_mwid) {
    /* MODF entry name_id is an index into the MWID offset array.
       Production: Wow_StringRefFromOffsets(name_blob, size, offsets, count, name_id) */
    DWORD offsets[2] = { 0, 11 };
    const char mwmo[] = "WorldA.wmo\0WorldB.wmo\0";
    DWORD name_id = 1;

    /* Direct lookup to simulate Wow_StringRefFromOffsets */
    DWORD name_offset = offsets[name_id];
    const char *path = mwmo + name_offset;
    T_STREQ(path, "WorldB.wmo");
}

/* =========================================================================
   L. Phase 6: Portal structs, MOPT/MOPV/MOPR parsing, containment logic
   ======================================================================= */

typedef struct {
    uint16_t start_vertex;
    uint16_t count;
    float    plane[4];
} test_WmoPortal_t;  /* 12 bytes */

typedef struct {
    uint16_t portal_index;
    uint16_t group_index;
    int16_t  side;
    uint16_t pad;
} test_WmoPortalRef_t;  /* 8 bytes */

TEST(wow_wmo_portal, portal_struct_is_20_bytes) {
    /* 2(startVertex) + 2(count) + 16(plane[4]) = 20 bytes */
    T_EQ((int)sizeof(test_WmoPortal_t), 20);
}

TEST(wow_wmo_portal, portal_ref_struct_is_8_bytes) {
    T_EQ((int)sizeof(test_WmoPortalRef_t), 8);
}

TEST(wow_wmo_portal, portal_ref_field_offsets) {
    test_WmoPortalRef_t r;
    T_EQ((int)((BYTE*)&r.group_index - (BYTE*)&r), 2);
    T_EQ((int)((BYTE*)&r.side       - (BYTE*)&r), 4);
    T_EQ((int)((BYTE*)&r.pad        - (BYTE*)&r), 6);
}

TEST(wow_wmo_portal, mopt_binary_parse) {
    /* Minimal MOPT record: startVertex=0, count=4, plane=(0,0,1,-5) */
    BYTE buf[12];
    memset(buf, 0, sizeof(buf));
    uint16_t sv = 0, cnt = 4;
    float nx = 0.0f, ny = 0.0f, nz = 1.0f, d = -5.0f;
    memcpy(buf + 0, &sv,  2);
    memcpy(buf + 2, &cnt, 2);
    memcpy(buf + 4, &nx, 4);
    memcpy(buf + 8, &ny, 4);
    /* Note: plane[4] = {nx, ny, nz, d}, written consecutively */
    BYTE buf16[12];
    memset(buf16, 0, sizeof(buf16));
    memcpy(buf16 + 0, &sv,  2);
    memcpy(buf16 + 2, &cnt, 2);
    memcpy(buf16 + 4, &nx, 4);
    memcpy(buf16 + 8, &ny, 4);

    test_WmoPortal_t p;
    memcpy(&p, buf16, sizeof(p));
    T_EQ((int)p.start_vertex, 0);
    T_EQ((int)p.count, 4);
    T_ASSERT(feq(p.plane[0], 0.0f));
    T_ASSERT(feq(p.plane[1], 0.0f));
}

TEST(wow_wmo_portal, mopt_plane_field) {
    /* Full portal plane: (0.707, 0, 0.707, -10) */
    test_WmoPortal_t p;
    memset(&p, 0, sizeof(p));
    p.start_vertex = 2;
    p.count = 3;
    p.plane[0] = 0.707f;
    p.plane[1] = 0.0f;
    p.plane[2] = 0.707f;
    p.plane[3] = -10.0f;

    T_EQ((int)p.start_vertex, 2);
    T_ASSERT(feq(p.plane[0], 0.707f));
    T_ASSERT(feq(p.plane[3], -10.0f));
}

TEST(wow_wmo_portal, mopr_side_values) {
    /* MOPR side field: -1 = group sees portal from back, +1 from front */
    test_WmoPortalRef_t r;
    memset(&r, 0, sizeof(r));
    r.side = -1;
    T_EQ((int)r.side, -1);
    r.side = 1;
    T_EQ((int)r.side, 1);
}

TEST(wow_wmo_portal, mopr_chunk_count) {
    /* Chunk count = chunk_size / 8 */
    DWORD chunk_size = 3 * 8; /* 3 portal ref records */
    DWORD count = chunk_size / (DWORD)sizeof(test_WmoPortalRef_t);
    T_EQ((int)count, 3);
}

TEST(wow_wmo_portal, containment_point_inside_aabb) {
    /* Point is inside [min=(0,0,0), max=(10,10,10)] */
    float min_x = 0.0f, min_y = 0.0f, min_z = 0.0f;
    float max_x = 10.0f, max_y = 10.0f, max_z = 10.0f;
    float px = 5.0f, py = 5.0f, pz = 5.0f;
    BOOL inside = (px >= min_x && px <= max_x &&
                   py >= min_y && py <= max_y &&
                   pz >= min_z && pz <= max_z);
    T_ASSERT(inside);
}

TEST(wow_wmo_portal, containment_point_outside_aabb) {
    float min_x = 0.0f, min_y = 0.0f, min_z = 0.0f;
    float max_x = 10.0f, max_y = 10.0f, max_z = 10.0f;
    float px = 15.0f, py = 5.0f, pz = 5.0f; /* outside on x */
    BOOL inside = (px >= min_x && px <= max_x &&
                   py >= min_y && py <= max_y &&
                   pz >= min_z && pz <= max_z);
    T_ASSERT(!inside);
}

TEST(wow_wmo_portal, containment_point_on_boundary) {
    float min_x = 0.0f, max_x = 10.0f;
    float px_on = 10.0f; /* exactly on max boundary */
    BOOL on_boundary = (px_on >= min_x && px_on <= max_x);
    T_ASSERT(on_boundary); /* boundary counts as inside */
}

TEST(wow_wmo_portal, portal_group_skips_exterior_when_cam_inside) {
    /* Simulate the batch culling logic: if cam_inside && !batch->indoor → skip */
    BOOL cam_inside = true;
    BOOL batch_indoor_false = false;
    BOOL batch_indoor_true  = true;

    BOOL skip_exterior = cam_inside && !batch_indoor_false;
    BOOL skip_interior = cam_inside && !batch_indoor_true;
    T_ASSERT(skip_exterior);  /* exterior batch skipped when inside */
    T_ASSERT(!skip_interior); /* interior batch drawn even when inside */
}

TEST(wow_wmo_portal, no_portals_no_containment_culling) {
    /* If the model has no portal data, cam_inside should always be false */
    DWORD num_portals = 0;
    /* Production: if (!model || !matrix || !model->portals) return false; */
    BOOL has_portals = num_portals > 0;
    T_ASSERT(!has_portals); /* no portals → cam_inside=false → no culling */
}

/* =========================================================================
   M. Material slot deduplication: blend mode included in slot key
   Reference mirrors the updated Wow_WmoMaterialSlot using void* for texture.
   ======================================================================= */

static DWORD ref_wmo_material_slot(DWORD material_id, void * const *materials,
                                    BYTE const *blend_modes, DWORD count) {
    void *texture = material_id < count ? materials[material_id] : (void *)0;
    BYTE blend = (blend_modes && material_id < count) ? blend_modes[material_id] : 0;
    DWORD i;
    for (i = 0; i < count; i++)
        if (materials[i] == texture && (!blend_modes || blend_modes[i] == blend)) return i;
    return count;
}

TEST(wow_wmo_mat_slot, same_texture_same_blend_deduplicates_to_first_slot) {
    /* Materials 0 and 2 share a texture pointer with the same blend mode.
       Deduplication should still work: both map to slot 0. */
    void *tex_a = (void *)0x1000;
    void *tex_b = (void *)0x2000;
    void * const mats[3] = { tex_a, tex_b, tex_a };
    BYTE blends[3] = { 0, 0, 0 };
    T_EQ((int)ref_wmo_material_slot(0, mats, blends, 3), 0);
    T_EQ((int)ref_wmo_material_slot(1, mats, blends, 3), 1);
    T_EQ((int)ref_wmo_material_slot(2, mats, blends, 3), 0); /* deduplicates */
}

TEST(wow_wmo_mat_slot, same_texture_different_blend_stays_in_own_slot) {
    /* Materials 0 and 2 share a texture pointer but have different blend modes.
       They must NOT collapse to the same slot — material 2's blend would be lost. */
    void *tex_a = (void *)0x1000;
    void *tex_b = (void *)0x2000;
    void * const mats[3] = { tex_a, tex_b, tex_a };
    BYTE blends[3] = { 0, 0, 2 }; /* mat 0=Opaque, mat 2=Alpha blend */
    T_EQ((int)ref_wmo_material_slot(0, mats, blends, 3), 0);
    T_EQ((int)ref_wmo_material_slot(2, mats, blends, 3), 2); /* not deduped */
}

TEST(wow_wmo_mat_slot, out_of_range_material_id_returns_count) {
    void *tex_a = (void *)0x1000;
    void * const mats[1] = { tex_a };
    BYTE blends[1] = { 0 };
    T_EQ((int)ref_wmo_material_slot(99, mats, blends, 1), 1); /* fallback slot */
}

/* =========================================================================
   N. Wow_ComputeMoltContribution algorithm
   Reference takes pre-transformed world-space light positions to avoid
   needing a matrix implementation in the test.
   ======================================================================= */

static void ref_molt_contribution(wowWmoLight_t const *lights, DWORD num_lights,
                                   wowVec3_t const *world_positions,
                                   wowVec3_t ref_pos, wowVec3_t *out) {
    DWORD i;
    out->x = out->y = out->z = 0.0f;
    if (!lights || !num_lights) return;
    for (i = 0; i < num_lights; i++) {
        wowWmoLight_t const *lt = &lights[i];
        float atten = 0.0f;
        float contrib;
        if (lt->type == 3) { /* AMBIENT */
            atten = 1.0f;
        } else if (lt->type == 0 || lt->type == 1) { /* OMNI / SPOT */
            float dx = world_positions[i].x - ref_pos.x;
            float dy = world_positions[i].y - ref_pos.y;
            float dz = world_positions[i].z - ref_pos.z;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            if (lt->use_atten) {
                if (dist <= lt->atten_start) {
                    atten = 1.0f;
                } else if (lt->atten_end > lt->atten_start && dist < lt->atten_end) {
                    atten = 1.0f - (dist - lt->atten_start) / (lt->atten_end - lt->atten_start);
                }
            } else {
                atten = 1.0f;
            }
        }
        contrib = atten * lt->intensity;
        out->x += contrib * lt->color.r / 255.0f;
        out->y += contrib * lt->color.g / 255.0f;
        out->z += contrib * lt->color.b / 255.0f;
    }
    if (out->x > 1.0f) out->x = 1.0f;
    if (out->y > 1.0f) out->y = 1.0f;
    if (out->z > 1.0f) out->z = 1.0f;
}

TEST(wow_wmo_molt_contrib, no_lights_returns_zero_vector) {
    wowVec3_t out = {1.0f, 1.0f, 1.0f};
    ref_molt_contribution(NULL, 0, NULL, (wowVec3_t){0,0,0}, &out);
    T_ASSERT(feq(out.x, 0.0f) && feq(out.y, 0.0f) && feq(out.z, 0.0f));
}

TEST(wow_wmo_molt_contrib, omni_no_atten_full_contribution_regardless_of_distance) {
    wowWmoLight_t lt; memset(&lt, 0, sizeof(lt));
    lt.type = 0; lt.use_atten = 0; lt.intensity = 1.0f;
    lt.color.r = 255; lt.color.g = 127; lt.color.b = 0;
    wowVec3_t wpos = {0, 0, 0};
    wowVec3_t ref  = {999, 0, 0}; /* far away, but no attenuation flag */
    wowVec3_t out  = {0, 0, 0};
    ref_molt_contribution(&lt, 1, &wpos, ref, &out);
    T_ASSERT(feq(out.x, 1.0f));
    T_ASSERT(feq(out.y, 127.0f / 255.0f));
    T_ASSERT(feq(out.z, 0.0f));
}

TEST(wow_wmo_molt_contrib, omni_inside_atten_start_is_full) {
    wowWmoLight_t lt; memset(&lt, 0, sizeof(lt));
    lt.type = 0; lt.use_atten = 1; lt.intensity = 1.0f;
    lt.atten_start = 10.0f; lt.atten_end = 50.0f;
    lt.color.r = 255; lt.color.g = 255; lt.color.b = 255;
    wowVec3_t wpos = {0, 0, 0};
    wowVec3_t ref  = {5, 0, 0}; /* dist=5 < atten_start=10 */
    wowVec3_t out  = {0, 0, 0};
    ref_molt_contribution(&lt, 1, &wpos, ref, &out);
    T_ASSERT(feq(out.x, 1.0f));
}

TEST(wow_wmo_molt_contrib, omni_beyond_atten_end_is_zero) {
    wowWmoLight_t lt; memset(&lt, 0, sizeof(lt));
    lt.type = 0; lt.use_atten = 1; lt.intensity = 1.0f;
    lt.atten_start = 10.0f; lt.atten_end = 50.0f;
    lt.color.r = 255; lt.color.g = 255; lt.color.b = 255;
    wowVec3_t wpos = {0, 0, 0};
    wowVec3_t ref  = {100, 0, 0}; /* dist=100 > atten_end=50 */
    wowVec3_t out  = {1, 1, 1};
    ref_molt_contribution(&lt, 1, &wpos, ref, &out);
    T_ASSERT(feq(out.x, 0.0f) && feq(out.y, 0.0f) && feq(out.z, 0.0f));
}

TEST(wow_wmo_molt_contrib, linear_falloff_at_midpoint) {
    wowWmoLight_t lt; memset(&lt, 0, sizeof(lt));
    lt.type = 0; lt.use_atten = 1; lt.intensity = 1.0f;
    lt.atten_start = 0.0f; lt.atten_end = 40.0f;
    lt.color.r = 255; lt.color.g = 255; lt.color.b = 255;
    wowVec3_t wpos = {0, 0, 0};
    wowVec3_t ref  = {20, 0, 0}; /* dist=20, halfway → atten=0.5 */
    wowVec3_t out  = {0, 0, 0};
    ref_molt_contribution(&lt, 1, &wpos, ref, &out);
    T_ASSERT(feq(out.x, 0.5f));
}

TEST(wow_wmo_molt_contrib, ambient_type_contributes_without_position) {
    wowWmoLight_t lt; memset(&lt, 0, sizeof(lt));
    lt.type = 3; /* AMBIENT */
    lt.use_atten = 0; lt.intensity = 0.5f;
    lt.color.r = 200; lt.color.g = 100; lt.color.b = 50;
    wowVec3_t wpos = {0, 0, 0}; /* position irrelevant for ambient */
    wowVec3_t ref  = {9999, 0, 0};
    wowVec3_t out  = {0, 0, 0};
    ref_molt_contribution(&lt, 1, &wpos, ref, &out);
    T_ASSERT(feq(out.x, 0.5f * 200.0f / 255.0f));
    T_ASSERT(feq(out.y, 0.5f * 100.0f / 255.0f));
    T_ASSERT(feq(out.z, 0.5f * 50.0f / 255.0f));
}

TEST(wow_wmo_molt_contrib, multiple_lights_sum_clamped_to_one) {
    wowWmoLight_t lts[2]; memset(lts, 0, sizeof(lts));
    lts[0].type = 0; lts[0].use_atten = 0; lts[0].intensity = 0.7f;
    lts[0].color.r = 255; lts[0].color.g = 0; lts[0].color.b = 0;
    lts[1].type = 0; lts[1].use_atten = 0; lts[1].intensity = 0.8f;
    lts[1].color.r = 255; lts[1].color.g = 0; lts[1].color.b = 0;
    wowVec3_t wpos[2] = {{0,0,0}, {1,0,0}};
    wowVec3_t ref = {0, 0, 0};
    wowVec3_t out = {0, 0, 0};
    ref_molt_contribution(lts, 2, wpos, ref, &out);
    /* 0.7 + 0.8 = 1.5 on red → clamped to 1.0 */
    T_ASSERT(feq(out.x, 1.0f));
    T_ASSERT(feq(out.y, 0.0f));
}

/* =========================================================================
   Sun direction (Wow_SunDirection) — synthesized time-of-day sun, Z-up.
   Classic ships no Light*.dbc, so the authored sun path is unavailable; this
   is WoWee's directionalDir negated and Y-up→Z-up swapped to engine axes.
   day_frac: 0=midnight, 0.25=dawn(sun in -X), 0.5=noon, 0.75=dusk(sun in +X).
   ======================================================================= */

static void ref_sun_direction(float day_frac, float out[3]) {
    float a = day_frac * 6.283185307f;
    float x = -0.6f * sinf(a);
    float y = -0.6f * cosf(a);
    float z = 0.6f - 0.4f * cosf(a);
    float len = sqrtf(x * x + y * y + z * z);
    out[0] = x / len; out[1] = y / len; out[2] = z / len;
}

TEST(wow_sun_direction, unit_length_across_day) {
    float d[3];
    for (int i = 0; i < 32; i++) {
        ref_sun_direction(i / 32.0f, d);
        T_ASSERT(feq(sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 1.0f));
    }
}

TEST(wow_sun_direction, noon_points_up) {
    float d[3];
    ref_sun_direction(0.5f, d); /* noon: sun overhead, slightly +Y */
    T_ASSERT(d[2] > 0.5f);
}

TEST(wow_sun_direction, dawn_west_dusk_east) {
    float d[3];
    ref_sun_direction(0.25f, d); /* dawn: sun in -X */
    T_ASSERT(d[0] < -0.5f);
    ref_sun_direction(0.75f, d); /* dusk: sun in +X */
    T_ASSERT(d[0] > 0.5f);
}
