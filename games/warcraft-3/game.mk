ZIP_URL  := https://archive.org/download/warcraft-iii-installer-enus/Warcraft-III-1.29.2-enUS.zip
ZIP_FILE := Warcraft-III-1.29.2-enUS.zip
DATA_DIR := data

WC3DATA  := data/Warcraft\ III
DEMODATA := data/Warcraft3demo
MPQ      := $(WC3DATA)/War3.mpq
MAP      := Maps/Campaign/Human02.w3m
UI_CMD   := menu_main

WC3_DIR := games/warcraft-3
WC3_JASS_DIR := $(WC3_DIR)/jass
WC3_SHEET_DIR := $(WC3_DIR)/sheet
WC3_TEST_DIR := $(WC3_DIR)/tests

WC3_CFLAGS := $(CFLAGS) -I$(WC3_DIR) -I$(WC3_DIR)/common -DUSE_FOGOFWAR
WC3_FDF_CFLAGS := $(WC3_CFLAGS) -DSTB_FDF_IMPLEMENTATION -DSTB_FDF_GLOBALS
WC3_COMMON_SRCS := $(shell find $(WC3_DIR)/common -name '*.c' 2>/dev/null | sort)
WC3_UI_HEADERS := $(shell find $(WC3_DIR)/ui -name '*.h' | sort) client/ui.h

JASS_LIB     := $(LIB_DIR)/libjass$(LIB_EXT)
SHEET_LIB    := $(LIB_DIR)/libsheet$(LIB_EXT)
RENDERER_LIB := $(LIB_DIR)/librenderer$(LIB_EXT)
GAME_LIB     := $(LIB_DIR)/libgame$(LIB_EXT)
UI_LIB       := $(LIB_DIR)/libui$(LIB_EXT)
BINARY       := $(BIN_DIR)/openwarcraft3$(EXE_EXT)
MPQ_TEST     := $(BIN_DIR)/test_mpq_compat$(EXE_EXT)

JASS_BIN := $(BIN_DIR)/jass$(EXE_EXT)

build: wc3-build
wc3-build: shared jass sheet renderer game ui openwarcraft3 tools jass-tool
jass-tool: $(JASS_BIN)
jass:        $(JASS_LIB)
sheet:       $(SHEET_LIB)
renderer:    $(RENDERER_LIB)
game:        $(GAME_LIB)
ui:          $(UI_LIB)
openwarcraft3: $(BINARY)

run: $(BINARY)
	$(BINARY) -data $(WC3DATA) -tft

run-roc: $(BINARY)
	$(BINARY) -data $(WC3DATA)

run-demo: $(BINARY)
	$(BINARY) -data $(DEMODATA)

run-map: $(BINARY)
	$(BINARY) -data $(WC3DATA) +map "$(MAP)"

run-ui-text: $(BINARY)
	$(BINARY) -data $(WC3DATA) +r_module stdout +com_frame_limit 1 +$(UI_CMD)

# Golden-image render regression test (deterministic MDX renders vs committed
# references). Requires a display/GL, so it is opt-in and NOT part of `make test`
# (CI is headless). Run locally after renderer changes.
test-render-golden: mdxtool imgdiff
	@tools/parity/render_golden.sh --data "$(subst \,,$(WC3DATA))"

update-render-golden: mdxtool imgdiff
	@tools/parity/render_golden.sh --data "$(subst \,,$(WC3DATA))" --update

# jass — standalone JASS interpreter (no renderer/game/SDL2 needed)
$(JASS_BIN): tools/jass.c $(TOOL_DEPS) | $(BIN_DIR) $(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB)
	@echo "[jass-tool]"
	@$(CC) $(WC3_CFLAGS) -DTOOL_COMMON_NO_MPQ -o $@ tools/jass.c \
		$(RPATH) $(LDFLAGS) -lsheet -lshared -ljass -lm

$(MPQ_TEST): $(WC3_TEST_DIR)/test_mpq_compat.c common/mpq.c common/mpq.h | $(BIN_DIR)
	@echo "[mpq-compat-test]"
	@$(CC) $(CFLAGS) -o $@ $(WC3_TEST_DIR)/test_mpq_compat.c common/mpq.c -lm -lz

