#include "../client/client.h"
#include "server/server.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef _WIN32
#include <conio.h>
#include <direct.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#define BZ_PLATFORM "Darwin"
#elif defined(_WIN32)
#define BZ_PLATFORM "Windows"
#elif defined(__linux__)
#define BZ_PLATFORM "Linux"
#elif defined(__OpenBSD__)
#define BZ_PLATFORM "OpenBSD"
#else
#define BZ_PLATFORM "Unknown"
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define BZ_ARCH "arm64"
#elif defined(__x86_64__) || defined(_M_X64)
#define BZ_ARCH "x86_64"
#elif defined(__i386__) || defined(_M_IX86)
#define BZ_ARCH "x86"
#else
#define BZ_ARCH "unknown"
#endif

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BZ_BYTE_ORDER "big endian"
#else
#define BZ_BYTE_ORDER "little endian"
#endif

#define USAGE \
"Usage:\n" \
"  openwarcraft3 -data <folder> +map <map>       (listen server + local client)\n" \
"  openwarcraft3 -data <folder> +dedicated 1 +map <map>  (dedicated server, no client)\n" \
"  openwarcraft3 -data <folder>                 (client menu)\n" \
"  openwarcraft3 -data <folder> -connect <host>  (remote client, default port " \
                                                    PORT_SERVER_STRING ")\n" \
"  openwarcraft3 -data <folder> -connect <host:port>\n" \
"  openwarcraft3 -data <folder> -tft             (mount expansion MPQs)\n" \
"\n" \
"Examples:\n" \
"  openwarcraft3 -data /home/user/Warcraft3 +map Maps\\\\Campaign\\\\Human02.w3m\n" \
"  openwarcraft3 -data /home/user/Warcraft3 +dedicated 1 +map Maps\\\\Campaign\\\\Human02.w3m\n" \
"  openwarcraft3 -data /home/user/Warcraft3 -tft +menu_single_player_campaign\n" \
"  openwarcraft3 -data /home/user/Warcraft3 -connect 192.168.1.10\n" \
"\n" \
"Notes:\n" \
"  - The data folder should contain Warcraft III MPQs and optionally Maps/.\n" \
"  - Expansion MPQs are skipped by default; use -tft or +fs_expansion 1 to mount them.\n" \
"  - The data folder may also be saved as data in the generated per-build config.\n" \
"  - The map path uses the internal MPQ path format; use +map to launch one.\n" \
"  - Remote clients still need the game data for asset loading.\n" \
"  - Dedicated mode runs the server headless without renderer, sound, or UI.\n"

extern LPTEXTURE Texture;

#ifdef _WIN32
static BOOL Sys_FileExists(LPCSTR filename) {
    FILE *file = fopen(filename, "rb");

    if (!file) {
        return false;
    }
    fclose(file);
    return true;
}

static BOOL Sys_IsWarcraftDataDirectory(LPCSTR dirname) {
    PATHSTR archive;

    if (!dirname || !*dirname) {
        return false;
    }
    snprintf(archive, sizeof(archive), "%s/war3.mpq", dirname);
    return Sys_FileExists(archive);
}

static BOOL Sys_TryWarcraftDataDirectory(LPCSTR dirname,
                                         LPSTR result,
                                         size_t result_size) {
    if (!Sys_IsWarcraftDataDirectory(dirname)) {
        return false;
    }
    strlcpy(result, dirname, result_size);
    return true;
}

static BOOL Sys_DiscoverWarcraftDataDirectory(LPSTR result, size_t result_size) {
    static LPCSTR relative_candidates[] = {
        ".",
        "data",
        "Warcraft III",
        NULL
    };
    static LPCSTR drive_candidates[] = {
        "%c:/Warcraft III",
        "%c:/Games/Warcraft III",
        "%c:/Program Files/Warcraft III",
        "%c:/Program Files (x86)/Warcraft III",
        NULL
    };
    LPCSTR environment_path = getenv("WARCRAFT_III_PATH");
    LPSTR executable_path = SDL_GetBasePath();
    unsigned long drives = _getdrives();
    PATHSTR candidate;

    if (Sys_TryWarcraftDataDirectory(environment_path, result, result_size)) {
        SDL_free(executable_path);
        return true;
    }
    if (executable_path) {
        if (Sys_TryWarcraftDataDirectory(executable_path, result, result_size)) {
            SDL_free(executable_path);
            return true;
        }
        snprintf(candidate, sizeof(candidate), "%sdata", executable_path);
        if (Sys_TryWarcraftDataDirectory(candidate, result, result_size)) {
            SDL_free(executable_path);
            return true;
        }
        SDL_free(executable_path);
    }
    for (LPCSTR *path = relative_candidates; *path; path++) {
        if (Sys_TryWarcraftDataDirectory(*path, result, result_size)) {
            return true;
        }
    }
    for (int drive = 0; drive < 26; drive++) {
        if (!(drives & (1UL << drive))) {
            continue;
        }
        for (LPCSTR *format = drive_candidates; *format; format++) {
            snprintf(candidate, sizeof(candidate), *format, 'A' + drive);
            if (Sys_TryWarcraftDataDirectory(candidate, result, result_size)) {
                return true;
            }
        }
    }
    return false;
}

