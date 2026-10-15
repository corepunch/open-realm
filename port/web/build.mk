# Browser build foundation. Invoked with: make web-warcraft3-foundation
# This target intentionally does not claim that the native engine links to WASM.
EMCC ?= emcc
WEB_BUILD_DIR ?= build/web
WEB_PROBE_SRC := port/web/src/web_probe.c
WEB_PROBE_HTML := $(WEB_BUILD_DIR)/index.html

.PHONY: web-warcraft3-foundation
web-warcraft3-foundation: $(WEB_PROBE_HTML)

$(WEB_PROBE_HTML): $(WEB_PROBE_SRC) port/web/site/shell.html port/web/site/browser-keyboard.js port/web/site/installer.js port/web/site/install-worker.js port/web/site/iso9660.js port/web/site/setup-mpq.js port/web/site/mpq-header.js
	@command -v $(EMCC) >/dev/null || { echo 'Emscripten emcc is required (activate emsdk first)' >&2; exit 1; }
	@mkdir -p $(WEB_BUILD_DIR)
	$(EMCC) $(WEB_PROBE_SRC) -O2 -sUSE_SDL=2 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
		-sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web -sFILESYSTEM=1 \
		--shell-file port/web/site/shell.html -o $@
	cp port/web/site/browser-keyboard.js port/web/site/installer.js port/web/site/install-worker.js port/web/site/iso9660.js port/web/site/setup-mpq.js port/web/site/mpq-header.js $(WEB_BUILD_DIR)/

# Patch 2: source-level engine browser scheduling integration. This checks the
# integration contract without claiming the full engine is yet linkable to WASM.
.PHONY: web-warcraft3-engine-check
web-warcraft3-engine-check:
	@grep -q 'emscripten_set_main_loop_arg(OpenRealm_Frame' common/main.c || { echo 'Browser engine frame scheduler missing' >&2; exit 1; }
	@grep -q 'GetGameAPI(&import)' server/sv_game.c || { echo 'Game module contract changed' >&2; exit 1; }
	@grep -q 'refExport_t R_GetAPI(' renderer/r_main.c || { echo 'Renderer module entry point missing' >&2; exit 1; }
	@grep -q 'menuExport_t M_GetAPI(' games/warcraft-3/menu/menu_main.c || { echo 'Menu module entry point missing' >&2; exit 1; }
	@echo 'Browser frame scheduling integration present (source check only; no compilation)'

# Patch 4: experimental whole-engine static link. The foundation target remains
# a standalone graphics probe; this target compiles the real source modules.
.PHONY: web-warcraft3-engine
web-warcraft3-engine:
	python3 port/web/build-engine.py --emcc $(EMCC) --compat-workarounds $(COMPAT_WORKAROUNDS) --out $(WEB_BUILD_DIR)/engine

# Patch 10: non-compiling source-layout preflight. Useful before installing emsdk.
.PHONY: web-warcraft3-preflight
web-warcraft3-preflight:
	python3 port/web/build-engine.py --check --out $(WEB_BUILD_DIR)/engine