$(eval $(call unity_lib_schema,$(JASS_LIB),$(SHARED_LIB) $(shell find $(WC3_JASS_DIR) -name '*.c' -o -name '*.h'),jass,$(WC3_JASS_DIR),,$(WC3_CFLAGS),,-lshared -lm))
$(eval $(call src_lib_schema,$(SHEET_LIB),$(WC3_SHEET_DIR)/parser.c $(WC3_SHEET_DIR)/sheet.c common/common.h,sheet,$(CFLAGS),$(WC3_SHEET_DIR)/parser.c $(WC3_SHEET_DIR)/sheet.c,))
$(eval $(call unity_lib_schema,$(RENDERER_LIB),$(RENDERER_BASE_DEPS) $(call CSRC,renderer $(WC3_DIR)/renderer),renderer,renderer $(WC3_DIR)/renderer,,$(WC3_CFLAGS),common/mpq.c,$(RENDERER_SHARED_LIBS)))
$(eval $(call unity_lib_schema,$(GAME_LIB),$(GAME_BASE_DEPS) $(JASS_LIB) $(SHEET_LIB) $(WORLD_CORE_SRCS) $(WC3_COMMON_SRCS) $(call CSRC,$(WC3_DIR)/game),game,$(WC3_DIR)/game $(WC3_DIR)/common,! -name 'world_w3.c',$(WC3_FDF_CFLAGS),common/mpq.c,-lsheet -lshared -ljass $(LIBS) -lm -lz))
$(eval $(call unity_lib_schema,$(UI_LIB),$(UI_BASE_DEPS) $(WC3_UI_HEADERS) common/mpq.c common/mpq.h $(call CSRC,$(WC3_DIR)/ui),ui,$(WC3_DIR)/ui $(WC3_DIR)/common,! -name 'world_w3.c',$(WC3_FDF_CFLAGS),common/mpq.c,-lshared -lsheet -lm -lz))
$(eval $(call app_schema,$(BINARY),$(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB) $(GAME_LIB) $(RENDERER_LIB) $(UI_LIB) $(APP_SRCS) $(CLIENT_HEADERS) $(COMMON_HEADERS),openwarcraft3,$(WC3_FDF_CFLAGS),-lsheet -lshared -ljass -lgame -lrenderer -lui $(LIBS) -lz))

# ---------------------------------------------------------------------------
# In-engine tests (see CONTRIBUTING.md)
#
# The game module is rebuilt with -DBZ_TESTS so that TEST() blocks under
# games/warcraft-3/game/tests/ are compiled in and self-register.  A dedicated
# openwarcraft3-tests binary links that variant and boots headless:
#   openwarcraft3-tests -data <wc3data> +dedicated 1 +test '<pattern>'
# The test registry lives in libshared, so the game module's constructors and
# the engine's `test` command share one instance.  Production `make` never
# defines BZ_TESTS, so the shipped game module contains no test code.
# ---------------------------------------------------------------------------
GAME_WC3_TEST_LIB := $(LIB_DIR)/libgame-wc3-test$(LIB_EXT)
WC3_TEST_BINARY   := $(BIN_DIR)/openwarcraft3-tests$(EXE_EXT)

$(eval $(call unity_lib_schema,$(GAME_WC3_TEST_LIB),$(GAME_BASE_DEPS) $(JASS_LIB) $(SHEET_LIB) $(WORLD_CORE_SRCS) $(WC3_COMMON_SRCS) $(call CSRC,$(WC3_DIR)/game),game-wc3-test,$(WC3_DIR)/game $(WC3_DIR)/common,! -name 'world_w3.c',$(WC3_FDF_CFLAGS) -DBZ_TESTS,common/mpq.c,-lsheet -lshared -ljass $(LIBS) -lm -lz))
$(eval $(call app_schema,$(WC3_TEST_BINARY),$(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB) $(GAME_WC3_TEST_LIB) $(RENDERER_LIB) $(UI_LIB) $(APP_SRCS) $(CLIENT_HEADERS) $(COMMON_HEADERS),openwarcraft3-tests,$(WC3_FDF_CFLAGS) -DBZ_TESTS,-lsheet -lshared -ljass -lgame-wc3-test -lrenderer -lui $(LIBS) -lz))

openwarcraft3-tests: $(WC3_TEST_BINARY)

WC3_ENGINE_TEST_DIR    := $(TESTS_DIR)/wc3-engine-data
WC3_ENGINE_TEST_MPQ    := $(WC3_ENGINE_TEST_DIR)/test-wc3.mpq
WC3_ENGINE_FIXTURE_DIR := tests/wc3-engine-data

WC3_PATTERN ?= *
test-wc3-engine: $(WC3_TEST_BINARY) | test-wc3-engine-assets
	$(WC3_TEST_BINARY) -data $(WC3_ENGINE_TEST_DIR) -tft +dedicated 1 +test '$(WC3_PATTERN)'

# ---------------------------------------------------------------------------
# Standalone test binaries — tests that don't need the full game module.
# In-engine tests live in games/warcraft-3/game/tests/t_*.c and run via
# `+dedicated 1 +test 'wc3_*'` (see test-wc3-engine target).
# ---------------------------------------------------------------------------

