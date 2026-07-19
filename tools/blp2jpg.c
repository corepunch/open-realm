#include "tool_common.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif

#define BLP1_MAGIC 0x31504c42u
#define BLP2_MAGIC 0x32504c42u

typedef struct {
    uint8_t r, g, b, a;
} rgba8_t;

typedef struct {
    uint32_t magic;
    uint32_t type;
    uint32_t alpha_bits;
    uint32_t width;
    uint32_t height;
    uint32_t extra;
    uint32_t has_mipmaps;
    uint32_t offsets[16];
    uint32_t lengths[16];
} blp1_header_t;

typedef struct {
    uint32_t magic;
    uint32_t type;
    uint8_t encoding;
    uint8_t alpha_depth;
    uint8_t alpha_encoding;
    uint8_t has_mipmaps;
    uint32_t width;
    uint32_t height;
    uint32_t offsets[16];
    uint32_t lengths[16];
    uint32_t palette[256];
} blp2_header_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    rgba8_t *pixels;
} image_t;

static void usage(void) {
    fprintf(stderr,
            "Usage:\n"
            "  blp2jpg [-mpq <archive.mpq> ...] [-outdir <dir>] [-scale <n>] <file.blp> [...]\n"
            "\n"
            "Writes JPEGs beside local BLP files. For MPQ archive paths, mirrors the archive\n"
            "path under -outdir, or under the current directory when -outdir is omitted.\n"
            "\n"
            "Examples:\n"
            "  blp2jpg UI/Widgets/Glues/GlueScreen-Button1-Border.blp\n"
            "  blp2jpg -mpq War3.mpq -outdir build/blp-preview UI/Widgets/Glues/GlueScreen-Button1-Border.blp\n");
}

