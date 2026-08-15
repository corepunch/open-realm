#include "client.h"

BOOL cl_screenshot_pending;

void CL_Screenshot_f(void) {
	cl_screenshot_pending = true;
}