# Common flags for standalone test binaries.
TEST_CFLAGS := $(WC3_CFLAGS) -DTOOL_COMMON_NO_MPQ -Itests -I$(WC3_TEST_DIR) -Ishared -Ishared/types -Iserver -Icommon -Iclient
TEST_UI_CFLAGS := $(TEST_CFLAGS) -I$(WC3_DIR)/ui

TEST_UI_SRCS := \
	$(WC3_TEST_DIR)/test_ui_fdf.c \
	$(WC3_TEST_DIR)/test_ui_oracle.c \
	$(WC3_TEST_DIR)/stb_fdf_impl.c \
	tests/test_tool_common.c

 test: test-assets $(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB) | $(BIN_DIR)
	@$(CC) $(TEST_CFLAGS) -DBZ_TESTS -o $(BIN_DIR)/test_openwarcraft3$(EXE_EXT) \
		tests/test_runner.c tests/test_net.c tests/test_tool_common.c \
		$(WC3_TEST_DIR)/test_client_stubs.c \
		common/net.c common/msg.c client/cl_parse.c client/cl_scrn.c client/cl_layout.c \
		$(RPATH) $(LDFLAGS) -lsheet -lshared -lm
	@$(BIN_DIR)/test_openwarcraft3$(EXE_EXT)
	@# Propagate sub-suite failures; the old ignored-error prefix made a red suite report success.
	@$(MAKE) test-commands
	@$(MAKE) test-server-net
	@$(MAKE) test-renderer-model
	@$(MAKE) test-sc2
	@$(MAKE) test-wow-appearance
	@$(MAKE) test-wow-engine
	@$(MAKE) test-wow-game
	@$(MAKE) test-wow-entities
	@$(MAKE) test-wow-abilities
	@$(MAKE) test-wow-ui
	@$(MAKE) test-wow-wmo
	@$(MAKE) test-ui
	@$(MAKE) test-wc3-engine

$(eval $(call test_schema,test-commands,test-assets $(SHARED_LIB) $(SHEET_LIB),$(TEST_CFLAGS),$(BIN_DIR)/test_commands$(EXE_EXT),tests/test_runner.c $(WC3_TEST_DIR)/test_commands.c client/cl_screenshot.c common/common.c common/cmd.c common/cvar.c common/msg.c common/net.c common/mpq.c,-lsheet -lshared -lm -lz $(NET_LIBS),))
$(eval $(call test_schema,test-server-net,test-assets $(SHARED_LIB) $(SHEET_LIB),$(TEST_CFLAGS),$(BIN_DIR)/test_server_net$(EXE_EXT),tests/test_runner.c $(WC3_TEST_DIR)/test_server_net.c $(WC3_TEST_DIR)/test_client_stubs.c server/sv_init.c server/sv_lan.c server/sv_main.c server/sv_lobby.c server/sv_send.c common/net.c common/msg.c,-lsheet -lshared -lm $(NET_LIBS),))
$(eval $(call test_schema,test-renderer-model,$(SHARED_LIB),$(TEST_CFLAGS) -Wno-unused-function,$(BIN_DIR)/test_renderer_model$(EXE_EXT),tests/test_runner.c tests/test_renderer_model.c renderer/r_model.c renderer/r_texture.c,-lshared -lm $(LIBS),))
$(eval $(call test_schema,test-ui,test-assets $(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB),$(TEST_UI_CFLAGS),$(BIN_DIR)/test_openwarcraft3_ui$(EXE_EXT),tests/test_runner.c $(TEST_UI_SRCS) common/mpq.c $(call CSRC,$(WC3_DIR)/ui),-lsheet -lshared -ljass -lm -lz,))

test-mpq-compat: mpqtool $(MPQ_TEST)
	@$(MPQ_TEST) -mpq=$(MPQ)

# ---------------------------------------------------------------------------
# test-assets — generate fixture resources and pack them into build/tests/tests.mpq
#
# Generated files (never committed):
#   build/tests/resources/TestUI/Textures/*.blp  — via blpgen
#   build/tests/resources/TestUI/Models/*.mdx    — via mdxgen
# Source-controlled FDF fixtures ($(WC3_TEST_DIR)/resources-src/**/*.fdf) are packed directly.
# The archive is recreated from scratch on every run (deterministic).
# ---------------------------------------------------------------------------
TESTS_DIR     := build/tests
TESTS_MPQ     := $(TESTS_DIR)/tests.mpq
TESTS_RES_DIR := $(TESTS_DIR)/resources
TESTS_SRC_DIR := $(WC3_TEST_DIR)/resources-src

