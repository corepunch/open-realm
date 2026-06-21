WC3DATA  := data/Warcraft\ III
DEMODATA := data/Warcraft3demo
MPQ      := $(WC3DATA)/War3.mpq
MAP      := Maps/Campaign/Human02.w3m
UI_CMD   := menu_main

WC3_DIR := games/warcraft-3
WC3_JASS_DIR := $(WC3_DIR)/jass
WC3_SHEET_DIR := $(WC3_DIR)/sheet
WC3_TEST_DIR := $(WC3_DIR)/tests

WC3_CFLAGS := $(CFLAGS) -I$(WC3_DIR)
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
	$(BINARY) -data $(WC3DATA)

run-demo: $(BINARY)
	$(BINARY) -data $(DEMODATA)

run-map: $(BINARY)
	$(BINARY) -data $(WC3DATA) +map "$(MAP)"

run-ui-text: $(BINARY)
	$(BINARY) -data $(WC3DATA) +r_module stdout +com_frame_limit 1 +$(UI_CMD)

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
$(eval $(call unity_lib_schema,$(GAME_LIB),$(GAME_BASE_DEPS) $(JASS_LIB) $(SHEET_LIB) $(WORLD_CORE_SRCS) $(WC3_COMMON_SRCS) $(call CSRC,$(WC3_DIR)/game),game,$(WC3_DIR)/game,,$(WC3_CFLAGS),common/mpq.c,-lsheet -lshared -ljass $(LIBS) -lm -lz))
$(eval $(call unity_lib_schema,$(UI_LIB),$(UI_BASE_DEPS) $(WC3_UI_HEADERS) $(call CSRC,$(WC3_DIR)/ui),ui,$(WC3_DIR)/ui,,$(WC3_CFLAGS),,-lshared -lm))
$(eval $(call app_schema,$(BINARY),$(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB) $(GAME_LIB) $(RENDERER_LIB) $(UI_LIB) $(APP_SRCS) $(CLIENT_HEADERS) $(COMMON_HEADERS),openwarcraft3,$(WC3_CFLAGS),-lsheet -lshared -ljass -lgame -lrenderer -lui $(LIBS) -lz))

# ---------------------------------------------------------------------------
# Test target — builds and runs the Warcraft III unit test binary.
#
# The test binary compiles only the game modules needed by the tests
# (no renderer, no archive backend, no SDL2) together with the shared sources.
# Global game state and gi function-pointers are provided by the test
# harness ($(WC3_DIR)/tests/test_harness.c) rather than by games/warcraft-3/game/g_main.c.
# ---------------------------------------------------------------------------
TEST_GAME_SRCS := \
	common/routing.c \
	$(WC3_DIR)/game/g_ai.c \
	$(WC3_DIR)/game/g_events.c \
	$(WC3_DIR)/game/g_fow.c \
	$(WC3_DIR)/game/g_metadata.c \
	$(WC3_DIR)/game/g_model.c \
	$(WC3_DIR)/game/g_monster.c \
	$(WC3_DIR)/game/g_pathing.c \
	$(WC3_DIR)/game/g_phys.c \
	$(WC3_DIR)/game/g_utils.c \
	$(WC3_DIR)/game/m_unit.c \
	$(WC3_DIR)/game/skills/s_attack.c \
	$(WC3_DIR)/game/skills/s_area_spell.c \
	$(WC3_DIR)/game/skills/s_move.c \
	$(WC3_DIR)/game/skills/s_item.c \
	$(WC3_DIR)/game/skills/s_skills.c \
	$(WC3_DIR)/game/skills/s_spell.c \
	$(WC3_DIR)/game/skills/s_stop.c \
	$(WC3_DIR)/game/skills/s_summon.c \
	$(WC3_DIR)/game/skills/s_thunderbolt.c \
	$(WC3_DIR)/game/skills/s_train.c \
	$(WC3_DIR)/game/skills/s_utility_abilities.c \
	$(WC3_DIR)/game/skills/s_ability_stubs.c \
	client/cl_layout.c \
	client/cl_parse.c \
	client/cl_scrn.c \
	client/cl_unit_ui.c \
	server/sv_init.c \
	server/sv_lan.c \
	server/sv_lobby.c \
	server/sv_send.c \
	server/sv_main.c \
	common/net.c \
	common/msg.c

