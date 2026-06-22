#include "common.h"

#include <ctype.h>
#include <stdlib.h>

extern void Key_WriteBindings(FILE *file);

#define CVAR_COMPLETE_CHARS 1024

static cvar_t *cvar_vars;

static BOOL Cvar_NameMatches(LPCSTR name, LPCSTR partial) {
    size_t len;

    if (!name || !partial) {
        return false;
    }
    len = strlen(partial);
    return !strncasecmp(name, partial, len);
}

static void Cvar_CommonPrefix(LPSTR out, DWORD out_size, LPCSTR name) {
    DWORD i;

    if (!out || out_size == 0 || !name) {
        return;
    }
    if (!out[0]) {
        snprintf(out, out_size, "%s", name);
        return;
    }
    for (i = 0; out[i] && name[i] && i + 1 < out_size; i++) {
        if (tolower((unsigned char)out[i]) != tolower((unsigned char)name[i])) {
            break;
        }
    }
    out[i] = '\0';
}

static LPSTR Cvar_CopyString(LPCSTR in) {
    size_t len = in ? strlen(in) : 0;
    LPSTR out = MemAlloc((long)len + 1);

    if (len) {
        memcpy(out, in, len);
    }
    return out;
}

static bool Cvar_NameIsValid(LPCSTR name) {
    if (!name || !*name) {
        return false;
    }
    for (LPCSTR p = name; *p; p++) {
        if (isspace((unsigned char)*p) || *p == '"' || *p == ';') {
            return false;
        }
    }
    return true;
}

static void Cvar_UpdateValue(cvar_t *var) {
    if (!var || !var->string) {
        return;
    }
    var->value = strtof(var->string, NULL);
    var->integer = (int)var->value;
}

static cvar_t *Cvar_FindVar(LPCSTR name) {
    if (!name) {
        return NULL;
    }
    FOR_EACH_LIST(cvar_t, var, cvar_vars) {
        if (!strcmp(var->name, name)) {
            return var;
        }
    }
    return NULL;
}

cvar_t *Cvar_Get(LPCSTR name, LPCSTR value, DWORD flags) {
    cvar_t *var;

    if (!Cvar_NameIsValid(name)) {
        fprintf(stderr, "Cvar_Get: invalid cvar name \"%s\"\n", name ? name : "");
        return NULL;
    }
    var = Cvar_FindVar(name);
    if (var) {
        var->flags |= flags;
        return var;
    }
    var = MemAlloc(sizeof(*var));
    var->name = Cvar_CopyString(name);
    var->string = Cvar_CopyString(value ? value : "");
    var->flags = flags;
    var->modified = true;
    Cvar_UpdateValue(var);
    ADD_TO_LIST(var, cvar_vars);
    return var;
}

cvar_t *Cvar_Set(LPCSTR name, LPCSTR value) {
    cvar_t *var = Cvar_Get(name, value ? value : "", 0);

    if (!var) {
        return NULL;
    }
    value = value ? value : "";
    if (!strcmp(var->string, value)) {
        return var;
    }
    MemFree(var->string);
    var->string = Cvar_CopyString(value);
    var->modified = true;
    Cvar_UpdateValue(var);
    return var;
}

cvar_t *Cvar_SetValue(LPCSTR name, FLOAT value) {
    char text[64];

    snprintf(text, sizeof(text), "%g", value);
    return Cvar_Set(name, text);
}

LPCSTR Cvar_String(LPCSTR name, LPCSTR fallback) {
    cvar_t *var = Cvar_FindVar(name);

    return var ? var->string : fallback;
}

int Cvar_Integer(LPCSTR name, int fallback) {
    cvar_t *var = Cvar_FindVar(name);

    return var ? var->integer : fallback;
}

FLOAT Cvar_Value(LPCSTR name, FLOAT fallback) {
    cvar_t *var = Cvar_FindVar(name);

    return var ? var->value : fallback;
}

void Cvar_ForEachVariable(cmdListFunc_t func, void *userData) {
    if (!func) {
        return;
    }
    FOR_EACH_LIST(cvar_t, var, cvar_vars) {
        func(var->name, userData);
    }
}

int Cvar_CompleteVariable(LPCSTR partial, LPSTR out, DWORD out_size, bool print) {
    int matches = 0;
    char common[CVAR_COMPLETE_CHARS];

    if (out && out_size > 0) {
        out[0] = '\0';
    }
    common[0] = '\0';
    partial = partial ? partial : "";
    FOR_EACH_LIST(cvar_t, var, cvar_vars) {
        if (!Cvar_NameMatches(var->name, partial)) {
            continue;
        }
        if (print) {
            fprintf(stderr, "%s\n", var->name);
        }
        Cvar_CommonPrefix(common, sizeof(common), var->name);
        matches++;
    }
    if (out && out_size > 0 && matches > 0) {
        snprintf(out, out_size, "%s", common);
    }
    return matches;
}

