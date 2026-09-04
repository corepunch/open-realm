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
    { NULL, 0 },
};

static struct {
    LPCSTR name;
    DWORD bit;
} const key_mod_names[] = {
    { "SHIFT", KEY_MOD_SHIFT },
    { "CTRL", KEY_MOD_CTRL },
    { "CONTROL", KEY_MOD_CTRL },
    { "ALT", KEY_MOD_ALT },
    { NULL, 0 },
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

/* Parse "CTRL+SHIFT+1" / "alt+mouse1". Modifier order does not matter; the last
 * non-modifier token is the key. Returns false when the name is empty or unknown. */
static BOOL Key_ParseName(LPCSTR str, keyCode_t *key, DWORD *mods) {
    char buf[64];
    char *p, *next;
    DWORD m = 0;
    LPCSTR keytok = NULL;

    if (!str || !*str || !key || !mods) return false;
    snprintf(buf, sizeof(buf), "%s", str);
    for (p = buf; p; p = next) {
        BOOL ismod = false;
        next = strchr(p, '+');
        if (next) *next++ = 0;
        if (!*p) return false;
        for (DWORD i = 0; key_mod_names[i].name; i++) {
            if (!strcasecmp(p, key_mod_names[i].name)) {
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

/* Canonical bind name: CTRL+ALT+SHIFT+<key>, matching existing ALT+MOUSE1 configs. */
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

/* Choose the bind-table slot for a held modifier set. Exact combos win; then
 * weaker modifiers are dropped in Alt, Shift, Ctrl order so Ctrl beats Shift
 * when both are held and only single-modifier binds exist. Slot 0 is the
 * unmodified fallback. KEY_MOD_COUNT means no matching bind. */
static DWORD Key_SelectSlot(DWORD mods, DWORD occupied) {
    DWORD slot = mods & KEY_MOD_MASK;

    while (slot) {
        if (occupied & (1u << slot)) return slot;
        if (slot & KEY_MOD_ALT) slot &= ~KEY_MOD_ALT;
        else if (slot & KEY_MOD_SHIFT) slot &= ~KEY_MOD_SHIFT;
        else slot &= ~KEY_MOD_CTRL;
    }
    return (occupied & 1u) ? 0 : KEY_MOD_COUNT;
}

#endif