static uint32_t read_u32(uint8_t const *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static rgba8_t palette_bgra_to_rgba(uint8_t const *palette, uint8_t index) {
    uint8_t const *p = palette + index * 4;
    return (rgba8_t) { p[2], p[1], p[0], p[3] };
}

static uint8_t blp_alpha_at(uint8_t const *alpha, uint32_t i, uint32_t alpha_bits) {
    switch (alpha_bits) {
        case 0:
            return 255;
        case 1:
            return (alpha[i >> 3] & (1u << (i & 7))) ? 255 : 0;
        case 4: {
            uint8_t byte = alpha[i >> 1];
            uint8_t nibble = (i & 1) ? (byte >> 4) : (byte & 0x0f);
            return (uint8_t)((nibble << 4) | nibble);
        }
        case 8:
            return alpha[i];
        default:
            return 255;
    }
}

static rgba8_t dxt_color_565_to_rgba(uint16_t color, uint8_t alpha) {
    uint8_t r = (uint8_t)(((color >> 11) & 0x1f) * 255 / 31);
    uint8_t g = (uint8_t)(((color >> 5) & 0x3f) * 255 / 63);
    uint8_t b = (uint8_t)((color & 0x1f) * 255 / 31);
    return (rgba8_t) { r, g, b, alpha };
}

static void dxt_decode_color_block(uint8_t const *block, rgba8_t colors[4], bool four_color) {
    uint16_t c0 = (uint16_t)(block[0] | (block[1] << 8));
    uint16_t c1 = (uint16_t)(block[2] | (block[3] << 8));

    colors[0] = dxt_color_565_to_rgba(c0, 255);
    colors[1] = dxt_color_565_to_rgba(c1, 255);

    if (four_color || c0 > c1) {
        colors[2] = (rgba8_t){
            (uint8_t)((2 * colors[0].r + colors[1].r) / 3),
            (uint8_t)((2 * colors[0].g + colors[1].g) / 3),
            (uint8_t)((2 * colors[0].b + colors[1].b) / 3),
            255
        };
        colors[3] = (rgba8_t){
            (uint8_t)((colors[0].r + 2 * colors[1].r) / 3),
            (uint8_t)((colors[0].g + 2 * colors[1].g) / 3),
            (uint8_t)((colors[0].b + 2 * colors[1].b) / 3),
            255
        };
    } else {
        colors[2] = (rgba8_t){
            (uint8_t)((colors[0].r + colors[1].r) / 2),
            (uint8_t)((colors[0].g + colors[1].g) / 2),
            (uint8_t)((colors[0].b + colors[1].b) / 2),
            255
        };
        colors[3] = (rgba8_t){ 0, 0, 0, 0 };
    }
}

static void dxt_decode_alpha_dxt3(uint8_t const *block, uint8_t alpha[16]) {
    for (uint32_t i = 0; i < 16; i++) {
        uint8_t value = (uint8_t)((block[i / 2] >> ((i & 1) * 4)) & 0x0f);
        alpha[i] = (uint8_t)((value << 4) | value);
    }
}

static void dxt_decode_alpha_dxt5(uint8_t const *block, uint8_t alpha[16]) {
    uint8_t table[8];
    uint64_t bits = 0;

    table[0] = block[0];
    table[1] = block[1];
    if (table[0] > table[1]) {
        table[2] = (uint8_t)((6 * table[0] + 1 * table[1]) / 7);
        table[3] = (uint8_t)((5 * table[0] + 2 * table[1]) / 7);
        table[4] = (uint8_t)((4 * table[0] + 3 * table[1]) / 7);
        table[5] = (uint8_t)((3 * table[0] + 4 * table[1]) / 7);
        table[6] = (uint8_t)((2 * table[0] + 5 * table[1]) / 7);
        table[7] = (uint8_t)((1 * table[0] + 6 * table[1]) / 7);
    } else {
        table[2] = (uint8_t)((4 * table[0] + 1 * table[1]) / 5);
        table[3] = (uint8_t)((3 * table[0] + 2 * table[1]) / 5);
        table[4] = (uint8_t)((2 * table[0] + 3 * table[1]) / 5);
        table[5] = (uint8_t)((1 * table[0] + 4 * table[1]) / 5);
        table[6] = 0;
        table[7] = 255;
    }

    for (uint32_t i = 0; i < 6; i++) {
        bits |= ((uint64_t)block[2 + i]) << (8 * i);
    }
    for (uint32_t i = 0; i < 16; i++) {
        alpha[i] = table[(bits >> (3 * i)) & 7u];
    }
}

static bool decode_blp2_dxt(blp2_header_t const *header, uint8_t const *payload, image_t *out) {
    uint32_t blocks_x;
    uint32_t blocks_y;
    uint8_t const *src = payload;
    uint8_t const *src_end = payload + header->lengths[0];

    blocks_x = (header->width + 3) / 4;
    blocks_y = (header->height + 3) / 4;

    for (uint32_t by = 0; by < blocks_y; by++) {
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            uint8_t alpha[16];
            rgba8_t colors[4];
            uint8_t const *color_block;
            uint32_t indices;
            uint32_t block_size;

            memset(alpha, 255, sizeof(alpha));
            if (header->alpha_encoding == 1) {
                block_size = 16;
                if ((size_t)(src_end - src) < block_size) {
                    fprintf(stderr, "BLP2 DXT3 block truncated\n");
                    return false;
                }
                dxt_decode_alpha_dxt3(src, alpha);
                color_block = src + 8;
            } else if (header->alpha_encoding == 7) {
                block_size = 16;
                if ((size_t)(src_end - src) < block_size) {
                    fprintf(stderr, "BLP2 DXT5 block truncated\n");
                    return false;
                }
                dxt_decode_alpha_dxt5(src, alpha);
                color_block = src + 8;
            } else {
                block_size = 8;
                if ((size_t)(src_end - src) < block_size) {
                    fprintf(stderr, "BLP2 DXT1 block truncated\n");
                    return false;
                }
                color_block = src;
            }
            src += block_size;

            dxt_decode_color_block(color_block, colors, header->alpha_encoding != 0);
            indices = (uint32_t)(color_block[4] |
                                 (color_block[5] << 8) |
                                 (color_block[6] << 16) |
                                 (color_block[7] << 24));

            for (uint32_t py = 0; py < 4; py++) {
                for (uint32_t px = 0; px < 4; px++) {
                    uint32_t i = py * 4 + px;
                    uint32_t x = bx * 4 + px;
                    uint32_t y = by * 4 + py;
                    rgba8_t color;
                    if (x >= header->width || y >= header->height) {
                        continue;
                    }
                    color = colors[(indices >> (2 * i)) & 3u];
                    color.a = alpha[i];
                    out->pixels[y * header->width + x] = color;
                }
            }
        }
    }
    return true;
}

