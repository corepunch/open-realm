# OpenWarcraft3 iPad build (no Xcode project).
#
# Mirrors the mapview/ui packaging/ipad/build.mk workflow: every compile is a
# direct `xcrun --sdk` clang invocation orchestrated by make, the .app bundle
# is assembled by tools/ipad/bundle.py, signing by tools/ipad/sign.py. There
# is no .xcodeproj; `make ipad*` from the top-level Makefile is the entry
# point.
#
# Module topology mirrors the desktop build (games/warcraft-3/game.mk), which
# ships game/menu/renderer/jass/sheet/shared as separate shared libraries:
# game and menu each carry a private copy of the stb_fdf parser (own globals,
# own host/theme bindings; g_world.c even textually includes world_w3.c), so
# statically linking them into one image fails with ~150 duplicate symbols.
# iOS therefore embeds them as Frameworks/ dynamic libraries, exactly as
# desktop keeps them as .so files. No source changes are needed for this.
#
# Outputs: build/ipad/<SDK>-<ARCH>/warcraft3/Warcraft3.app
# The SDL2 runtime is built from the release tarball with CMake's Unix
# Makefiles generator (no Xcode project) and linked into shared.framework.

.DEFAULT_GOAL := app
.DELETE_ON_ERROR:

BUILD_DIR ?= build/ipad
SDK ?= iphoneos
ARCH ?= $(if $(filter iphoneos,$(SDK)),arm64,$(shell uname -m))
IOS_MIN ?= 16.0
BUNDLE_ID ?= com.openwarcraft3.warcraft3
TEAM ?=
PROFILE ?=
DEVICE ?=

ifeq ($(filter $(SDK),iphoneos iphonesimulator),)
$(error SDK must be iphoneos or iphonesimulator)
endif
ifeq ($(filter $(ARCH),arm64 x86_64),)
$(error ARCH must be arm64 or x86_64)
endif

SDK_PATH := $(shell xcrun --sdk $(SDK) --show-sdk-path)
SDK_VERSION := $(shell xcrun --sdk $(SDK) --show-sdk-version)
BUILD_ROOT := $(abspath $(BUILD_DIR))/$(SDK)-$(ARCH)
APP_ROOT := $(BUILD_ROOT)/warcraft3
FW_DIR := $(BUILD_ROOT)/frameworks
BUNDLE := $(APP_ROOT)/Warcraft3.app
BINARY := $(APP_ROOT)/warcraft3
COMPILER := xcrun --sdk $(SDK) clang
MIN_FLAG := $(if $(filter iphoneos,$(SDK)),-miphoneos-version-min,-mios-simulator-version-min)=$(IOS_MIN)

# SDL2 runtime (built once per SDK/ARCH; matches the macOS native 2.32.10).
SDL_VERSION := 2.32.10
SDL_TARBALL := $(abspath $(BUILD_DIR))/SDL2-$(SDL_VERSION).tar.gz
SDL_URL := https://github.com/libsdl-org/SDL/releases/download/release-$(SDL_VERSION)/SDL2-$(SDL_VERSION).tar.gz
SDL_SRC := $(abspath $(BUILD_DIR))/SDL2-$(SDL_VERSION)
SDL_BUILD := $(BUILD_ROOT)/sdl-build
SDL_PREFIX := $(SDL_BUILD)/prefix
SDL_LIB := $(SDL_PREFIX)/lib/libSDL2.a
SDL_MAIN_LIB := $(SDL_PREFIX)/lib/libSDL2main.a
SDL_MARKER := $(SDL_BUILD)/built-by-make

# Engine flags. BASE is game-agnostic (shared/sheet, like desktop CFLAGS);
# WC3 carries the game selector + GLES3 renderer (r_local.h selects
# <OpenGLES/ES3/gl.h> under TARGET_OS_IPHONE, r_main.c requests an ES 3.0 SDL
# GL context; epoxy is never used on Apple targets). FDF repeats the desktop
# WC3_FDF_CFLAGS scope (game/menu/app only: jass's own lexer defines
# eat_token, which an implementation TU would redefine as static).
BASE_INCLUDES := -I. -Ishared -Ishared/types -I"$(SDL_PREFIX)/include"
BASE_FLAGS := -isysroot "$(SDK_PATH)" -arch $(ARCH) $(MIN_FLAG) -O2 -g \
	-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
	-MMD -MP $(BASE_INCLUDES)
WC3_INCLUDES := -Icommon -Iclient -Iserver -Irenderer -Isound \
	-Igames/warcraft-3 -Igames/warcraft-3/common -Igames/warcraft-3/game \
	-Igames/warcraft-3/game/api -Igames/warcraft-3/game/skills \
	-Igames/warcraft-3/jass -Igames/warcraft-3/sheet -Igames/warcraft-3/menu
