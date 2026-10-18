/* Browser DOM loading presentation adapter. Never blocks the engine worker. */
#ifdef __EMSCRIPTEN__
#include "common/common.h"
#include <emscripten/em_asm.h>
void Sys_UpdateLoadingPresentation(bool active, float progress, bool loading_screen_submitted) {
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined' &&
            typeof window.openrealmEarlyMapLoading === 'function')
            window.openrealmEarlyMapLoading($0 !== 0, $1, $2 !== 0);
    }, active ? 1 : 0, progress, loading_screen_submitted ? 1 : 0);
}
#endif
