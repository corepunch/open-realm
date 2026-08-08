#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "renderer/stb/stb_image_write.h"

#include "client.h"
#include <sys/stat.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

BOOL cl_screenshot_pending;
static char cl_screenshot_dir[256] = "screenshots";
static char cl_screenshot_name[256];

static void CL_EnsureScreenshotDir(void) {
#ifndef _WIN32
	mkdir(cl_screenshot_dir, 0777);
#else
	_mkdir(cl_screenshot_dir);
#endif
}

static int CL_FindFreeSlot(LPCSTR prefix, LPCSTR ext) {
	int i;
	char path[512];
	for (i = 0; i <= 9999; i++) {
		snprintf(path, sizeof(path), "%s/%s%04d.%s", cl_screenshot_dir, prefix, i, ext);
		FILE *f = fopen(path, "rb");
		if (!f) return i;
		fclose(f);
	}
	return -1;
}

void CL_ScreenshotCapture(void) {
	if (!cl_screenshot_pending)
		return;
	cl_screenshot_pending = false;

	size2_t window = re.GetWindowSize();
	DWORD width = window.width, height = window.height;
	if (!width || !height)
		return;

	CL_EnsureScreenshotDir();

	char path[512];
	if (cl_screenshot_name[0])
		snprintf(path, sizeof(path), "%s/%s.png", cl_screenshot_dir, cl_screenshot_name);
	else {
		int slot = CL_FindFreeSlot("shot", "png");
		if (slot < 0) { CON_printf("Screenshot: no free slot (max 10000)\n"); return; }
		snprintf(path, sizeof(path), "%s/shot%04d.png", cl_screenshot_dir, slot);
	}
	cl_screenshot_name[0] = '\0';

	BYTE *pixels = malloc((size_t)width * height * 4);
	if (!pixels) {
		CON_printf("Screenshot: alloc failed\n");
		return;
	}

	glReadPixels(0, 0, (GLsizei)width, (GLsizei)height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	stbi_flip_vertically_on_write(1);
	int ok = stbi_write_png(path, (int)width, (int)height, 4, pixels, (int)width * 4);
	if (ok)
		CON_printf("Wrote %s\n", path);
	else
		CON_printf("Screenshot: write failed for %s\n", path);
	free(pixels);
}

void CL_Screenshot_f(void) {
	cl_screenshot_pending = true;
}

#ifdef WOW
/* Called by CL_ParseGameCommand when server sends "screenshot <name>". */
void CL_WoweeScreenshot(LPCSTR name) {
	snprintf(cl_screenshot_name, sizeof(cl_screenshot_name), "wowee_%s", name);
	cl_screenshot_pending = true;
}
#endif