static bool decode_blp1_paletted(uint8_t const *data, size_t size, image_t *out) {
    blp1_header_t header;
    uint8_t const *palette;
    uint8_t const *indices;
    uint8_t const *alpha;
    uint32_t count;

    if (size < sizeof(header)) {
        fprintf(stderr, "BLP1 too small\n");
        return false;
    }
    memcpy(&header, data, sizeof(header));
    if (header.type != 1) {
        fprintf(stderr, "Unsupported BLP1 JPEG-compressed texture\n");
        return false;
    }
    if (header.width == 0 || header.height == 0 || header.width > 16384 || header.height > 16384) {
        fprintf(stderr, "Invalid BLP1 dimensions %ux%u\n", header.width, header.height);
        return false;
    }
    if (size < sizeof(header) + 1024) {
        fprintf(stderr, "BLP1 missing palette\n");
        return false;
    }
    if (header.offsets[0] >= size || header.lengths[0] > size - header.offsets[0]) {
        fprintf(stderr, "BLP1 mip 0 outside file\n");
        return false;
    }

    count = header.width * header.height;
    if (header.lengths[0] < count) {
        fprintf(stderr, "BLP1 mip 0 too small\n");
        return false;
    }
    palette = data + sizeof(header);
    indices = data + header.offsets[0];
    alpha = indices + count;

    out->width = header.width;
    out->height = header.height;
    out->pixels = Tool_XMalloc(sizeof(*out->pixels) * count);
    for (uint32_t i = 0; i < count; i++) {
        out->pixels[i] = palette_bgra_to_rgba(palette, indices[i]);
        out->pixels[i].a = blp_alpha_at(alpha, i, header.alpha_bits);
    }
    return true;
}

static bool decode_blp2(uint8_t const *data, size_t size, image_t *out) {
    blp2_header_t header;
    uint32_t count;
    uint8_t const *payload;

    if (size < sizeof(header)) {
        fprintf(stderr, "BLP2 too small\n");
        return false;
    }
    memcpy(&header, data, sizeof(header));
    if (header.width == 0 || header.height == 0 || header.width > 16384 || header.height > 16384) {
        fprintf(stderr, "Invalid BLP2 dimensions %ux%u\n", header.width, header.height);
        return false;
    }
    if (header.offsets[0] >= size || header.lengths[0] > size - header.offsets[0]) {
        fprintf(stderr, "BLP2 mip 0 outside file\n");
        return false;
    }

    count = header.width * header.height;
    payload = data + header.offsets[0];
    out->width = header.width;
    out->height = header.height;
    out->pixels = Tool_XMalloc(sizeof(*out->pixels) * count);

    if (header.encoding == 1) {
        uint8_t const *indices = payload;
        uint8_t const *alpha = payload + count;
        uint8_t const *palette = (uint8_t const *)header.palette;
        if (header.lengths[0] < count) {
            fprintf(stderr, "BLP2 paletted mip 0 too small\n");
            free(out->pixels);
            out->pixels = NULL;
            return false;
        }
        for (uint32_t i = 0; i < count; i++) {
            out->pixels[i] = palette_bgra_to_rgba(palette, indices[i]);
            out->pixels[i].a = blp_alpha_at(alpha, i, header.alpha_depth);
        }
        return true;
    }

    if (header.encoding == 3) {
        if (header.lengths[0] < count * 4u) {
            fprintf(stderr, "BLP2 BGRA mip 0 too small\n");
            free(out->pixels);
            out->pixels = NULL;
            return false;
        }
        for (uint32_t i = 0; i < count; i++) {
            uint8_t const *p = payload + i * 4;
            out->pixels[i] = (rgba8_t) { p[2], p[1], p[0], p[3] };
        }
        return true;
    }

    if (header.encoding == 2) {
        bool ok;
        ok = decode_blp2_dxt(&header, payload, out);
        if (!ok) {
            free(out->pixels);
            out->pixels = NULL;
        }
        return ok;
    }

    fprintf(stderr, "Unsupported BLP2 encoding %u\n", header.encoding);
    free(out->pixels);
    out->pixels = NULL;
    return false;
}

static bool decode_blp(uint8_t const *data, size_t size, image_t *out) {
    uint32_t magic;

    memset(out, 0, sizeof(*out));
    if (size < 4) {
        fprintf(stderr, "File too small\n");
        return false;
    }
    magic = read_u32(data);
    if (magic == BLP1_MAGIC) {
        return decode_blp1_paletted(data, size, out);
    }
    if (magic == BLP2_MAGIC) {
        return decode_blp2(data, size, out);
    }
    fprintf(stderr, "Not a BLP file\n");
    return false;
}