static void Cvar_Set_f(void) {
    int argc = Cmd_Argc();
    LPCSTR name = Cmd_Argv(1);

    if (argc < 2) {
        fprintf(stderr, "usage: set <name> <value>\n");
        return;
    }
    if (argc == 2) {
        cvar_t *var = Cvar_FindVar(name);
        if (var) {
            fprintf(stderr, "\"%s\" is \"%s\"\n", var->name, var->string);
        } else {
            fprintf(stderr, "\"%s\" is undefined\n", name);
        }
        return;
    }
    Cvar_Set(name, Cmd_ArgsFrom(2));
}

static void Cvar_SetA_f(void) {
    cvar_t *var;
    int argc = Cmd_Argc();
    LPCSTR name = Cmd_Argv(1);

    if (argc < 3) {
        Cvar_Set_f();
        return;
    }
    var = Cvar_Set(name, Cmd_ArgsFrom(2));
    if (var) {
        var->flags |= CVAR_ARCHIVE;
    }
}

static void Cvar_List_f(void) {
    DWORD count = 0;

    FOR_EACH_LIST(cvar_t, var, cvar_vars) {
        fprintf(stderr, "%c %s \"%s\"\n",
                (var->flags & CVAR_ARCHIVE) ? '*' : ' ',
                var->name,
                var->string);
        count++;
    }
    fprintf(stderr, "%u cvars\n", count);
}

bool Cvar_Command(void) {
    cvar_t *var = Cvar_FindVar(Cmd_Argv(0));

    if (!var) {
        return false;
    }
    if (Cmd_Argc() == 1) {
        fprintf(stderr, "\"%s\" is \"%s\"\n", var->name, var->string);
    } else {
        Cvar_Set(var->name, Cmd_ArgsFrom(1));
    }
    return true;
}

