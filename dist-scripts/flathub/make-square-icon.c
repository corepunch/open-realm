#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "renderer/stb/stb_image.h"
#include "renderer/stb/stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int width, height, channels;
    const int out_size = 512;
    unsigned char *source;
    unsigned char *output;
    int copy_width, copy_height, src_x, src_y, dst_x, dst_y;

    if (argc != 3) {
        fprintf(stderr, "usage: %s input.png output.png\n", argv[0]);
        return 2;
    }

    source = stbi_load(argv[1], &width, &height, &channels, 4);
    if (!source) {
        fprintf(stderr, "could not load %s: %s\n", argv[1], stbi_failure_reason());
        return 1;
    }

    output = calloc((size_t)out_size * out_size, 4);
    if (!output) {
        stbi_image_free(source);
        return 1;
    }

    copy_width = width < out_size ? width : out_size;
    copy_height = height < out_size ? height : out_size;
    src_x = (width - copy_width) / 2;
    src_y = (height - copy_height) / 2;
    dst_x = (out_size - copy_width) / 2;
    dst_y = (out_size - copy_height) / 2;

    for (int y = 0; y < copy_height; y++) {
        memcpy(output + ((size_t)(dst_y + y) * out_size + dst_x) * 4,
               source + ((size_t)(src_y + y) * width + src_x) * 4,
               (size_t)copy_width * 4);
    }

    if (!stbi_write_png(argv[2], out_size, out_size, 4, output, out_size * 4)) {
        fprintf(stderr, "could not write %s\n", argv[2]);
        free(output);
        stbi_image_free(source);
        return 1;
    }

    free(output);
    stbi_image_free(source);
    return 0;
}
