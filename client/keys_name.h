#ifndef keys_name_h
#define keys_name_h

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "common/shared.h"
#include "keys.h"

typedef struct {
    LPCSTR name;
    DWORD keynum;
} keyname_t;

static keyname_t const key_names[] = {
    { "TAB", K_TAB },
    { "ENTER", K_ENTER },
    { "ESCAPE", K_ESCAPE },
    { "ESC", K_ESCAPE },
    { "SPACE", K_SPACE },
    { "F1",  K_F1  }, { "F2",  K_F2  }, { "F3",  K_F3  }, { "F4",  K_F4  },
    { "F5",  K_F5  }, { "F6",  K_F6  }, { "F7",  K_F7  }, { "F8",  K_F8  },
    { "F9",  K_F9  }, { "F10", K_F10 }, { "F11", K_F11 }, { "F12", K_F12 },
    { "UPARROW", K_UPARROW }, { "DOWNARROW", K_DOWNARROW },
    { "LEFTARROW", K_LEFTARROW }, { "RIGHTARROW", K_RIGHTARROW },
    { "UP", K_UPARROW }, { "DOWN", K_DOWNARROW },
    { "LEFT", K_LEFTARROW }, { "RIGHT", K_RIGHTARROW },
    { "MOUSE1", K_MOUSE1 },
    { "MOUSE2", K_MOUSE2 },
    { "MOUSE3", K_MOUSE3 },
    { "MWHEELUP", K_MWHEELUP },
    { "MWHEELDOWN", K_MWHEELDOWN },
    { NULL, 0 },
};

/* Stroke order is ctrl, alt, shift — same as lite's keymap.modkeys. */
static struct {
    LPCSTR name;
    DWORD bit;
    int rank;
} const key_mod_names[] = {
    { "CTRL", KEY_MOD_CTRL, 0 },
    { "CONTROL", KEY_MOD_CTRL, 0 },
    { "ALT", KEY_MOD_ALT, 1 },
    { "SHIFT", KEY_MOD_SHIFT, 2 },
    { NULL, 0, 0 },
};

/* Map one bind token (F1, MOUSE1, a) to a key code. Letters fold to lowercase
 * because SDL keydown events report SDLK_a even when Shift is held. */
static keyCode_t Key_TokenToKeynum(LPCSTR tok) {
    unsigned char ch;

    if (!tok || !*tok) return 0;
    if (!tok[1]) {
        ch = (unsigned char)tok[0];
        return (keyCode_t)(isalpha(ch) ? tolower(ch) : ch);
    }
    for (keyname_t const *key = key_names; key->name; key++) {
        if (!strcasecmp(tok, key->name))
            return (keyCode_t)key->keynum;
    }
    return 0;
}

/* Parse "CTRL+SHIFT+1" / "ALT+MOUSE1". Modifiers must appear in ctrl, alt, shift
 * order (lite keymap). The last non-modifier token is the key. */
static BOOL Key_ParseName(LPCSTR str, keyCode_t *key, DWORD *mods) {
    char buf[64];
    char *p, *next;
    DWORD m = 0;
    LPCSTR keytok = NULL;
    int last_rank = -1;

    if (!str || !*str || !key || !mods) return false;
    snprintf(buf, sizeof(buf), "%s", str);
    for (p = buf; p; p = next) {
        BOOL ismod = false;
        next = strchr(p, '+');
        if (next) *next++ = 0;
        if (!*p) return false;
        for (DWORD i = 0; key_mod_names[i].name; i++) {
            if (!strcasecmp(p, key_mod_names[i].name)) {
                if (key_mod_names[i].rank <= last_rank) return false;
                last_rank = key_mod_names[i].rank;
                m |= key_mod_names[i].bit;
                ismod = true;
                break;
            }
        }
        if (ismod) continue;
        if (keytok) return false;
        keytok = p;
    }
    if (!keytok) return false;
    *key = Key_TokenToKeynum(keytok);
    *mods = m;
    return *key != 0;
}

/* Canonical stroke: ctrl, alt, shift, then the key. */
static void Key_FormatName(keyCode_t key, DWORD mods, LPSTR dst, DWORD dst_size) {
    char tiny[2] = { 0 };
    LPCSTR name = NULL;

    for (keyname_t const *kn = key_names; kn->name; kn++) {
        if (kn->keynum == key) {
            name = kn->name;
            break;
        }
    }
    if (!name && key > 32 && key < 127) {
        tiny[0] = (char)key;
        name = tiny;
    }
    if (!name) name = "<UNKNOWN>";
    snprintf(dst, dst_size, "%s%s%s%s",
             (mods & KEY_MOD_CTRL) ? "CTRL+" : "",
             (mods & KEY_MOD_ALT) ? "ALT+" : "",
             (mods & KEY_MOD_SHIFT) ? "SHIFT+" : "",
             name);
}

#ifdef BZ_TESTS
/* Exact stroke only: held mods in ctrl/alt/shift order. Shift+1 is not 1. */
static DWORD Key_SelectSlot(DWORD mods, DWORD occupied) {
    DWORD slot = mods & KEY_MOD_MASK;
    return (occupied & (1u << slot)) ? slot : KEY_MOD_COUNT;
}
#endif

#endif