static LPSTR Cvar_ReadLocalConfig(LPCSTR filename) {
    FILE *file;
    long fileSize;
    LPSTR buffer;

    file = fopen(filename, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    fileSize = ftell(file);
    if (fileSize < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    buffer = MemAlloc(fileSize + 1);
    if (fileSize > 0 && fread(buffer, 1, fileSize, file) != (size_t)fileSize) {
        MemFree(buffer);
        fclose(file);
        return NULL;
    }
    fclose(file);
    buffer[fileSize] = '\0';
    return buffer;
}

void Cvar_LoadConfig(LPCSTR filename) {
    LPSTR text;

    if (!filename || !*filename) {
        return;
    }
    text = FS_ReadFileIntoString(filename);
    if (!text) {
        text = Cvar_ReadLocalConfig(filename);
    }
    if (!text) {
        return;
    }
    fprintf(stderr, "Executing %s\n", filename);
    Cbuf_AddText(text);
    Cbuf_AddText("\n");
    FS_FreeFileString(text);
}

static void Cvar_WriteEscaped(FILE *file, LPCSTR text) {
    for (LPCSTR p = text ? text : ""; *p; p++) {
        if (*p == '"') {
            fputc('\\', file);
        }
        fputc(*p, file);
    }
}

static bool Cvar_IsSessionOnly(LPCSTR name) {
    return name && (!strcmp(name, "map") || !strcmp(name, "connect"));
}

void Cvar_WriteConfig(LPCSTR filename) {
    FILE *file;
    DWORD count = 0;

    if (!filename || !*filename) {
        filename = Cvar_String("config", "");
    }
    file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Couldn't write %s\n", filename);
        return;
    }
    fprintf(file, "// OpenWarcraft3 generated config\n");
    fprintf(file, "// Loaded after share/default.cfg and the build-specific defaults.\n\n");
    Key_WriteBindings(file);
    fprintf(file, "\n");
    FOR_EACH_LIST(cvar_t, var, cvar_vars) {
        if (!(var->flags & CVAR_ARCHIVE) || Cvar_IsSessionOnly(var->name)) {
            continue;
        }
        fprintf(file, "seta %s \"", var->name);
        Cvar_WriteEscaped(file, var->string);
        fprintf(file, "\"\n");
        count++;
    }
    fclose(file);
    fprintf(stderr, "Wrote %u archived cvars to %s\n", count, filename);
}

static void Cvar_WriteConfig_f(void) {
    Cvar_WriteConfig(Cmd_Argc() > 1 ? Cmd_Argv(1) : Cvar_String("config", ""));
}

static void Cvar_Exec_f(void) {
    if (Cmd_Argc() != 2) {
        fprintf(stderr, "usage: exec <filename>\n");
        return;
    }
    Cvar_LoadConfig(Cmd_Argv(1));
}

static bool Cvar_ApplyDashArg(int argc, LPCSTR *argv, int *index, LPCSTR name) {
    LPCSTR arg = argv[*index];
    size_t len = strlen(name);

    if (arg[0] != '-' || strncmp(arg + 1, name, len)) {
        return false;
    }
    if (arg[len + 1] == '\0' && *index + 1 < argc) {
        Cvar_Set(name, argv[++(*index)]);
        return true;
    }
    return false;
}

static bool Cvar_ApplyDashFlag(LPCSTR arg, LPCSTR name, LPCSTR cvar, LPCSTR value) {
    if (!arg || arg[0] != '-' || strcmp(arg + 1, name)) {
        return false;
    }
    Cvar_Set(cvar, value);
    return true;
}

void Cvar_ApplyConfigCommandLine(int argc, LPCSTR *argv) {
    for (int i = 1; i < argc; i++) {
        LPCSTR arg = argv[i];

        if (!arg || !*arg) {
            continue;
        }
        if (Cvar_ApplyDashArg(argc, argv, &i, "config")) {
            continue;
        }
    }
}

void Cvar_ApplyCommandLine(int argc, LPCSTR *argv) {
    for (int i = 1; i < argc; i++) {
        LPCSTR arg = argv[i];

        if (!arg || !*arg) {
            continue;
        }
        if (Cvar_ApplyDashArg(argc, argv, &i, "connect")) {
            continue;
        }
        if (Cvar_ApplyDashArg(argc, argv, &i, "data")) {
            continue;
        }
        if (Cvar_ApplyDashFlag(arg, "tft", "fs_expansion", "1")) {
            continue;
        }
        if (Cvar_ApplyDashFlag(arg, "roc", "fs_expansion", "0")) {
            continue;
        }
    }
}

void Cvar_Init(void) {
    Cmd_AddCommand("set", Cvar_Set_f);
    Cmd_AddCommand("seta", Cvar_SetA_f);
    Cmd_AddCommand("cvarlist", Cvar_List_f);
    Cmd_AddCommand("exec", Cvar_Exec_f);
    Cmd_AddCommand("writeconfig", Cvar_WriteConfig_f);

#ifdef WOW
    Cvar_Get("config", "share/openwow-config.cfg", CVAR_ARCHIVE);
#else
    Cvar_Get("config", "share/openwarcraft3-config.cfg", CVAR_ARCHIVE);
#endif
    Cvar_Get("data", "", CVAR_ARCHIVE);
    Cvar_Get("fs_expansion", "0", 0);
    Cvar_Get("map", "", 0);
    Cvar_Get("connect", "", 0);
    Cvar_Get("cl_debug_entities", "0", 0);
    Cvar_Get("sv_debug_entities", "0", 0);
    Cvar_Get("r_debug_entities", "0", 0);
    Cvar_Get("r_debug_shadows", "0", 0);
    Cvar_Get("r_module", "renderer", CVAR_ARCHIVE);
    Cvar_Get("ui_module", "ui", CVAR_ARCHIVE);
    Cvar_Get("g_module", "game", CVAR_ARCHIVE);
    Cvar_Get("ui_game_setup_map", "", 0);
    Cvar_Get("game_port", PORT_SERVER_STRING, CVAR_ARCHIVE);
    Cvar_Get("name", "Player", CVAR_ARCHIVE);
    Cvar_Get("sv_hostname", "OpenWarcraft3", CVAR_ARCHIVE);
    Cvar_Get("com_frame_limit", "0", 0);
    Cvar_Get("scr_showfps", "1", CVAR_ARCHIVE);
    Cvar_Get("skip_cutscene", "0", 0);
    Cvar_Get("vid_mode", "2", CVAR_ARCHIVE);
    Cvar_Get("r_model_detail", "2", CVAR_ARCHIVE);
    Cvar_Get("r_anim_quality", "2", CVAR_ARCHIVE);
    Cvar_Get("r_texture_quality", "2", CVAR_ARCHIVE);
    Cvar_Get("r_particles", "2", CVAR_ARCHIVE);
    Cvar_Get("r_lights", "2", CVAR_ARCHIVE);
    Cvar_Get("r_occlusion", "1", CVAR_ARCHIVE);
    Cvar_Get("ui_chat_support", "0", CVAR_ARCHIVE);
    Cvar_Get("s_provider", "1", CVAR_ARCHIVE);
}