static bool mkdir_p(char const *path) {
    char *copy;
    char *p;

    if (!path || !*path) {
        return true;
    }
    copy = Tool_XStrdup(path);
    Tool_NormalizeSlashes(copy, '/');
    for (p = copy + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(copy, 0775) != 0 && errno != EEXIST) {
                fprintf(stderr, "mkdir %s: %s\n", copy, strerror(errno));
                free(copy);
                return false;
            }
            *p = '/';
        }
    }
    if (mkdir(copy, 0775) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir %s: %s\n", copy, strerror(errno));
        free(copy);
        return false;
    }
    free(copy);
    return true;
}

static char *output_path_for(char const *input, char const *outdir, bool from_mpq) {
    char *path;
    char *parent;
    char *dot;

    path = from_mpq || outdir ? Tool_PathJoin(outdir ? outdir : "", input) : Tool_XStrdup(input);
    Tool_NormalizeSlashes(path, '/');
    dot = strrchr(path, '.');
    {
        size_t base_len = dot ? (size_t)(dot - path) : strlen(path);
        path = Tool_XRealloc(path, base_len + 5);
        memcpy(path + base_len, ".jpg", 5);
    }

    parent = Tool_PathParent(path);
    if (!mkdir_p(parent)) {
        free(parent);
        free(path);
        return NULL;
    }
    free(parent);
    return path;
}

#ifdef __APPLE__
static rgba8_t checker_color(uint32_t x, uint32_t y) {
    uint8_t v = (((x / 8) + (y / 8)) & 1) ? 72 : 32;
    return (rgba8_t) { v, v, v, 255 };
}

static uint8_t blend_channel(uint8_t fg, uint8_t bg, uint8_t a) {
    return (uint8_t)(((uint32_t)fg * a + (uint32_t)bg * (255 - a) + 127) / 255);
}

static rgba8_t *composite_and_scale(image_t const *image, int scale, size_t *out_size) {
    uint32_t out_w = image->width * (uint32_t)scale;
    uint32_t out_h = image->height * (uint32_t)scale;
    rgba8_t *out = Tool_XMalloc(sizeof(*out) * out_w * out_h);

    for (uint32_t y = 0; y < out_h; y++) {
        for (uint32_t x = 0; x < out_w; x++) {
            rgba8_t src = image->pixels[(y / (uint32_t)scale) * image->width + (x / (uint32_t)scale)];
            rgba8_t bg = checker_color(x, y);
            rgba8_t dst = {
                blend_channel(src.r, bg.r, src.a),
                blend_channel(src.g, bg.g, src.a),
                blend_channel(src.b, bg.b, src.a),
                255,
            };
            out[y * out_w + x] = dst;
        }
    }
    *out_size = sizeof(*out) * out_w * out_h;
    return out;
}
#endif

static bool write_jpeg(char const *path, image_t const *image, int scale) {
#ifdef __APPLE__
    bool ok = false;
    size_t rgba_size = 0;
    uint32_t out_w = image->width * (uint32_t)scale;
    uint32_t out_h = image->height * (uint32_t)scale;
    rgba8_t *rgba = composite_and_scale(image, scale, &rgba_size);
    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, rgba, rgba_size, NULL);
    CGImageRef cg_image = NULL;
    CFURLRef url = NULL;
    CGImageDestinationRef dest = NULL;
    CFMutableDictionaryRef props = NULL;
    CFNumberRef quality = NULL;
    float quality_value = 0.95f;

    if (!color_space || !provider) {
        goto done;
    }
    cg_image = CGImageCreate(out_w,
                             out_h,
                             8,
                             32,
                             out_w * sizeof(rgba8_t),
                             color_space,
                             kCGBitmapByteOrder32Big | kCGImageAlphaNoneSkipLast,
                             provider,
                             NULL,
                             false,
                             kCGRenderingIntentDefault);
    if (!cg_image) {
        goto done;
    }
    url = CFURLCreateFromFileSystemRepresentation(NULL, (UInt8 const *)path, (CFIndex)strlen(path), false);
    if (!url) {
        goto done;
    }
    dest = CGImageDestinationCreateWithURL(url, CFSTR("public.jpeg"), 1, NULL);
    if (!dest) {
        goto done;
    }
    props = CFDictionaryCreateMutable(NULL, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    quality = CFNumberCreate(NULL, kCFNumberFloatType, &quality_value);
    if (props && quality) {
        CFDictionarySetValue(props, kCGImageDestinationLossyCompressionQuality, quality);
    }
    CGImageDestinationAddImage(dest, cg_image, props);
    ok = CGImageDestinationFinalize(dest);

done:
    if (quality) CFRelease(quality);
    if (props) CFRelease(props);
    if (dest) CFRelease(dest);
    if (url) CFRelease(url);
    if (cg_image) CGImageRelease(cg_image);
    if (provider) CGDataProviderRelease(provider);
    if (color_space) CGColorSpaceRelease(color_space);
    free(rgba);
    if (!ok) {
        fprintf(stderr, "Failed to write JPEG %s\n", path);
    }
    return ok;
#else
    (void)path;
    (void)image;
    (void)scale;
    fprintf(stderr, "JPEG output is currently implemented with macOS ImageIO only\n");
    return false;
#endif
}