static void Sys_ShowStartupError(LPCSTR message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             "OpenWarcraft3 could not start",
                             message,
                             NULL);
}
#endif

/* Anchor the read-only share/ tree at the executable's location so the binary
 * finds its configs regardless of the working directory. Probes three layouts:
 * flat portable (share/ beside the exe), the FHS build tree (share/ beside
 * bin/), and the in-tree CWD fallback. */
static void Sys_ResolveShareDirectory(void) {
    LPSTR base = SDL_GetBasePath();

    if (base) {
        PATHSTR share;
        snprintf(share, sizeof(share), "%sshare", base);
        FS_SetShareDirectory(share);
        snprintf(share, sizeof(share), "%s../share", base);
        FS_SetShareDirectory(share);
        SDL_free(base);
    }
    FS_SetShareDirectory("share");
}

/* Resolve the writable per-user directory to $HOME/.<BZ_GAME>; only adopted if
 * creatable and writable (verified by FS_SetHomeDirectory), so a read-only or
 * absent $HOME leaves FS_UserPath to fall back to share/<game>/. */
static void Sys_ResolveHomeDirectory(void) {
#ifdef _WIN32
    LPCSTR home = getenv("USERPROFILE");
#else
    LPCSTR home = getenv("HOME");
#endif
    PATHSTR dir;

    if (!home || !*home) {
        return;
    }
    snprintf(dir, sizeof(dir), "%s/.%s", home, BZ_GAME);
    FS_SetHomeDirectory(dir);
}

static unsigned short Sys_GamePort(void) {
    int port = Cvar_Integer("game_port", PORT_SERVER);

    if (port <= 0 || port > 65535) {
        fprintf(stderr,
                "Invalid game_port %d, using default %u\n",
                port,
                (unsigned)PORT_SERVER);
        Cvar_Set("game_port", PORT_SERVER_STRING);
        port = PORT_SERVER;
    }
    return (unsigned short)port;
}

void Sys_Quit(void) {
    exit(0);
}

/* Read a line from stdin for dedicated server console input.
 * Returns NULL if no input is available (non-blocking check). */
static LPSTR Sys_ConsoleInput(void) {
    static char line[256];
    static char *pos = line;
    int ch;

    if (!Cvar_Integer("dedicated", 0)) {
        return NULL;
    }
    /* Non-blocking check: is there data on stdin? */
#ifdef _WIN32
    if (!_kbhit()) {
        return NULL;
    }
#else
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) {
        return NULL;
    }
#endif
    while (1) {
        ch = fgetc(stdin);
        if (ch == EOF) {
            pos = line;
            return NULL;
        }
        if (ch == '\n') {
            *pos = '\0';
            pos = line;
            return line;
        }
        if (pos < line + sizeof(line) - 1) {
            *pos++ = (char)ch;
        }
    }
}

