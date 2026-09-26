#include "r_local.h"

#include "conchars_sysfont.h"

texture_t *R_MakeSysFontTexture(void) {
    texture_t *texture = R_LoadTexturePCX((handle_t)conchars_sysfont_pcx, CONCHARS_SYSFONT_PCX_SIZE);

    assert(texture);

    R_Call(glBindTexture, GL_TEXTURE_2D, texture->texid);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return texture;
}
