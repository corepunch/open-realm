#include "client.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

BOOL cl_screenshot_pending;
DWORD cl_screenshot_delay;

/* Queue a framebuffer capture for the Nth rendered frame after this command. */
void CL_Screenshot_f(void) {
	char *end = NULL; unsigned long delay = 0;
	if (Cmd_Argc() > 2) { fprintf(stderr, "Usage: screenshot [frame-delay]\n"); return; }
	if (Cmd_Argc() == 2) {
		errno = 0; delay = strtoul(Cmd_Argv(1), &end, 10);
		if (errno || !end || *end || Cmd_Argv(1)[0] == '-' || delay > UINT_MAX) {
			fprintf(stderr, "screenshot: invalid frame delay '%s'\n", Cmd_Argv(1));
			return;
		}
	}
	cl_screenshot_delay = (DWORD)delay; cl_screenshot_pending = true;
}

/* Count rendered frames, not simulation frames, so captures always contain a completed framebuffer. */
BOOL CL_ScreenshotReady(void) {
	if (!cl_screenshot_pending) return false;
	if (cl_screenshot_delay > 1) { cl_screenshot_delay--; return false; }
	cl_screenshot_pending = false; cl_screenshot_delay = 0;
	return true;
}
