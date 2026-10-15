/* Browser-only map loading notification. Never blocks the engine worker. */
#ifdef __EMSCRIPTEN__
#include "web_loading.h"
#include <emscripten/em_asm.h>
void Web_LoadingOverlay(bool visible) {
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined' &&
            typeof window.openrealmEarlyMapLoading === 'function')
            window.openrealmEarlyMapLoading($0 !== 0);
    }, visible ? 1 : 0);
}
#endif
