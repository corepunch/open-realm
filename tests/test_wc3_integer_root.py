"""Exact roots under host rounding modes, against both recovered algorithms."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <fenv.h>
#include <stdio.h>
#include "games/warcraft-3/common/wc3_pathing_adaptive.h"

static uint32_t restoring(uint32_t n) {
    uint32_t root=0,bit=1u<<30;
    while(bit>n)bit>>=2;
    while(bit) {
        if(n>=root+bit){n-=root+bit;root=(root>>1)+bit;}
        else root>>=1;
        bit>>=2;
    }
    return root;
}
static uint32_t newton(uint32_t n) {
    uint32_t root=n<256?n/12+1:n<65536?n/200+21:n/26743+444;
    int delta;
    do {delta=(int)(root-n/root);root=(uint32_t)((int)(n/root+root)/2);} while(delta/2);
    return root;
}
/* Explicit sign extension avoids relying on a host's right shift of negative integers. */
static inline int64_t original_align(int32_t n, unsigned shift) {
    uint32_t w = (uint32_t)n;
    if (shift) w = (w >> shift) | (n < 0 ? UINT32_MAX << (32 - shift) : 0);
    return w & 0x80000000u ? (int64_t)w - 0x100000000ll : w;
}

/* 6f06fbb0: align doubled signed significands, then truncate the normalized sum. */
static inline uint32_t original_add_bits(uint32_t a, uint32_t b) {
    int ea = (a >> 23) & 255, eb = (b >> 23) & 255, exp = ea > eb ? ea : eb;
    if (!ea || eb - ea >= 23) return b;
    if (!eb || ea - eb >= 23) return a;
    int32_t ma = ((a & 0x7fffff) | 0x800000) * 2, mb = ((b & 0x7fffff) | 0x800000) * 2;
    int64_t sum = original_align(a & 0x80000000u ? -ma : ma, exp - ea);
    sum += original_align(b & 0x80000000u ? -mb : mb, exp - eb);
    if (!sum) return 0;
    uint32_t mag = sum < 0 ? -sum : sum;
    int shift = 8 - __builtin_clz(mag);
    uint32_t mant = shift < 0 ? mag << -shift : mag >> shift;
    return ((uint32_t)(exp + shift - 1) << 23) | (mant & 0x7fffff) | (sum < 0 ? 0x80000000u : 0);
}

static int check_add(uint32_t a, uint32_t b) {
    uint32_t actual = wc3_add_bits(a, b), expected = original_add_bits(a, b);
    if (actual != expected) {
        fprintf(stderr, "add(%08x,%08x): %08x/%08x\n", a, b, actual, expected);
        return 1;
    }
    return 0;
}
static int check_additions(void) {
    uint32_t mantissas[] = {0, 1, 0x3fffff, 0x400000, 0x7ffffe, 0x7fffff};
    /* Every exponent pair and sign combination, covering cancellation,
     * exponent cutoffs, denormals and exceptional retail word behavior. */
    for (unsigned a = 0; a < 512; a++) for (unsigned b = 0; b < 512; b++)
        for (unsigned m = 0; m < 6; m++) for (unsigned n = 0; n < 6; n++)
            if (check_add((a << 23) | mantissas[m], (b << 23) | mantissas[n])) return 1;
    uint32_t random = 0x9e3779b9;
    for (unsigned i = 0; i < 1000000; i++) {
        random = random * 1664525u + 1013904223u;
        uint32_t a = random;
        random = random * 1664525u + 1013904223u;
        if (check_add(a, random)) return 1;
    }
    return 0;
}
static int check(uint32_t n) {
    uint32_t a=wc3_isqrt(n),b=restoring(n),c=newton(n);
    if(a!=b || a!=c || wc3_acc_sqrt(n)!=a) {
        fprintf(stderr,"root(%u): %u/%u/%u\n",n,a,b,c);return 1;
    }
    return 0;
}
int main(void) {
    if (check_additions()) return 1;
    int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    for(unsigned mode=0;mode<4;mode++) {
        if(fesetround(modes[mode]))return 2;
        /* All root transitions, including both sides and every small input. */
        for(uint32_t root=0;root<65536;root++) {
            uint32_t square=root*root;
            if(check(square)||check(square+1)||check(square-1)||check(root))return 1;
        }
        uint32_t n=0x9e3779b9;
        for(unsigned i=0;i<250000;i++) {
            n=n*1664525u+1013904223u;
            if(check(n))return 1;
        }
    }
    return fesetround(FE_TONEAREST);
}
'''


class IntegerRootTests(unittest.TestCase):
    def test_original_roots_and_rounding_modes(self):
        with tempfile.TemporaryDirectory(prefix="wc3-integer-root-") as directory:
            source = Path(directory) / "root.c"
            source.write_text(SOURCE)
            for optimization in ("-O0", "-O2"):
                with self.subTest(optimization=optimization):
                    binary = Path(directory) / optimization
                    subprocess.run(["cc", "-std=c11", optimization, "-I", str(ROOT),
                                    str(source), "-lm", "-o", str(binary)], check=True)
                    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
