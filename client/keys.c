#include "client.h"
#include "keys_name.h"

static UINAME keybindings[KEY_MOD_COUNT][MAX_KEYS];
static BYTE key_down_mods[MAX_KEYS];
static BYTE key_down_set[MAX_KEYS];

void Key_SetBinding(keyCode_t key, DWORD mods, LPCSTR binding) {
    mods &= KEY_MOD_MASK;
    if (binding)
        snprintf(keybindings[mods][key], sizeof(keybindings[mods][key]), "%s", binding);
    else
        memset(keybindings[mods][key], 0, sizeof(keybindings[mods][key]));
}

LPCSTR Key_GetBinding(keyCode_t key, DWORD mods) {
    return keybindings[mods & KEY_MOD_MASK][key];
}

static LPCSTR Key_FindBinding(keyCode_t key, DWORD mods) {
    LPCSTR kb = keybindings[mods & KEY_MOD_MASK][key];
    return kb[0] ? kb : NULL;
}

static void Key_Bind_f(void) {
    keyCode_t keynum;
    DWORD mods;

    if (Cmd_Argc() < 2) {
        fprintf(stderr, "bind <key> [command] : attach a command to a key\n");
        return;
    }
    if (!Key_ParseName(Cmd_Argv(1), &keynum, &mods)) {
        fprintf(stderr, "\"%s\" isn't a valid key\n", Cmd_Argv(1));
        return;
    }
    if (Cmd_Argc() == 2) {
        fprintf(stderr, "\"%s\" = \"%s\"\n", Cmd_Argv(1), Key_GetBinding(keynum, mods));
        return;
    }
    Key_SetBinding(keynum, mods, Cmd_ArgsFrom(2));
}

static void Key_Unbind_f(void) {
    keyCode_t keynum;
    DWORD mods;

    if (Cmd_Argc() != 2) {
        fprintf(stderr, "unbind <key> : remove commands from a key\n");
        return;
    }
    if (!Key_ParseName(Cmd_Argv(1), &keynum, &mods)) {
        fprintf(stderr, "\"%s\" isn't a valid key\n", Cmd_Argv(1));
        return;
    }
    Key_SetBinding(keynum, mods, NULL);
}

static void Key_Unbindall_f(void) {
    FOR_LOOP(m, KEY_MOD_COUNT) {
        FOR_LOOP(i, MAX_KEYS)
            Key_SetBinding((keyCode_t)i, m, NULL);
    }
}

static void Key_WriteBindName(FILE *file, keyCode_t key, DWORD mods, LPCSTR binding) {
    char name[64];

    Key_FormatName(key, mods, name, sizeof(name));
    if (file) {
        fprintf(file, "bind %s \"", name);
        for (LPCSTR p = binding; *p; p++) {
            if (*p == '"') fputc('\\', file);
            fputc(*p, file);
        }
        fprintf(file, "\"\n");
        return;
    }
    fprintf(stderr, "%s \"%s\"\n", name, binding);
}

static void Key_Bindlist_f(void) {
    FOR_LOOP(i, MAX_KEYS) {
        FOR_LOOP(m, KEY_MOD_COUNT) {
            if (keybindings[m][i][0])
                Key_WriteBindName(NULL, (keyCode_t)i, m, keybindings[m][i]);
        }
    }
}

void Key_WriteBindings(FILE *file) {
    if (!file) return;
    FOR_LOOP(i, MAX_KEYS) {
        FOR_LOOP(m, KEY_MOD_COUNT) {
            if (keybindings[m][i][0])
                Key_WriteBindName(file, (keyCode_t)i, m, keybindings[m][i]);
        }
    }
}

void Key_Init(void) {
    Cmd_AddCommand("bind", Key_Bind_f);
    Cmd_AddCommand("unbind", Key_Unbind_f);
    Cmd_AddCommand("unbindall", Key_Unbindall_f);
    Cmd_AddCommand("bindlist", Key_Bindlist_f);
}

void Key_Event(keyCode_t key, DWORD mods, bool down, DWORD time) {
    LPCSTR kb;
    char cmd[1024];

    /* Forward to UI library if in menu mode */
    if (cls.key_dest == key_menu && menu.KeyEvent) {
        menu.KeyEvent(key, down, time);
        return;
    }

    /* Key-up still reaches +command releases so focusing a window cannot leave an earlier gameplay command stuck. */
    if (cls.key_dest == key_game && down && CL_WindowKeyEvent(key)) return;

    if (down) {
        key_down_mods[key] = (BYTE)(mods & KEY_MOD_MASK);
        key_down_set[key] = 1;
    } else if (key_down_set[key]) {
        mods = key_down_mods[key];
        key_down_set[key] = 0;
    }

    kb = Key_FindBinding(key, mods);
    if (!kb || !*kb) {
        if (down) SCR_LayoutKeyEvent(key);
        return;
    }

    if (key == K_ESCAPE && down && SCR_LayoutKeyEvent(key)) {
        return;
    }

    if (!down) {
        if (*kb == '+') {
            snprintf(cmd, sizeof(cmd), "-%s %i %i\n", kb+1, key, time);
            Cbuf_AddText(cmd);
        }
        return;
    }

    if (kb[0] == '+') {    // button commands add keynum and time as a parm
        snprintf(cmd, sizeof(cmd), "%s %i %i\n", kb, key, time);
        Cbuf_AddText(cmd);
    } else {
        Cbuf_AddText(kb);
        Cbuf_AddText("\n");
    }
}
