ZIP_URL  := https://archive.org/download/warcraft-iii-installer-enus/Warcraft-III-1.29.2-enUS.zip
ZIP_FILE := Warcraft-III-1.29.2-enUS.zip
DATA_DIR := data

WC3DATA  := data/Warcraft\ III
DEMODATA := data/Warcraft3demo
MPQ      := $(WC3DATA)/War3.mpq
MAP      := Maps/Campaign/Human02.w3m

WC3_DIR := games/warcraft-3
WC3_JASS_DIR := $(WC3_DIR)/jass
WC3_SHEET_DIR := $(WC3_DIR)/sheet
WC3_TEST_DIR := $(WC3_DIR)/tests

WC3_CFLAGS := $(CFLAGS) -I$(WC3_DIR) -I$(WC3_DIR)/common -DWC3 -DUSE_FOGOFWAR -DBZ_GAME=\"warcraft-3\"
ifeq ($(WC3_FOW_PACKED_MASK),1)
WC3_CFLAGS += -DWC3_FOW_PACKED_MASK
endif
WC3_FDF_CFLAGS := $(WC3_CFLAGS) -DSTB_FDF_IMPLEMENTATION -DSTB_FDF_GLOBALS
WC3_COMMON_SRCS := $(filter-out $(WC3_DIR)/common/routing.c,$(shell find $(WC3_DIR)/common -name '*.c' 2>/dev/null | sort))
MENU_HEADERS := $(shell find $(WC3_DIR)/menu -name '*.h' | sort) client/menu.h

JASS_LIB     := $(LIB_DIR)/libjass$(LIB_EXT)
SHEET_LIB    := $(LIB_DIR)/libsheet$(LIB_EXT)
RENDERER_LIB := $(LIB_DIR)/librenderer$(LIB_EXT)
GAME_LIB     := $(LIB_DIR)/libgame$(LIB_EXT)
MENU_LIB       := $(LIB_DIR)/libmenu$(LIB_EXT)
BINARY       := $(BIN_DIR)/openwarcraft3$(EXE_EXT)
MPQ_TEST     := $(BIN_DIR)/test_mpq_compat$(EXE_EXT)

JASS_BIN := $(BIN_DIR)/jass$(EXE_EXT)

build: wc3-build
wc3-build: shared jass sheet renderer game menu openwarcraft3 tools jass-tool
jass-tool: $(JASS_BIN)
jass:        $(JASS_LIB)
sheet:       $(SHEET_LIB)
renderer:    $(RENDERER_LIB)
game:        $(GAME_LIB)
menu:          $(MENU_LIB)
openwarcraft3: $(BINARY)

run: $(BINARY) install-share
	$(BINARY) -data $(WC3DATA) -tft

run-roc: $(BINARY) install-share
	$(BINARY) -data $(WC3DATA)

run-demo: $(BINARY) install-share
	$(BINARY) -data $(DEMODATA)

run-map: $(BINARY) install-share
	$(BINARY) -data $(WC3DATA) +map "$(MAP)"

TRACE_FILE := build/profile-map.trace

profile-map: $(BINARY) xctraceprof
	xctrace record --template "Time Profiler" --time-limit 20s \
		--output $(TRACE_FILE) \
		--launch -- $(BINARY) -data $(WC3DATA) -tft +map "$(MAP)"
	xctrace export --input $(TRACE_FILE) \
		--xpath '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]' \
		> build/time-profile.xml
	$(BIN_DIR)/xctraceprof --window 3:20 --top 40 build/time-profile.xml

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
	@$(CC) $(CFLAGS) -DMPQ_TEST_API -o $@ $(WC3_TEST_DIR)/test_mpq_compat.c common/mpq.c -lm -lz

