/* Mount browser-local OPFS using Emscripten WasmFS; no MPQ file copies. */
#ifdef __EMSCRIPTEN__
#include "web_opfs.h"
#include <emscripten/wasmfs.h>
#include <stdio.h>
#include <sys/stat.h>

int WebOPFS_Mount(void) {
    backend_t backend = wasmfs_create_opfs_backend();
    if (!backend || wasmfs_create_directory("/opfs", 0777, backend) != 0) {
        fputs("OpenRealm: unable to mount browser OPFS storage\n", stderr);
        return 0;
    }
    return 1;
}
#endif
