#include "client.h"

#include <strings.h>

static UINAME keybindings[MAX_KEYS];

typedef struct {
    LPCSTR name;
    DWORD keynum;
} keyname_t;

keyname_t keynames[] = {
    { "TAB", K_TAB },
    { "ENTER", K_ENTER },
    { "ESCAPE", K_ESCAPE },
    { "ESC", K_ESCAPE },
    { "SPACE", K_SPACE },
    { "MOUSE1", K_MOUSE1 },
    { "MOUSE2", K_MOUSE2 },
    { "MOUSE3", K_MOUSE3 },
    { "ALT+MOUSE1", K_ALT_MOUSE1 },
    { "ALT+MOUSE2", K_ALT_MOUSE2 },
    { "ALT+MOUSE3", K_ALT_MOUSE3 },
    { NULL, 0 },
};

void Key_SetBinding(keyCode_t key, LPCSTR binding) {
    if (binding) {
        snprintf(keybindings[key], sizeof(keybindings[key]), "%s", binding);
    } else {
        memset(keybindings[key], 0, sizeof(*keybindings));
    }
}

LPCSTR Key_GetBinding(keyCode_t key) {
    return keybindings[key];
}

static keyCode_t Key_StringToKeynum(LPCSTR str) {
    if (!str || !*str) {
        return 0;
    }
    if (!str[1]) {
        return (keyCode_t)str[0];
    }
    for (keyname_t const *key = keynames; key->name; key++) {
        if (!strcasecmp(str, key->name)) {
            return (keyCode_t)key->keynum;
        }
    }
    return 0;
}

static LPCSTR Key_KeynumToString(keyCode_t keynum) {
    static char tinystr[2];

    for (keyname_t const *key = keynames; key->name; key++) {
        if (key->keynum == keynum) {
            return key->name;
        }
    }
    if (keynum > 32 && keynum < 127) {
        tinystr[0] = (char)keynum;
        tinystr[1] = '\0';
        return tinystr;
    }
    return "<UNKNOWN>";
}

static void Key_Bind_f(void) {
    keyCode_t keynum;

    if (Cmd_Argc() < 2) {
        fprintf(stderr, "bind <key> [command] : attach a command to a key\n");
        return;
    }
    keynum = Key_StringToKeynum(Cmd_Argv(1));
    if (!keynum) {
        fprintf(stderr, "\"%s\" isn't a valid key\n", Cmd_Argv(1));
        return;
    }
    if (Cmd_Argc() == 2) {
        fprintf(stderr, "\"%s\" = \"%s\"\n", Cmd_Argv(1), Key_GetBinding(keynum));
        return;
    }
    Key_SetBinding(keynum, Cmd_ArgsFrom(2));
}

static void Key_Unbind_f(void) {
    keyCode_t keynum;

    if (Cmd_Argc() != 2) {
        fprintf(stderr, "unbind <key> : remove commands from a key\n");
        return;
    }
    keynum = Key_StringToKeynum(Cmd_Argv(1));
    if (!keynum) {
        fprintf(stderr, "\"%s\" isn't a valid key\n", Cmd_Argv(1));
        return;
    }
    Key_SetBinding(keynum, NULL);
}

static void Key_Unbindall_f(void) {
    FOR_LOOP(i, MAX_KEYS) {
        Key_SetBinding((keyCode_t)i, NULL);
    }
}

static void Key_Bindlist_f(void) {
    FOR_LOOP(i, MAX_KEYS) {
        if (keybindings[i][0]) {
            fprintf(stderr, "%s \"%s\"\n", Key_KeynumToString((keyCode_t)i), keybindings[i]);
        }
    }
}

void Key_WriteBindings(FILE *file) {
    if (!file) {
        return;
    }
    FOR_LOOP(i, MAX_KEYS) {
        if (keybindings[i][0]) {
            fprintf(file, "bind %s \"", Key_KeynumToString((keyCode_t)i));
            for (LPCSTR p = keybindings[i]; *p; p++) {
                if (*p == '"') {
                    fputc('\\', file);
                }
                fputc(*p, file);
            }
            fprintf(file, "\"\n");
        }
    }
}

void Key_Init(void) {
    Cmd_AddCommand("bind", Key_Bind_f);
    Cmd_AddCommand("unbind", Key_Unbind_f);
    Cmd_AddCommand("unbindall", Key_Unbindall_f);
    Cmd_AddCommand("bindlist", Key_Bindlist_f);
}

void Key_Event(keyCode_t key, bool down, DWORD time) {
    /* Forward to UI library if in menu mode */
    if (cls.key_dest == key_menu && ui.KeyEvent) {
        ui.KeyEvent(key, down, time);
        return;
    }
    
    LPCSTR kb = keybindings[key];
    char cmd[1024];

    if (!kb || !*kb) {
        if (down) SCR_LayoutKeyEvent(key);
        return;
    }
    
    if (!down) {
        if (*kb == '+') {
            snprintf(cmd, sizeof(cmd), "-%s %i %i\n", kb+1, key, time);
            Cbuf_AddText(cmd);
        }
        return;
    }

    if (kb) {
        if (kb[0] == '+') {    // button commands add keynum and time as a parm
            snprintf(cmd, sizeof(cmd), "%s %i %i\n", kb, key, time);
            Cbuf_AddText(cmd);
        } else {
            Cbuf_AddText(kb);
            Cbuf_AddText("\n");
        }
    }
    return;
}