WC3_FLAGS := $(BASE_FLAGS) $(WC3_INCLUDES) \
	-DWC3 -DUSE_FOGOFWAR -DBZ_GAME='"warcraft-3"' -DBZ_GL_ES3 -DLUA_USE_IOS
FDF_FLAGS := -DSTB_FDF_IMPLEMENTATION -DSTB_FDF_GLOBALS

# SDL's static archive references iOS system frameworks; whoever links SDL
# (shared.framework) must also link these. The executable links the same set
# for SDL2main's UIKit startup stub.
SDL_SYSTEM_LIBS := -framework AudioToolbox -framework AVFoundation \
	-framework CoreAudio -framework CoreBluetooth -framework CoreGraphics -framework CoreHaptics \
	-framework CoreMotion -framework CoreVideo -framework Foundation \
	-framework GameController -framework Metal -framework OpenGLES \
	-framework QuartzCore -framework UIKit

# Unity sources per framework. Partition mirrors games/warcraft-3/game.mk,
# including its quirks: g_world.c textually includes world_w3.c (hence the
# !world_w3.c exclude), the app unity drops stb_vorbis.c/sv_routing.c.
SHARED_SRCS := $(shell find shared -name '*.c' | sort) vendor/blast/blast.c
JASS_SRCS := $(shell find games/warcraft-3/jass -name '*.c' | sort)
SHEET_SRCS := games/warcraft-3/sheet/parser.c games/warcraft-3/sheet/sheet.c
RENDERER_SRCS := $(shell find renderer games/warcraft-3/renderer -name '*.c' | sort) common/mpq.c
GAME_SRCS := $(shell find games/warcraft-3/game games/warcraft-3/common -name '*.c' ! -name 'world_w3.c' | sort) common/mpq.c
MENU_SRCS := $(shell find games/warcraft-3/menu games/warcraft-3/common -name '*.c' ! -name 'world_w3.c' | sort) common/mpq.c
# NOTE: common/main.c stays in the app unity. SDL2main provides the platform
# main() and calls our SDL_main (SDL_main.h renames our main); dropping
# main.c would leave SDL_main undefined, exactly as on desktop.
APP_SRCS := $(shell find client server common sound -name '*.c' ! -name 'stb_vorbis.c' ! -name 'sv_routing.c' | sort) \
	games/warcraft-3/common/world_w3.c

FRAMEWORKS := shared jass sheet renderer game menu
FW_BINS := $(foreach fw,$(FRAMEWORKS),$(FW_DIR)/$(fw).framework/$(fw))

.PHONY: app run deploy mac sdl settings frameworks

settings:
	@mkdir -p "$(APP_ROOT)" "$(FW_DIR)"
	@printf '%s\n' '$(COMPILER) $(WC3_FLAGS) $(FDF_FLAGS) $(SDK_VERSION)' > "$(APP_ROOT)/settings.tmp"
	@/usr/bin/cmp -s "$(APP_ROOT)/settings.tmp" "$(APP_ROOT)/settings" || cp "$(APP_ROOT)/settings.tmp" "$(APP_ROOT)/settings"
$(APP_ROOT)/settings: settings

# SDL2 runtime from source, no Xcode project: CMake Unix Makefiles with the
# iOS system name and the selected SDK as sysroot.
$(SDL_TARBALL):
	@mkdir -p "$(abspath $(BUILD_DIR))"
	curl -L -o "$@" "$(SDL_URL)"

$(SDL_SRC): $(SDL_TARBALL)
	@tar -xzf "$<" -C "$(abspath $(BUILD_DIR))"
	@touch "$@"

$(SDL_MARKER): $(SDL_SRC)
	@mkdir -p "$(SDL_BUILD)"
	@cd "$(SDL_BUILD)" && cmake "$(SDL_SRC)" -G "Unix Makefiles" \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_SYSROOT="$(SDK_PATH)" \
		-DCMAKE_OSX_ARCHITECTURES="$(ARCH)" \
		-DCMAKE_OSX_DEPLOYMENT_TARGET="$(IOS_MIN)" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$(SDL_PREFIX)" \
		-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF
	@cmake --build "$(SDL_BUILD)"
	@cmake --install "$(SDL_BUILD)"
	@test -f "$(SDL_LIB)" -a -f "$(SDL_MAIN_LIB)"
	@touch "$@"

sdl: $(SDL_MARKER)

