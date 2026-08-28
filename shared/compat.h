#ifndef OPENREALM_COMPAT_H
#define OPENREALM_COMPAT_H

#ifdef OPENREALM_COMPAT_STRL

#include <stddef.h>
#include <string.h>

static inline size_t
openrealm_strlcpy(char *dst, const char *src, size_t dstsize)
{
    size_t srclen = strlen(src);

    if (dstsize != 0) {
        size_t copylen = srclen;

        if (copylen >= dstsize)
            copylen = dstsize - 1;

        memcpy(dst, src, copylen);
        dst[copylen] = '\0';
    }

    return srclen;
}

static inline size_t
openrealm_strlcat(char *dst, const char *src, size_t dstsize)
{
    size_t dstlen = strnlen(dst, dstsize);
    size_t srclen = strlen(src);

    if (dstlen == dstsize)
        return dstsize + srclen;

    size_t available = dstsize - dstlen - 1;
    size_t copylen = srclen;

    if (copylen > available)
        copylen = available;

    if (copylen != 0)
        memcpy(dst + dstlen, src, copylen);

    dst[dstlen + copylen] = '\0';

    return dstlen + srclen;
}

#define strlcpy openrealm_strlcpy
#define strlcat openrealm_strlcat

#endif /* OPENREALM_COMPAT_STRL */

#endif /* OPENREALM_COMPAT_H */