static uint8_t *read_local_file(char const *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long len;

    if (!f) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s: failed to determine size\n", path);
        fclose(f);
        return NULL;
    }
    data = Tool_XMalloc((size_t)len);
    if (fread(data, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "%s: failed to read\n", path);
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)len;
    return data;
}

static uint8_t *read_mpq_file(HANDLE const *archives, size_t archive_count, char const *path, size_t *out_size) {
    HANDLE file = Tool_OpenFile(archives, archive_count, path);
    DWORD size;
    uint8_t *data;

    if (!file) {
        fprintf(stderr, "%s: not found in MPQ archives\n", path);
        return NULL;
    }
    size = SFileGetFileSize(file, NULL);
    data = Tool_XMalloc(size);
    if (!SFileReadFile(file, data, size, NULL, NULL)) {
        fprintf(stderr, "%s: failed to read from MPQ\n", path);
        free(data);
        Tool_CloseFile(file);
        return NULL;
    }
    Tool_CloseFile(file);
    *out_size = size;
    return data;
}

static bool convert_one(HANDLE const *archives,
                        size_t archive_count,
                        char const *path,
                        char const *outdir,
                        int scale,
                        bool from_mpq) {
    size_t size = 0;
    uint8_t *data = from_mpq ? read_mpq_file(archives, archive_count, path, &size) : read_local_file(path, &size);
    image_t image;
    char *out_path;
    bool ok;

    if (!data) {
        return false;
    }
    ok = decode_blp(data, size, &image);
    free(data);
    if (!ok) {
        return false;
    }

    out_path = output_path_for(path, outdir, from_mpq);
    if (!out_path) {
        free(image.pixels);
        return false;
    }
    ok = write_jpeg(out_path, &image, scale);
    if (ok) {
        printf("%s -> %s (%ux%u, scale %d)\n", path, out_path, image.width, image.height, scale);
    }
    free(out_path);
    free(image.pixels);
    return ok;
}

int main(int argc, char **argv) {
    HANDLE archives[64] = { 0 };
    size_t archive_count = sizeof(archives) / sizeof(*archives);
    char const *outdir = NULL;
    int scale = 1;
    int first_file = 1;
    bool have_mpq = false;
    bool ok = true;

    if (argc < 2) {
        usage();
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-mpq") || !strcmp(argv[i], "--mpq")) {
            if (++i >= argc) {
                usage();
                return 1;
            }
            if (!Tool_AddArchive(archives, archive_count, argv[i])) {
                return 1;
            }
            have_mpq = true;
            first_file = i + 1;
        } else if (!strcmp(argv[i], "-outdir") || !strcmp(argv[i], "--outdir")) {
            if (++i >= argc) {
                usage();
                return 1;
            }
            outdir = argv[i];
            first_file = i + 1;
        } else if (!strcmp(argv[i], "-scale") || !strcmp(argv[i], "--scale")) {
            if (++i >= argc) {
                usage();
                return 1;
            }
            scale = atoi(argv[i]);
            if (scale < 1 || scale > 32) {
                fprintf(stderr, "scale must be 1..32\n");
                return 1;
            }
            first_file = i + 1;
        } else if (argv[i][0] == '-') {
            usage();
            return 1;
        } else {
            first_file = i;
            break;
        }
    }

    if (first_file >= argc) {
        usage();
        Tool_CloseArchives(archives, archive_count);
        return 1;
    }

    for (int i = first_file; i < argc; i++) {
        ok = convert_one(archives, archive_count, argv[i], outdir, scale, have_mpq) && ok;
    }

    Tool_CloseArchives(archives, archive_count);
    return ok ? 0 : 1;
}
