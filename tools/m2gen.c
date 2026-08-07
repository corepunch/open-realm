/*
 * m2gen - deterministic M2 binary model fixture generator
 *
 * Generates minimal classic (MD20, version 263) M2 files with named animation
 * sequences for use as test fixtures. Output is byte-compatible with g_model.c
 * LoadModelM2 parser. No third-party dependencies.
 *
 * Usage:
 *   m2gen <out.m2> <anim_name>=<id>:<start>:<end> ...
 *
 * Example:
 *   m2gen orc_male.m2 Stand=0:0:1000 Death=1:1000:2600 Attack1H=17:2600:3600
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define M2_MAGIC_MD20   0x3032444Du  /* "MD20" */
#define M2_HEADER_SIZE  36
#define M2_SEQ_SIZE     68   /* sizeof(svM2SequenceClassic_t), 4-byte aligned, no trailing pad */

typedef struct {
	int32_t size;
	int32_t offset;
} svM2Array_t;

typedef struct {
	uint32_t magic;
	uint32_t version;
	svM2Array_t name;
	uint32_t flags;
	svM2Array_t global_loops;
	svM2Array_t sequences;
} svM2Header_t;

typedef struct {
	uint16_t animation_id;
	uint16_t sub_animation_id;
	uint32_t start_timestamp;
	uint32_t end_timestamp;
	float    movement_speed;
	uint32_t flags;
	int16_t  probability;
	uint16_t padding;
	uint32_t minimum_repetitions;
	uint32_t maximum_repetitions;
	uint32_t blend_time;
	float    min[3];
	float    max[3];
	float    radius;
	int16_t  next_animation;
	uint16_t alias_next;
} svM2SequenceClassic_t;

typedef struct {
	uint8_t *data;
	size_t size;
	size_t cap;
} wbuf_t;

static void wb_grow(wbuf_t *b, size_t need) {
	if (b->size + need <= b->cap) return;
	size_t nc = b->cap ? b->cap * 2 : 4096;
	while (nc < b->size + need) nc *= 2;
	b->data = realloc(b->data, nc);
	if (!b->data) { fprintf(stderr, "m2gen: out of memory\n"); exit(1); }
	b->cap = nc;
}

static void wb_write(wbuf_t *b, const void *src, size_t n) {
	wb_grow(b, n);
	memcpy(b->data + b->size, src, n);
	b->size += n;
}

static void wb_u32(wbuf_t *b, uint32_t v) { wb_write(b, &v, 4); }
static void wb_i32(wbuf_t *b, int32_t  v) { wb_write(b, &v, 4); }
static void wb_u16(wbuf_t *b, uint16_t v) { wb_write(b, &v, 2); }
static void wb_f32(wbuf_t *b, float    v) { wb_write(b, &v, 4); }
static void wb_free(wbuf_t *b) { free(b->data); b->data = NULL; b->size = b->cap = 0; }

static void write_seq(wbuf_t *b, uint16_t id, uint32_t start, uint32_t end) {
	wb_u16(b, id);       /* animation_id */
	wb_u16(b, 0);        /* sub_animation_id */
	wb_u32(b, start);    /* start_timestamp */
	wb_u32(b, end);      /* end_timestamp */
	wb_f32(b, 0.0f);     /* movement_speed */
	wb_u32(b, 0);        /* flags (0=looping) */
	wb_u16(b, 0);        /* probability */
	wb_u16(b, 0);        /* padding */
	wb_u32(b, 0);        /* minimum_repetitions */
	wb_u32(b, 0);        /* maximum_repetitions */
	wb_u32(b, 150);      /* blend_time */
	wb_f32(b, -0.5f); wb_f32(b, -0.5f); wb_f32(b, 0.0f);  /* min */
	wb_f32(b,  0.5f); wb_f32(b,  0.5f); wb_f32(b, 2.0f);  /* max */
	wb_f32(b, 1.0f);     /* radius */
	wb_u16(b, -1);       /* next_animation (none) */
	wb_u16(b, 0);        /* alias_next */
}

int main(int argc, char **argv) {
#define MAX_SEQS 24
	int i, n = 0;
	uint16_t ids[MAX_SEQS];
	uint32_t starts[MAX_SEQS], ends[MAX_SEQS];

	if (argc < 2) {
		fprintf(stderr, "usage: m2gen <out.m2> <AnimName>=<id>:<start>:<end> ...\n");
		return 1;
	}

	for (i = 2; i < argc && n < MAX_SEQS; i++) {
		char name[80];
		unsigned int id, start, end;
		if (sscanf(argv[i], "%79[^=]=%u:%u:%u", name, &id, &start, &end) != 4) {
			fprintf(stderr, "m2gen: bad seq spec '%s'\n", argv[i]);
			return 1;
		}
		ids[n] = (uint16_t)id;
		starts[n] = start;
		ends[n] = end;
		n++;
	}

	if (n == 0) {
		fprintf(stderr, "m2gen: at least one sequence required\n");
		return 1;
	}

	wbuf_t b = {0};

	/* svM2Header_t starts at offset 0 — the header.magic IS the file magic */
	wb_u32(&b, M2_MAGIC_MD20);  /* magic */
	wb_u32(&b, 263);             /* version (classic) */
	wb_i32(&b, 0); wb_i32(&b, -1);   /* name (empty) */
	wb_u32(&b, 0);               /* flags */
	wb_i32(&b, 0); wb_i32(&b, -1);   /* global_loops (empty) */
	wb_i32(&b, (int32_t)n);            /* sequences.size (element count) */
	wb_i32(&b, M2_HEADER_SIZE);         /* sequences.offset */

	/* Write sequences */
	for (i = 0; i < n; i++)
		write_seq(&b, ids[i], starts[i], ends[i]);

	FILE *f = fopen(argv[1], "wb");
	if (!f) {
		fprintf(stderr, "m2gen: cannot open '%s' for writing\n", argv[1]);
		wb_free(&b);
		return 1;
	}
	fwrite(b.data, 1, b.size, f);
	fclose(f);
	fprintf(stderr, "m2gen: wrote %s (%zu bytes)\n", argv[1], b.size);
	wb_free(&b);
	return 0;
}