test-assets: blpgen mdxgen mpqtool mdxtool | $(TESTS_DIR)
	@echo "[test-assets] generating textures"
	@mkdir -p $(TESTS_RES_DIR)/TestUI/Textures
	@for tex in \
		"solid 1 1 ffffffff $(TESTS_RES_DIR)/TestUI/Textures/solid_white.blp" \
		"checker 8 8 2 $(TESTS_RES_DIR)/TestUI/Textures/checker_8x8.blp" \
		"alpha_ring 16 16 $(TESTS_RES_DIR)/TestUI/Textures/alpha_ring_16x16.blp" \
		"panel_border 32 32 2 $(TESTS_RES_DIR)/TestUI/Textures/panel_border_32x32.blp" \
		"paletted 8 8 2 $(TESTS_RES_DIR)/TestUI/Textures/paletted_checker_8x8.blp"; do \
		$(BIN_DIR)/blpgen$(EXE_EXT) $$tex; \
	done
	@echo "[test-assets] generating models"
	@mkdir -p $(TESTS_RES_DIR)/TestUI/Models
	@for model in \
		"quad_sprite TestUI/Textures/checker_8x8.blp $(TESTS_RES_DIR)/TestUI/Models/quad_sprite.mdx" \
		"panel_sprite TestUI/Textures/solid_white.blp $(TESTS_RES_DIR)/TestUI/Models/panel_sprite.mdx" \
		"ui_panel TestUI/Textures/solid_white.blp $(TESTS_RES_DIR)/TestUI/Models/ui_panel.mdx" \
		"anim_pulse TestUI/Textures/alpha_ring_16x16.blp $(TESTS_RES_DIR)/TestUI/Models/anim_pulse.mdx"; do \
		$(BIN_DIR)/mdxgen$(EXE_EXT) $$model; \
	done
	@echo "[test-assets] packing tests.mpq"
	@set --; \
	for f in $$(find $(TESTS_RES_DIR) -type f | sort); do \
		rel=$${f#$(TESTS_RES_DIR)/}; set -- "$$@" "$$f" "$$rel"; \
	done; \
	for f in $$(find $(TESTS_SRC_DIR) -type f | sort); do \
		rel=$${f#$(TESTS_SRC_DIR)/}; arc=$$rel; \
		case "$$rel" in TestUI/FrameDef/*|TestUI/CampaignStrings*) arc=UI/$${rel#TestUI/};; esac; \
		set -- "$$@" "$$f" "$$arc"; \
	done; \
	$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) pack "$$@"
	@echo "[test-assets] verifying archive"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) ls | grep -q "TestUI/" && echo "  ls OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat Maps/Campaign/Human02.w3m | grep -q "Human02" && echo "  cat map OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat TestUI/Frames/basic_layout.fdf | grep -q "TestRootFrame" && echo "  cat FDF OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat TestUI/Textures/solid_white.blp | head -c4 | grep -q "BLP2" && echo "  cat BLP OK"
	@echo "[test-assets] done — $(TESTS_MPQ)"

$(TESTS_DIR):
	@mkdir -p $@

download: $(ZIP_FILE)
	mkdir -p $(DATA_DIR)
	unzip -o $(ZIP_FILE) -d $(DATA_DIR)

$(ZIP_FILE):
	curl -L -o $(ZIP_FILE) $(ZIP_URL)

# ---------------------------------------------------------------------------
# test-wc3-engine-assets — pack minimal SLK fixture for in-engine WC3 tests
# ---------------------------------------------------------------------------
test-wc3-engine-assets: mpqtool | $(TESTS_DIR)
	@echo "[test-wc3-engine-assets] packing test-wc3.mpq"
	@mkdir -p $(WC3_ENGINE_TEST_DIR)
	@set --; \
	for f in $$(find $(WC3_ENGINE_FIXTURE_DIR) -type f | sort); do \
		rel=$${f#$(WC3_ENGINE_FIXTURE_DIR)/}; set -- "$$@" "$$f" "$$rel"; \
	done; \
	$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WC3_ENGINE_TEST_MPQ) pack "$$@"
	@echo "[test-wc3-engine-assets] verifying archive"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WC3_ENGINE_TEST_MPQ) cat Units/UnitBalance.slk | grep -q "hpea" && echo "  cat SLK OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WC3_ENGINE_TEST_MPQ) cat Scripts/common.j | grep -q "playergameresult" && echo "  cat common.j OK"

WC3_PHONY := wc3-build jass-tool jass sheet renderer game ui openwarcraft3 run run-demo run-map run-ui-text test test-commands test-server-net test-renderer-model test-ui test-mpq-compat test-assets test-render-golden update-render-golden openwarcraft3-tests test-wc3-engine test-wc3-engine-assets download
