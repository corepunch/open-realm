/* Browser-only early map loading indicator. */
#ifndef OPENREALM_WEB_LOADING_H
#define OPENREALM_WEB_LOADING_H
#ifdef __EMSCRIPTEN__
#include <stdbool.h>
void Web_LoadingOverlay(bool visible);
#endif
#endif