int main(int argc, LPSTR argv[]) {
    BOOL data = 0;
#ifdef _WIN32
    PATHSTR discovered_data_dir = { 0 };
#endif

    fprintf(stderr,
            "\nOpenWarcraft3\n"
            "Platform: %s\n"
            "Architecture: %s\n"
            "Byte ordering: %s\n\n",
            BZ_PLATFORM,
            BZ_ARCH,
            BZ_BYTE_ORDER);

    Sys_ResolveShareDirectory();
    Sys_ResolveHomeDirectory();
    Com_Init(argc, (LPCSTR *)argv);

    LPCSTR data_dir = Cvar_String("data", "");
#ifdef _WIN32
    if ((!data_dir || !*data_dir) &&
        Sys_DiscoverWarcraftDataDirectory(discovered_data_dir,
                                          sizeof(discovered_data_dir))) {
        Cvar_Set("data", discovered_data_dir);
        /* The desktop entry point presents the Frozen Throne glue UI.  A
         * first-run double-click therefore needs the expansion archives too;
         * command-line launches retain their explicit -tft/-roc choice. */
        Cvar_Set("fs_expansion", "1");
        data_dir = Cvar_String("data", "");
        fprintf(stderr, "Discovered Warcraft III data: %s\n", data_dir);
    }
#endif
    if (data_dir && *data_dir) {
        if (!FS_AddDataDirectory(data_dir)) {
            fprintf(stderr, "Failed to add data directory: %s\n", data_dir);
#ifdef _WIN32
            Sys_ShowStartupError(
                "The configured Warcraft III folder is unavailable.\n\n"
                "Start OpenWarcraft3 with:\n"
                "  openwarcraft3.exe -data \"C:\\path\\to\\Warcraft III\"\n\n"
                "The folder must contain war3.mpq.");
#endif
            return 1;
        }
        data = 1;
    }

    LPCSTR extra_data_dir = Cvar_String("extra_data", "");
    if (extra_data_dir && *extra_data_dir) {
        FS_AddDataDirectory(extra_data_dir);
    }

    if (!data) {
        printf(USAGE);
#ifdef _WIN32
        Sys_ShowStartupError(
            "A compatible Warcraft III installation was not found.\n\n"
            "Install Warcraft III Classic, put openwarcraft3.exe in its folder, "
            "or start it with:\n"
            "  openwarcraft3.exe -data \"C:\\path\\to\\Warcraft III\"\n\n"
            "You can also set the WARCRAFT_III_PATH environment variable.");
#endif
        return 1;
    }

    PATHSTR resolved_map;
    LPCSTR map = Cvar_String("map", "");
    LPCSTR connect_addr = Cvar_String("connect", "");
    bool has_map = map && *map;
    bool has_connect_addr = connect_addr && *connect_addr;
    bool menu_mode = !has_map && !has_connect_addr;
    bool listen_server_mode = has_map && !has_connect_addr;
    unsigned short game_port = Sys_GamePort();

    if (has_map) {
        if (!Com_ResolveMapArgument(map, resolved_map, sizeof(resolved_map))) {
            return 1;
        }
        map = resolved_map;
    }
    bool dedicated = Cvar_Integer("dedicated", 0) != 0;

    /* Dedicated server mode: follow the Quake 2 convention of running the
     * server without the client stack.  Unlike Quake 2 which uses a
     * compile-time cl_null.c stub, we use runtime checks here because the
     * codebase is not yet structured for a separate dedicated target and
     * runtime branching keeps a single binary for both modes. */
    cls.key_dest = dedicated ? key_game : (menu_mode ? key_menu : key_game);
    cls.state = ca_disconnected;

    NET_Init();

    /* Headless test mode: `+test <pattern>` runs the in-engine test registry
     * without a map.  The game module is a link dependency, so its TEST()
     * constructors have already registered by the time we get here. */
    bool run_tests = false;
    for (int i = 1; i < COM_Argc(); i++) {
        if (!strcmp(COM_Argv(i), "+test")) { run_tests = true; break; }
    }

    if (dedicated) {
        // Dedicated server mode: no client stack, no SDL window.
        if (!has_map && !run_tests) {
            fprintf(stderr, "Dedicated server requires +map <map>\n");
            return 1;
        }
        SV_Init();
        if (has_map) {
            fprintf(stderr, "Dedicated server starting on map: %s\n", map);
            /* Call SV_Map directly instead of routing through the 'map' command,
             * because Com_Map_f -> MenuAction -> CL_BeginLoadingMap requires the
             * client stack which is not initialized in dedicated mode. */
            SV_Map(map);
        }
        if (run_tests) {
            /* Bring up the real game module (gi + globals via GetGameAPI, then
             * ge->Init) so in-engine tests exercise the actual server import
             * table and can spawn entities / register models from the mounted
             * archives.  When a map was loaded, SV_Map already did this. */
            if (!has_map) {
                SV_InitGameProgs();
            }
            /* Fire the queued `test` command; Com_Test_f exits with the
             * failure count once the registry has run. */
            Cbuf_AddLateCommands();
            Cbuf_Execute();
        }
    } else {
        if (!menu_mode) {
            SV_Init();
        }
        CL_Init();
        Cbuf_AddLateCommands();
        Cbuf_Execute();

        if (has_connect_addr) {
            // Remote-client mode: skip the local server, connect over UDP.
            CL_Connect(connect_addr, game_port);
        } else if (listen_server_mode) {
            // Listen-server mode: show the client loading screen before the
            // synchronous server map load, mirroring Quake's loading plaque flow.
            if (!svs.initialized) {
                SV_Init();
            }
            CL_BeginLoadingMap(map);
            SCR_UpdateScreen(0);
            SV_Map(map);
        }
        // Menu mode: UI runs client-side, no server connection needed (Quake 3 pattern)
    }

    fprintf(stderr, "OpenWarcraft3 initialized.\n\n");

    DWORD startTime = SDL_GetTicks();
    DWORD frameCount = 0;
    while (true) {
        DWORD currentTime = SDL_GetTicks();
        DWORD msec = currentTime - startTime;
        if (svs.initialized && (sv.state == ss_lobby || sv.state == ss_game)) {
            SV_Frame(msec);
        }
        if (!dedicated) {
            CL_Frame(msec);
        } else {
            /* Dedicated server: read console commands from stdin. */
            LPSTR cmd = Sys_ConsoleInput();
            if (cmd && *cmd) {
                Cbuf_AddText(cmd);
                Cbuf_AddText("\n");
                Cbuf_Execute();
            }
        }
        startTime = currentTime;
        frameCount++;
        if (Cvar_Integer("com_frame_limit", 0) > 0 &&
            frameCount >= (DWORD)Cvar_Integer("com_frame_limit", 0)) {
            Com_Quit();
        }
    }

    return 0;
}
