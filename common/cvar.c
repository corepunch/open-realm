#include "common.h"
#include "common/ui_constants.h"

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

void Cvar_Describe(LPCSTR name, LPCSTR description) {
    cvar_t *var = Cvar_FindVar(name);
    if (var) var->description = description;
}

cvar_t *Cvar_GetD(LPCSTR name, LPCSTR value, DWORD flags, LPCSTR description) {
    cvar_t *var = Cvar_Get(name, value, flags);
    if (var) var->description = description;
    return var;
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
            if (var->description)
                fprintf(stderr, "  %-28s — %s\n", var->name, var->description);
            else
                fprintf(stderr, "  %s\n", var->name);
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
        if (var->description)
            fprintf(stderr, "%c %-28s \"%s\"  — %s\n",
                    (var->flags & CVAR_ARCHIVE) ? '*' : ' ',
                    var->name, var->string, var->description);
        else
            fprintf(stderr, "%c %s \"%s\"\n",
                    (var->flags & CVAR_ARCHIVE) ? '*' : ' ',
                    var->name, var->string);
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
        if (var->description)
            fprintf(stderr, "\"%s\" is \"%s\"  — %s\n", var->name, var->string, var->description);
        else
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
    Cvar_GetD("config", "share/openwow-config.cfg",       CVAR_ARCHIVE, "config file path; loaded/saved on startup");
#else
    Cvar_GetD("config", "share/openwarcraft3-config.cfg", CVAR_ARCHIVE, "config file path; loaded/saved on startup");
#endif
    Cvar_GetD("data",             "",                  CVAR_ARCHIVE, "override game data directory path");
    Cvar_GetD("fs_expansion",     "0",                 0,            "0=RoC data only, 1=include TFT expansion data");
    Cvar_GetD("map",              "",                  0,            "map file to load at startup (e.g. Maps/HumanCampaign1.w3m)");
    Cvar_GetD("connect",          "",                  0,            "server address to connect to at startup");
    Cvar_GetD("cl_debug_entities","0",                 0,            "log client-side entity sync events");
    Cvar_GetD("sv_debug_entities","0",                 0,            "log server-side entity sync events");
    Cvar_GetD("r_debug_entities", "0",                 0,            "log renderer entity lifecycle events");
    Cvar_GetD("r_module",         "renderer",          CVAR_ARCHIVE, "renderer shared library name");
    Cvar_GetD("ui_module",        "ui",                CVAR_ARCHIVE, "UI shared library name");
    Cvar_GetD("g_module",         "game",              CVAR_ARCHIVE, "game logic shared library name");
    Cvar_GetD("ui_game_setup_map","",                  0,            "map pre-selected in game setup UI");
    Cvar_GetD("game_port",        PORT_SERVER_STRING,  CVAR_ARCHIVE, "UDP port the game server listens on");
    Cvar_GetD("name",             "Player",            CVAR_ARCHIVE, "player display name shown in lobbies");
    Cvar_GetD("sv_hostname",      "OpenWarcraft3",     CVAR_ARCHIVE, "server name shown in lobby browser");
    Cvar_GetD("sv_cheats",        "0",                 0,            "enable cheat commands on this server");
    Cvar_GetD("com_frame_limit",  "0",                 0,            "cap frame rate in fps; 0=unlimited");
    Cvar_GetD("scr_showfps",      "1",                 CVAR_ARCHIVE, "show FPS counter on screen");
    Cvar_GetD("skip_cutscene",    "0",                 0,            "skip intro cutscene on startup");
    Cvar_GetD("vid_mode",         "2",                 CVAR_ARCHIVE, "window mode: 0=windowed, 1=fullscreen, 2=borderless");
    Cvar_GetD("r_model_detail",   "2",                 CVAR_ARCHIVE, "model LOD quality: 0=low, 1=medium, 2=high");
    Cvar_GetD("r_anim_quality",   "2",                 CVAR_ARCHIVE, "animation interpolation quality: 0=off, 2=full");
    Cvar_GetD("r_texture_quality","2",                 CVAR_ARCHIVE, "texture mip level: 0=low, 1=medium, 2=full");
    Cvar_GetD("r_particles",      "2",                 CVAR_ARCHIVE, "particle effect density: 0=off, 1=reduced, 2=full");
    Cvar_GetD("r_lights",         "2",                 CVAR_ARCHIVE, "dynamic light count: 0=off, 1=minimal, 2=max");
    Cvar_GetD("r_unit_shadows",   "1",                 CVAR_ARCHIVE, "render blob shadows under units");
    Cvar_GetD("r_occlusion",      "1",                 CVAR_ARCHIVE, "frustum-cull off-screen entities");
    Cvar_GetD("r_msaa",           "4",                 CVAR_ARCHIVE, "multisample anti-aliasing samples: 0, 2, 4, or 8");
    Cvar_GetD("r_vsync",          "0",                 CVAR_ARCHIVE, "vertical sync: 0=off (use for perf testing), 1=on");
    Cvar_GetD("r_stats",          "0",                 0,            "print per-frame draw/world stats to console each second");
    Cvar_GetD("r_entities",       "1",                 0,            "render game entities (units, buildings, etc.)");
    Cvar_GetD("r_fogofwar",       "1",                 0,            "render fog-of-war overlay");
#ifdef WOW
    /* Bare Quake-style console assignment only works for cvars registered before command dispatch. */
    Cvar_GetD("r_fog",            "1",                 CVAR_ARCHIVE, "render world distance fog");
    Cvar_GetD("r_fog_start",      WOW_WORLD_FOG_START_STRING, CVAR_ARCHIVE, "fog start distance in WoW yards");
    Cvar_GetD("r_fog_end",        WOW_WORLD_FOG_END_STRING,   CVAR_ARCHIVE, "fog cutoff distance in WoW yards");
    Cvar_GetD("r_grass",          "1",                 0,            "render ground-cover grass (14 M2 instanced draw calls when on)");
    Cvar_GetD("r_doodads",        "1",                 0,            "render terrain and WMO doodad props");
    Cvar_GetD("r_wmos",           "1",                 0,            "render WMO buildings and structures");
    Cvar_GetD("r_terrain",        "1",                 0,            "render ADT terrain chunks");
    Cvar_GetD("r_minimap",        "1",                 0,            "render minimap overlay in corner");
#endif
    Cvar_GetD("ui_chat_support",  "0",                 CVAR_ARCHIVE, "enable in-game chat UI panel");
    Cvar_GetD("s_provider",       "1",                 CVAR_ARCHIVE, "sound backend: 0=none, 1=OpenAL");
}