# jass.h includes g_local.h: stale edict/client offsets break player-local ESC cleanup after contract changes.
JASS_HEADERS := $(COMMON_HEADERS) $(CLIENT_HEADERS) $(shell find $(WC3_DIR)/game $(WC3_DIR)/common server shared -name '*.h')
$(eval $(call unity_lib_schema,$(JASS_LIB),$(SHARED_LIB) $(JASS_HEADERS) $(shell find $(WC3_JASS_DIR) -name '*.c' -o -name '*.h'),jass,$(WC3_JASS_DIR),,$(WC3_CFLAGS),,-lshared -lm))
$(eval $(call src_lib_schema,$(SHEET_LIB),$(WC3_SHEET_DIR)/parser.c $(WC3_SHEET_DIR)/sheet.c common/common.h,sheet,$(CFLAGS),$(WC3_SHEET_DIR)/parser.c $(WC3_SHEET_DIR)/sheet.c,))
$(eval $(call unity_lib_schema,$(RENDERER_LIB),$(RENDERER_BASE_DEPS) $(call CSRC,renderer $(WC3_DIR)/renderer),renderer,renderer $(WC3_DIR)/renderer,,$(WC3_CFLAGS),common/mpq.c,$(RENDERER_SHARED_LIBS)))
$(eval $(call unity_lib_schema,$(GAME_LIB),$(GAME_BASE_DEPS) $(JASS_LIB) $(SHEET_LIB) $(WORLD_CORE_SRCS) $(WC3_COMMON_SRCS) $(call CSRC,$(WC3_DIR)/game),game,$(WC3_DIR)/game $(WC3_DIR)/common,! -name 'world_w3.c' ! -name 'routing.c',$(WC3_FDF_CFLAGS),common/mpq.c,-lsheet -lshared -ljass $(LIBS) -lm -lz))
$(eval $(call unity_lib_schema,$(MENU_LIB),$(UI_BASE_DEPS) $(MENU_HEADERS) common/mpq.c common/mpq.h $(call CSRC,$(WC3_DIR)/menu),menu,$(WC3_DIR)/menu $(WC3_DIR)/common,! -name 'world_w3.c' ! -name 'routing.c',$(WC3_FDF_CFLAGS),common/mpq.c,-lshared -lsheet -lm -lz))
$(eval $(call app_schema,$(BINARY),$(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB) $(GAME_LIB) $(RENDERER_LIB) $(MENU_LIB) $(APP_SRCS) $(CLIENT_HEADERS) $(COMMON_HEADERS),openwarcraft3,$(WC3_FDF_CFLAGS),-lsheet -lshared -ljass -lgame -lrenderer -lmenu $(LIBS) -lz))

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

$(eval $(call unity_lib_schema,$(GAME_WC3_TEST_LIB),$(GAME_BASE_DEPS) $(JASS_LIB) $(SHEET_LIB) $(WORLD_CORE_SRCS) $(WC3_COMMON_SRCS) $(call CSRC,$(WC3_DIR)/game),game-wc3-test,$(WC3_DIR)/game $(WC3_DIR)/common,! -name 'world_w3.c' ! -name 'routing.c',$(WC3_FDF_CFLAGS) -DBZ_TESTS,common/mpq.c,-lsheet -lshared -ljass $(LIBS) -lm -lz))
$(eval $(call app_schema,$(WC3_TEST_BINARY),$(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB) $(GAME_WC3_TEST_LIB) $(RENDERER_LIB) $(MENU_LIB) $(APP_SRCS) $(CLIENT_HEADERS) $(COMMON_HEADERS),openwarcraft3-tests,$(WC3_FDF_CFLAGS) -DBZ_TESTS,-lsheet -lshared -ljass -lgame-wc3-test -lrenderer -lmenu $(LIBS) -lz))

openwarcraft3-tests: $(WC3_TEST_BINARY)

WC3_PATTERN ?= *
test-wc3-engine: $(WC3_TEST_BINARY) test-assets | $(TEST_JUNIT_DIR)
	TEST_JUNIT="$(TEST_JUNIT_DIR)/test-wc3-engine-classic.xml" TEST_JUNIT_SUITE="test-wc3-engine-classic" $(WC3_TEST_BINARY) -data $(TESTS_DIR) +dedicated 1 +test '$(WC3_PATTERN)'
	TEST_JUNIT="$(TEST_JUNIT_DIR)/test-wc3-engine-tft.xml" TEST_JUNIT_SUITE="test-wc3-engine-tft" $(WC3_TEST_BINARY) -data $(TESTS_DIR) -tft +dedicated 1 +test '$(WC3_PATTERN)'

