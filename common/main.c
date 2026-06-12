#include "../client/client.h"
#include "server/server.h"

#include <SDL2/SDL.h>
#include <stdlib.h>

#if defined(__APPLE__)
#define BZ_PLATFORM "Darwin"
#elif defined(_WIN32)
#define BZ_PLATFORM "Windows"
#elif defined(__linux__)
#define BZ_PLATFORM "Linux"
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
"  openwarcraft3 -data <folder>                 (client menu)\n" \
"  openwarcraft3 -data <folder> -connect <host>  (remote client, default port " \
                                                    PORT_SERVER_STRING ")\n" \
"  openwarcraft3 -data <folder> -connect <host:port>\n" \
"  openwarcraft3 -data <folder> -tft             (mount expansion MPQs)\n" \
"\n" \
"Examples:\n" \
"  openwarcraft3 -data /home/user/Warcraft3 +map Maps\\\\Campaign\\\\Human02.w3m\n" \
"  openwarcraft3 -data /home/user/Warcraft3 -tft +menu_single_player_campaign\n" \
"  openwarcraft3 -data /home/user/Warcraft3 -connect 192.168.1.10\n" \
"\n" \
"Notes:\n" \
"  - The data folder should contain Warcraft III MPQs and optionally Maps/.\n" \
"  - Expansion MPQs are skipped by default; use -tft or +fs_expansion 1 to mount them.\n" \
"  - The data folder may also be saved as data in the generated per-build config.\n" \
"  - The map path uses the internal MPQ path format; use +map to launch one.\n" \
"  - Remote clients still need the game data for asset loading.\n"

extern LPTEXTURE Texture;

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

int main(int argc, LPSTR argv[]) {
    BOOL data = 0;

    fprintf(stderr,
            "\nOpenWarcraft3\n"
            "Platform: %s\n"
            "Architecture: %s\n"
            "Byte ordering: %s\n\n",
            BZ_PLATFORM,
            BZ_ARCH,
            BZ_BYTE_ORDER);

    Com_Init(argc, (LPCSTR *)argv);

    LPCSTR data_dir = Cvar_String("data", "");
    if (data_dir && *data_dir) {
        if (!FS_AddDataDirectory(data_dir)) {
            fprintf(stderr, "Failed to add data directory: %s\n", data_dir);
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
    cls.key_dest = menu_mode ? key_menu : key_game;
    cls.state = ca_disconnected;

    NET_Init();

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

    fprintf(stderr, "OpenWarcraft3 initialized.\n\n");

    DWORD startTime = SDL_GetTicks();
    DWORD frameCount = 0;
    while (true) {
        DWORD currentTime = SDL_GetTicks();
        DWORD msec = currentTime - startTime;
        if (!has_connect_addr && (sv.state == ss_lobby || sv.state == ss_game)) {
            SV_Frame(msec);
        }
        CL_Frame(msec);
        startTime = currentTime;
        frameCount++;
        if (Cvar_Integer("com_frame_limit", 0) > 0 &&
            frameCount >= (DWORD)Cvar_Integer("com_frame_limit", 0)) {
            Com_Quit();
        }
    }

    return 0;
}