# Unity TU generator: $(1) = TU name, $(2) = sources. Sources are real
# prerequisites so a new file regenerates the TU (like the desktop unity_lib
# rules, which rebuild on any covered source change).
define unity_tu
$(BUILD_ROOT)/$(1).c: $(2) packaging/ipad/build.mk
	@mkdir -p "$(BUILD_ROOT)"
	@printf '%s\n' $(2) | sed 's/.*/\#include "&"/' > "$$@"
endef

$(eval $(call unity_tu,unity_shared,$(SHARED_SRCS),))
$(eval $(call unity_tu,unity_jass,$(JASS_SRCS),))
$(eval $(call unity_tu,unity_sheet,$(SHEET_SRCS),))
$(eval $(call unity_tu,unity_renderer,$(RENDERER_SRCS),))
$(eval $(call unity_tu,unity_game,$(GAME_SRCS),))
$(eval $(call unity_tu,unity_menu,$(MENU_SRCS),))
$(eval $(call unity_tu,unity_app,$(APP_SRCS),))

FW_DEPS = $(SDL_MARKER) $(APP_ROOT)/settings packaging/ipad/build.mk

# -install_name @rpath/<fw>.framework/<fw>; siblings resolve via @loader_path/...
FW_LINK_BASE = -isysroot "$(SDK_PATH)" -arch $(ARCH) $(MIN_FLAG) -dynamiclib \
	-Wl,-install_name,@rpath/$(notdir $(@D))/$(notdir $@) -Wl,-rpath,@loader_path/..

$(FW_DIR)/shared.framework/shared: $(BUILD_ROOT)/unity_shared.c $(FW_DEPS)
	@mkdir -p "$(@D)"
	$(COMPILER) $(BASE_FLAGS) -c "$(BUILD_ROOT)/unity_shared.c" -o "$(BUILD_ROOT)/unity_shared.o"
	$(COMPILER) $(FW_LINK_BASE) "$(BUILD_ROOT)/unity_shared.o" \
		-force_load "$(SDL_LIB)" -lz -lm $(SDL_SYSTEM_LIBS) -o "$@"

$(FW_DIR)/jass.framework/jass: $(BUILD_ROOT)/unity_jass.c $(FW_DEPS) $(FW_DIR)/shared.framework/shared
	@mkdir -p "$(@D)"
	$(COMPILER) $(WC3_FLAGS) -c "$(BUILD_ROOT)/unity_jass.c" -o "$(BUILD_ROOT)/unity_jass.o"
	$(COMPILER) $(FW_LINK_BASE) "$(BUILD_ROOT)/unity_jass.o" \
		-F"$(FW_DIR)" -framework shared -lm -o "$@"

$(FW_DIR)/sheet.framework/sheet: $(BUILD_ROOT)/unity_sheet.c $(FW_DEPS)
	@mkdir -p "$(@D)"
	$(COMPILER) $(BASE_FLAGS) -c "$(BUILD_ROOT)/unity_sheet.c" -o "$(BUILD_ROOT)/unity_sheet.o"
	$(COMPILER) $(FW_LINK_BASE) "$(BUILD_ROOT)/unity_sheet.o" -o "$@"

$(FW_DIR)/renderer.framework/renderer: $(BUILD_ROOT)/unity_renderer.c $(FW_DEPS) $(FW_DIR)/shared.framework/shared $(FW_DIR)/sheet.framework/sheet
	@mkdir -p "$(@D)"
	$(COMPILER) $(WC3_FLAGS) -c "$(BUILD_ROOT)/unity_renderer.c" -o "$(BUILD_ROOT)/unity_renderer.o"
	$(COMPILER) $(FW_LINK_BASE) "$(BUILD_ROOT)/unity_renderer.o" \
		-F"$(FW_DIR)" -framework shared -framework sheet \
		-framework OpenGLES -lz -lm -o "$@"

$(FW_DIR)/game.framework/game: $(BUILD_ROOT)/unity_game.c $(FW_DEPS) $(FW_DIR)/shared.framework/shared $(FW_DIR)/sheet.framework/sheet $(FW_DIR)/jass.framework/jass
	@mkdir -p "$(@D)"
	$(COMPILER) $(WC3_FLAGS) $(FDF_FLAGS) -c "$(BUILD_ROOT)/unity_game.c" -o "$(BUILD_ROOT)/unity_game.o"
	$(COMPILER) $(FW_LINK_BASE) "$(BUILD_ROOT)/unity_game.o" \
		-F"$(FW_DIR)" -framework sheet -framework shared -framework jass \
		-lm -lz -o "$@"