.PHONY: test-jass-build
test-jass-build: $(JASS_LIB)
	@sh tests/test_jass_build.sh $(JASS_LIB)

# ---------------------------------------------------------------------------
# Standalone test binaries — tests that don't need the full game module.
# In-engine tests live in games/warcraft-3/game/tests/t_*.c and run via
# `+dedicated 1 +test 'wc3_*'` (see test-wc3-engine target).
# ---------------------------------------------------------------------------

# Common flags for standalone test binaries.
TEST_CFLAGS := $(WC3_CFLAGS) -DTOOL_COMMON_NO_MPQ -Itests -I$(WC3_TEST_DIR) -Ishared -Ishared/types -Iserver -Icommon -Iclient
TEST_MENU_CFLAGS := $(TEST_CFLAGS) -I$(WC3_DIR)/menu

TEST_UI_SRCS := \
	$(WC3_TEST_DIR)/test_menu_fdf.c \
	$(WC3_TEST_DIR)/test_menu_oracle.c \
	$(WC3_TEST_DIR)/stb_fdf_impl.c \
	tests/test_tool_common.c

TEST_JOBS ?= 16

 test: test-assets $(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB) | $(BIN_DIR) $(TEST_JUNIT_DIR)
	@rm -f $(TEST_JUNIT_DIR)/*.xml
	@$(CC) $(TEST_CFLAGS) -DBZ_TESTS -o $(BIN_DIR)/test_openwarcraft3$(EXE_EXT) \
		tests/test_runner.c tests/test_compat.c tests/test_net.c tests/test_tool_common.c \
		$(WC3_TEST_DIR)/test_client_stubs.c $(WC3_TEST_DIR)/test_control_groups.c $(WC3_TEST_DIR)/test_keys.c \
			common/net.c common/msg.c client/cl_control_groups.c client/cl_parse.c client/cl_configstrings.c client/cl_scrn.c client/cl_minimap.c client/cl_layout.c client/cl_window.c \
		$(RPATH) $(LDFLAGS) -lsheet -lshared -lm
	@TEST_JUNIT="$(TEST_JUNIT_DIR)/test-core.xml" TEST_JUNIT_SUITE="test-core" $(BIN_DIR)/test_openwarcraft3$(EXE_EXT)
	@# Run independent suites concurrently while preserving recursive-make failure propagation.
	@$(MAKE) -j$(TEST_JOBS) test-commands test-jass-build test-galaxy test-server-net \
		test-renderer-model test-renderer-shadows test-sc2 test-wow-appearance \
		test-wow-engine test-wow-game test-wow-entities test-wow-abilities test-wow-menu \
		test-wow-wmo test-menu test-wc3-engine

$(eval $(call test_schema,test-commands,test-assets $(SHARED_LIB) $(SHEET_LIB),$(TEST_CFLAGS),$(BIN_DIR)/test_commands$(EXE_EXT),tests/test_runner.c $(WC3_TEST_DIR)/test_commands.c client/cl_screenshot.c common/common.c common/cmd.c common/cvar.c common/msg.c common/net.c common/mpq.c,-lsheet -lshared -lm -lz $(NET_LIBS),))
$(eval $(call test_schema,test-server-net,test-assets $(SHARED_LIB) $(SHEET_LIB),$(TEST_CFLAGS),$(BIN_DIR)/test_server_net$(EXE_EXT),tests/test_runner.c $(WC3_TEST_DIR)/test_server_net.c $(WC3_TEST_DIR)/test_client_stubs.c server/sv_init.c server/sv_lan.c server/sv_main.c server/sv_lobby.c server/sv_send.c server/sv_ents.c server/sv_parse.c common/net.c common/msg.c,-lsheet -lshared -lm $(NET_LIBS),))
$(eval $(call test_schema,test-renderer-model,$(SHARED_LIB),$(TEST_CFLAGS) -Wno-unused-function,$(BIN_DIR)/test_renderer_model$(EXE_EXT),tests/test_runner.c tests/test_renderer_model.c renderer/r_model.c $(WC3_DIR)/renderer/mdx/r_mdx_anim.c $(WC3_DIR)/renderer/mdx/r_mdx_interpolation.c $(WC3_DIR)/renderer/mdx/r_mdx_buffer.c $(WC3_DIR)/renderer/mdx/r_mdx_light.c,-lshared -lm $(LIBS),))
$(eval $(call test_schema,test-renderer-shadows,$(SHARED_LIB),$(TEST_CFLAGS) -Wno-unused-function -DUSE_SHADOWMAPS,$(BIN_DIR)/test_renderer_shadows$(EXE_EXT),tests/test_runner.c tests/test_renderer_model.c renderer/r_model.c $(WC3_DIR)/renderer/mdx/r_mdx_anim.c $(WC3_DIR)/renderer/mdx/r_mdx_interpolation.c $(WC3_DIR)/renderer/mdx/r_mdx_buffer.c $(WC3_DIR)/renderer/mdx/r_mdx_light.c,-lshared -lm $(LIBS),))
$(eval $(call test_schema,test-galaxy,$(SHARED_LIB) $(JASS_LIB),$(TEST_CFLAGS) -DBZ_TESTS,$(BIN_DIR)/test_galaxy$(EXE_EXT),tests/test_runner.c tests/test_galaxy.c games/starcraft-2/game/galaxy/galaxy_host.c,-lshared -ljass -lm,))
$(eval $(call test_schema,test-menu,test-assets $(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB),$(TEST_MENU_CFLAGS),$(BIN_DIR)/test_openwarcraft3_ui$(EXE_EXT),tests/test_runner.c $(TEST_UI_SRCS) common/mpq.c $(call CSRC,$(WC3_DIR)/menu),-lsheet -lshared -ljass -lm -lz,))

test-mpq-compat: mpqtool $(MPQ_TEST)
	@$(MPQ_TEST) -mpq=$(MPQ)

# ---------------------------------------------------------------------------
# test-assets — generate fixture resources and pack them into build/tests/tests.mpq
#
# Generated files (never committed):
#   build/tests/resources/TestUI/Textures/*.blp  — via blpgen
#   build/tests/resources/TestUI/Models/*.mdx    — via mdxgen
# Source-controlled fixtures ($(WC3_TEST_DIR)/resources-src/**/*) are packed directly.
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
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat Units/ItemData.slk | \
		grep -q "Test Attack Item" && echo "  cat item SLK OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat Units/AbilityData.slk | \
		grep -q "AInv" && echo "  cat ability SLK OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat Units/AbilityBuffData.slk | \
		grep -q "Biml" && echo "  cat ability buff SLK OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat Units/UpgradeData.slk | \
		grep -q "Rhme" && echo "  cat upgrade SLK OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat Units/ItemFunc.txt | \
		grep -q "spro" && echo "  cat item UI OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat UI/war3skins.txt | \
		grep -q "ConsoleInventoryCoverTexture" && echo "  cat skin UI OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat UI/SoundInfo/UISounds.slk | \
		grep -q "InterfaceError" && echo "  cat UI sound SLK OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat Units/UnitBalance.slk | grep -q "hpea" && echo "  cat unit SLK OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(TESTS_MPQ) cat Scripts/common.j | \
		grep -q "playergameresult" && echo "  cat common.j OK"
	@echo "[test-assets] done — $(TESTS_MPQ)"

$(TESTS_DIR):
	@mkdir -p $@

download: $(ZIP_FILE)
	mkdir -p $(DATA_DIR)
	unzip -o $(ZIP_FILE) -d $(DATA_DIR)

$(ZIP_FILE):
	curl -L -o $(ZIP_FILE) $(ZIP_URL)

WC3_PHONY := wc3-build jass-tool jass sheet renderer game menu openwarcraft3 run run-demo run-map test \
	test-commands test-server-net test-renderer-model test-renderer-shadows test-galaxy test-menu test-mpq-compat test-assets test-render-golden \
	update-render-golden openwarcraft3-tests test-wc3-engine download
