#include "g_local.h"

#pragma pack (push, 1)
typedef struct {
    uint8_t id_length, colormap_type, image_type;
    uint16_t colormap_index, colormap_length;
    uint8_t colormap_size;
    uint16_t x_origin, y_origin, width, height;
    uint8_t pixel_size, attributes;
} tgaHeader_t;
#pragma pack (pop)

#define TGA_ORIGIN_MASK 0x30

pathTex_t *LoadTGA(uint8_t const* mem, size_t size) {
    tgaHeader_t const *header;
    uint8_t const *tga;
    size_t offset, bytes_per_pixel, num_pixels;
    uint32_t columns, rows;

    if (!mem || size < sizeof(tgaHeader_t)) return NULL;
    header = (tgaHeader_t const *)mem;
    offset = sizeof(tgaHeader_t) + header->id_length;
    if (offset > size) return NULL;
    tga = mem + offset;
    columns = header->width;
    rows = header->height;
    if (!columns || !rows) return NULL;
    switch (header->image_type) {
        case 2:  break;
        case 3:  break;
//        case 10: break;
        default: return NULL;
    }
    switch (header->colormap_type) {
        case 0:  break;
        default: return NULL;
    }
    switch (header->pixel_size) {
        case 32: break;
        case 24: break;
        case 8:  break;
        default: return NULL;
    }
    bytes_per_pixel = header->pixel_size / 8;
    num_pixels = (size_t)columns * rows;
    /* Pathing TGAs are archive data, but rejecting truncated payloads here keeps
     * a bad bridge resource from reading beyond its VFS buffer. Valid WC3 BGRA
     * bytes retain their existing channel and row interpretation. */
    if (num_pixels > (SIZE_MAX - sizeof(pathTex_t)) / sizeof(COLOR32) ||
        num_pixels > (size - offset) / bytes_per_pixel) return NULL;
    // Allocate memory for decoded image
    pathTex_t *pathTex = gi.MemAlloc(num_pixels * sizeof(COLOR32) + sizeof(pathTex_t));
    if (!pathTex)
        return NULL;
    pathTex->width = columns;
    pathTex->height = rows;
    // Uncompressed RGB image
    if (header->image_type==2 || header->image_type==3) {
        for (int row=rows-1; row>=0; row--) {
            for (int column=0; column<columns; column++) {
                LPCCOLOR32 pcolor = &pathTex->map[column + row * columns];
                uint8_t * dest = (uint8_t *)pcolor;
                uint8_t value;
                switch (header->pixel_size) {
                    case 8:
                        value = *tga++;
                        *dest++ = value;
                        *dest++ = value;
                        *dest++ = value;
                        *dest++ = 0xff;
                        break;
                    case 24:
                        *dest++ = *(tga++);
                        *dest++ = *(tga++);
                        *dest++ = *(tga++);
                        *dest++ = 0xff;
                        break;
                    case 32:
                        *dest++ = *(tga++);
                        *dest++ = *(tga++);
                        *dest++ = *(tga++);
                        *dest++ = *(tga++);
                        break;
                }
            }
        }
        return pathTex;
    }
    gi.MemFree(pathTex);
    return NULL;
}
