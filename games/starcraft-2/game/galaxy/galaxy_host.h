#ifndef galaxy_host_h
#define galaxy_host_h

#include "../../warcraft-3/jass/jass.h"

/* galaxy_get_natives — return the SC2 native function table.
 * Assign to host.galaxy_natives before calling jass_sethost(). */
LPCJASSMODULE galaxy_get_natives(void);

#endif