$(FW_DIR)/menu.framework/menu: $(BUILD_ROOT)/unity_menu.c $(FW_DEPS) $(FW_DIR)/shared.framework/shared $(FW_DIR)/sheet.framework/sheet
	@mkdir -p "$(@D)"
	$(COMPILER) $(WC3_FLAGS) $(FDF_FLAGS) -c "$(BUILD_ROOT)/unity_menu.c" -o "$(BUILD_ROOT)/unity_menu.o"
	$(COMPILER) $(FW_LINK_BASE) "$(BUILD_ROOT)/unity_menu.o" \
		-F"$(FW_DIR)" -framework shared -framework sheet -lm -lz -o "$@"

frameworks: $(FW_BINS)

$(APP_ROOT)/app.o: $(BUILD_ROOT)/unity_app.c $(FW_DEPS)
	@mkdir -p "$(@D)"
	$(COMPILER) $(WC3_FLAGS) $(FDF_FLAGS) -DBZ_CLIENT_WORLD \
		-c "$(BUILD_ROOT)/unity_app.c" -o "$@"

$(BINARY): $(APP_ROOT)/app.o frameworks
	$(COMPILER) -isysroot "$(SDK_PATH)" -arch $(ARCH) $(MIN_FLAG) \
		"$(APP_ROOT)/app.o" "$(SDL_MAIN_LIB)" \
		-F"$(FW_DIR)" -framework shared -framework sheet -framework jass \
		-framework game -framework renderer -framework menu \
		-Wl,-rpath,@executable_path/Frameworks \
		-lz -lm $(SDL_SYSTEM_LIBS) -o "$@"

app: $(BINARY)
	python3 tools/ipad/bundle.py --root "$(CURDIR)" --target "$(BUNDLE)" \
		--binary "$<" --frameworks "$(FW_DIR)" --bundle-id "$(BUNDLE_ID)" \
		--sdk $(SDK) --sdk-version $(SDK_VERSION) --minimum $(IOS_MIN)
ifeq ($(SDK),iphonesimulator)
	python3 tools/ipad/sign.py --adhoc "$(BUNDLE)"
endif

run: app
	@test "$(SDK)" = iphonesimulator || { echo 'Use SDK=iphonesimulator'; exit 1; }
	python3 tools/ipad/run_simulator.py "$(BUNDLE)" $(if $(DEVICE),--device "$(DEVICE)")

deploy: app
	@test "$(SDK)" = iphoneos -a "$(ARCH)" = arm64 || { echo 'Deploy requires SDK=iphoneos ARCH=arm64'; exit 1; }
	@device="$(DEVICE)"; \
	if [ -z "$$device" ]; then \
		devices="$$(xcrun devicectl list devices)" || exit $$?; \
		device="$$(printf '%s\n' "$$devices" | awk '/iPad/ && !/simulated/ { \
			for (i = 1; i <= NF; i++) { \
				if ($$i !~ /^([[:xdigit:]]{8}-[[:xdigit:]]{16}|[[:xdigit:]]{8}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{12})$$/) continue; \
				state = i + 1; if ($$state == "(UDID)") state++; \
				if ($$state == "available" || $$state == "connected") print $$i; \
			} \
		}')"; \
		count="$$(printf '%s\n' "$$device" | sed '/^$$/d' | wc -l | tr -d ' ')"; \
		if [ "$$count" -ne 1 ]; then \
			echo "Expected exactly one available physical iPad; found $$count. Use make list-devices and set DEVICE=..." >&2; \
			exit 1; \
		fi; \
		echo "Auto-selected iPad $$device"; \
	fi; \
	python3 tools/ipad/sign.py "$(BUNDLE)" $(if $(TEAM),--team "$(TEAM)") $(if $(PROFILE),--profile "$(PROFILE)"); \
	xcrun devicectl device install app --device "$$device" "$(BUNDLE)"; \
	xcrun devicectl device process launch --device "$$device" --terminate-existing "$(BUNDLE_ID)"

mac: app
	@test "$(SDK)" = iphoneos -a "$(ARCH)" = arm64 || { echo 'Mac launch requires SDK=iphoneos ARCH=arm64'; exit 1; }
	python3 tools/ipad/sign.py "$(BUNDLE)" $(if $(TEAM),--team "$(TEAM)") $(if $(PROFILE),--profile "$(PROFILE)")
	python3 tools/ipad/wrap_mac.py "$(BUNDLE)" "$(abspath $(BUILD_DIR))/Warcraft3.app"
	open "$(abspath $(BUILD_DIR))/Warcraft3.app"

-include $(BUILD_ROOT)/unity_*.d $(APP_ROOT)/app.d