TEST_SRCS := \
	$(shell find $(WC3_TEST_DIR) -maxdepth 1 -name 'test_*.c' \
	! -name 'test_commands.c' \
	! -name 'test_commands_main.c' \
	! -name 'test_jass_main.c' \
	! -name 'test_main_ui.c' \
	! -name 'test_mpq_compat.c' \
	! -name 'test_ui_*.c' | sort) \
	tests/test_net.c \
	tests/test_tool_common.c

TEST_CFLAGS := $(WC3_CFLAGS) -DTOOL_COMMON_NO_MPQ -Itests -I$(WC3_TEST_DIR) -Ishared/types -I$(WC3_DIR)/game -Iserver -Icommon -Iclient -I$(WC3_DIR)/game/skills
TEST_UI_CFLAGS := $(TEST_CFLAGS)

TEST_UI_SRCS := \
	$(WC3_TEST_DIR)/test_main_ui.c \
	$(WC3_TEST_DIR)/test_harness.c \
	$(WC3_TEST_DIR)/test_client_stubs.c \
	$(WC3_TEST_DIR)/test_server_net.c \
	$(WC3_TEST_DIR)/test_jass.c \
	tests/test_tool_common.c \
	$(shell find $(WC3_TEST_DIR) -maxdepth 1 -name 'test_ui_*.c' \
		! -name 'test_ui_e2e.c' \
		! -name 'test_ui_oracle.c' \
		! -name 'test_ui_serialize.c' | sort)

test: test-assets $(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB) | $(BIN_DIR)
	@$(CC) $(TEST_CFLAGS) -o $(BIN_DIR)/test_openwarcraft3$(EXE_EXT) \
		$(TEST_SRCS) $(TEST_GAME_SRCS) \
		$(RPATH) $(LDFLAGS) -lsheet -lshared -ljass -lm
	@$(BIN_DIR)/test_openwarcraft3$(EXE_EXT)
	@$(MAKE) test-commands
	@$(MAKE) test-sc2
	@$(MAKE) test-wow-appearance
	@$(MAKE) test-wow-combat
	@$(MAKE) test-wow-game
	@$(MAKE) test-wow-ui
	@$(MAKE) test-ui

$(eval $(call test_schema,test-commands,test-assets $(SHARED_LIB) $(SHEET_LIB),$(TEST_CFLAGS),$(BIN_DIR)/test_commands$(EXE_EXT),$(WC3_TEST_DIR)/test_commands_main.c $(WC3_TEST_DIR)/test_commands.c common/common.c common/cmd.c common/cvar.c common/msg.c common/net.c common/mpq.c,-lsheet -lshared -lm -lz,))
$(eval $(call test_schema,test-jass,$(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB),$(TEST_CFLAGS),$(BIN_DIR)/test_jass$(EXE_EXT),$(WC3_TEST_DIR)/test_jass_main.c $(WC3_TEST_DIR)/test_jass.c $(WC3_TEST_DIR)/test_harness.c $(WC3_TEST_DIR)/test_client_stubs.c $(WC3_DIR)/game/g_metadata.c common/msg.c,-lsheet -lshared -ljass -lm,))
$(eval $(call test_schema,test-ui,test-assets $(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB),$(TEST_UI_CFLAGS),$(BIN_DIR)/test_openwarcraft3_ui$(EXE_EXT),$(TEST_UI_SRCS) $(TEST_GAME_SRCS) common/mpq.c $(call CSRC,$(WC3_DIR)/ui),-lsheet -lshared -ljass -lm -lz,))

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

WC3_PHONY := wc3-build jass-tool jass sheet renderer game ui openwarcraft3 run run-demo run-map run-ui-text test test-jass test-commands test-ui test-mpq-compat test-assets
