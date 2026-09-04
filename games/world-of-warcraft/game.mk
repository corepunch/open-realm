WOW_DIR              := games/world-of-warcraft
WOW_TEST_DIR         := $(WOW_DIR)/tests
WOW_DATA_DIR         := data/world-of-warcraft
WOW_GENERATED_DIR    := build/generated
WOW_GENERATOR        := $(WOW_DIR)/serverdata/gen_serverdata_c.py
WOW_ISO_DIR          ?= $(ISO_DIR)
WOW_ISO_EXTRACT_DIR  ?= build/wow-install
WOW_KEEP_EXTRACT     ?= 0
WOW_INSTALL_DATA_DIR ?= $(WOW_DATA_DIR)
WOW_INSTALL_MPQS     := base.MPQ dbc.MPQ fonts.MPQ interface.MPQ misc.MPQ model.MPQ sound.MPQ speech.MPQ terrain.MPQ texture.MPQ wmo.MPQ

# Explicit list, not $(call CSRC,...): these targets don't exist on a fresh
# checkout, so a find-wildcard would be empty and the pattern rule would never
# fire (fatal: build/generated/g_*.c missing during `make test`).
WOW_GENERATED_SRCS := \
	$(WOW_GENERATED_DIR)/g_playercreateinfo.c \
	$(WOW_GENERATED_DIR)/g_creatures.c \
	$(WOW_GENERATED_DIR)/g_quests.c \
	$(WOW_GENERATED_DIR)/g_weapons.c \
	$(WOW_GENERATED_DIR)/g_areatrigger_teleport.c

RENDERER_WOW_LIB := $(LIB_DIR)/librenderer-wow$(LIB_EXT)
GAME_WOW_LIB     := $(LIB_DIR)/libgame-wow$(LIB_EXT)
MENU_WOW_LIB       := $(LIB_DIR)/libmenu-wow$(LIB_EXT)
WOW_BINARY       := $(BIN_DIR)/openwow$(EXE_EXT)
WOW_COMMON_SRCS  := $(shell find $(WOW_DIR)/common -name '*.c' 2>/dev/null | sort)

WOW_CFLAGS      := $(CFLAGS) -I$(WOW_DIR) -I$(WOW_DIR)/game -DWOW -DOW3_LOAD_ALL_MPQS -Wno-unused-function -D_GNU_SOURCE -DBZ_GAME=\"world-of-warcraft\"
WOW_TEST_CFLAGS := $(WOW_CFLAGS) -DTOOL_COMMON_NO_MPQ -Itests -Ishared
WOW_MENU_CFLAGS   := $(WOW_CFLAGS) $(LUA_CFLAGS) -DSTB_WOW_XML_IMPLEMENTATION

renderer-wow: $(RENDERER_WOW_LIB)
game-wow:     $(GAME_WOW_LIB)
menu-wow:       $(MENU_WOW_LIB)
openwow:      $(WOW_BINARY)

run-wow: $(WOW_BINARY) install-share
	$(WOW_BINARY) -data $(WOW_INSTALL_DATA_DIR) $(ARGS)

build-run-wow: openwow install-share
	$(WOW_BINARY) -data $(WOW_INSTALL_DATA_DIR)

build-run-wow-map: openwow install-share
	$(WOW_BINARY) -data $(WOW_INSTALL_DATA_DIR) +map 1

# Per-race launch targets: build-run-wow-orc, build-run-wow-human, …
WOW_RACES := Orc:orc Human:human Dwarf:dwarf Undead:undead Tauren:tauren NightElf:nightelf Gnome:gnome Troll:troll
define wow_race_target
build-run-wow-$(word 2,$(subst :, ,$(1))): openwow install-share
	$$(WOW_BINARY) -data $$(WOW_INSTALL_DATA_DIR) +set wow_playerinfo '\race\$(word 1,$(subst :, ,$(1)))\sex\Male\class\1\appearance\0' +map playercreate
endef
$(foreach r,$(WOW_RACES),$(eval $(call wow_race_target,$(r))))

m2tool-wow-orcmale-player: m2tool
	$(BIN_DIR)/m2tool$(EXE_EXT) \
		-mpq $(WOW_INSTALL_DATA_DIR)/model.MPQ \
		-mpq $(WOW_INSTALL_DATA_DIR)/dbc.MPQ \
		-mpq $(WOW_INSTALL_DATA_DIR)/texture.MPQ \
		-model Character/Orc/Male/OrcMale.m2 \
		--equipment 16843009 \
		--wow-player-config

install-wow: $(BIN_DIR)/isoextract$(EXE_EXT) $(BIN_DIR)/mpqtool$(EXE_EXT)
	@if [ -z "$(WOW_ISO_DIR)" ]; then \
		echo "Usage: make install-wow WOW_ISO_DIR=/path/to/wow-isos"; \
		echo "       make install-wow ISO_DIR=/path/to/wow-isos"; \
		exit 1; \
	fi
	@set -e; \
	iso_dir="$(WOW_ISO_DIR)"; \
	extract_dir="$(WOW_ISO_EXTRACT_DIR)"; \
	out_dir="$(WOW_INSTALL_DATA_DIR)"; \
	echo "[install-wow] extracting ISOs from $$iso_dir"; \
	rm -rf "$$extract_dir"; \
	mkdir -p "$$extract_dir" "$$out_dir"; \
	for mpq in $(WOW_INSTALL_MPQS); do \
		rm -f "$$out_dir/$$mpq"; \
	done; \
	for disc in 1 2 3 4; do \
		iso=""; \
		for candidate in "$$iso_dir"/WoWDisc$$disc.iso "$$iso_dir"/WoWDisc$$disc.ISO "$$iso_dir"/*Disc$$disc*.iso "$$iso_dir"/*Disc$$disc*.ISO "$$iso_dir"/*disc$$disc*.iso "$$iso_dir"/*disc$$disc*.ISO; do \
			if [ -f "$$candidate" ]; then iso="$$candidate"; break; fi; \
		done; \
		if [ -z "$$iso" ]; then \
			echo "install-wow: missing ISO for WoW disc $$disc in $$iso_dir" >&2; \
			exit 1; \
		fi; \
		"$(BIN_DIR)/isoextract$(EXE_EXT)" "$$iso" "$$extract_dir/WoWDisc$$disc"; \
	done; \
	echo "[install-wow] repacking $$out_dir"; \
	"$(BIN_DIR)/mpqtool$(EXE_EXT)" wow-install -strip-data-prefix "$$out_dir" \
		"$$extract_dir/WoWDisc1/Installer Tome.mpq" \
		"$$extract_dir/WoWDisc2/Installer Tome 2.mpq" \
		"$$extract_dir/WoWDisc3/Installer Tome 3.mpq" \
		"$$extract_dir/WoWDisc4/Installer Tome 4.mpq"; \
	if [ "$(WOW_KEEP_EXTRACT)" != "1" ]; then \
		echo "[install-wow] removing $$extract_dir"; \
		rm -rf "$$extract_dir"; \
	fi

$(BIN_DIR)/m2tool$(EXE_EXT): tools/m2tool.c $(TOOL_DEPS) $(CLIENT_HEADERS) $(COMMON_HEADERS) | $(BIN_DIR) $(SHARED_LIB) $(SHEET_LIB) $(RENDERER_WOW_LIB)
	@$(CC) $(WOW_CFLAGS) -o $@ $< \
		$(RPATH) $(LDFLAGS) -lrenderer-wow -lsheet -lshared $(LIBS) -lm -lz

# Generated serverdata — pattern rule covers all tables; quests gets an extra dep
$(WOW_GENERATED_DIR)/g_quests.c: $(WOW_DIR)/serverdata/quest_spawns.csv

$(WOW_GENERATED_DIR)/g_%.c: $(WOW_GENERATOR) $(WOW_DIR)/serverdata/%.csv
	@mkdir -p $(@D)
	@python3 $(WOW_GENERATOR) --only $(patsubst g_%.c,%,$(notdir $@)) --output-dir $(WOW_GENERATED_DIR)

$(eval $(call unity_lib_schema,$(RENDERER_WOW_LIB),$(RENDERER_BASE_DEPS) $(call CSRC,renderer $(WOW_DIR)/renderer),renderer-wow,renderer $(WOW_DIR)/renderer,,$(WOW_CFLAGS),common/mpq.c,$(RENDERER_SHARED_LIBS)))

$(eval $(call unity_lib_schema,$(GAME_WOW_LIB),$(GAME_BASE_DEPS) $(WOW_GENERATED_SRCS) common/world.c $(WOW_COMMON_SRCS) $(call CSRC,$(WOW_DIR)/game),game-wow,$(WOW_DIR)/game,,$(WOW_CFLAGS),common/mpq.c $(SERVER_GAME_SRCS),-lshared $(LIBS) -lm -lz))

$(eval $(call unity_lib_schema,$(MENU_WOW_LIB),$(UI_BASE_DEPS) client/ui.h $(LUA_LIB) $(call CSRC,$(WOW_DIR)/menu),menu-wow,$(WOW_DIR)/menu,,$(WOW_MENU_CFLAGS),,-lshared -llua -lm))

$(eval $(call app_schema,$(WOW_BINARY),$(SHARED_LIB) $(SHEET_LIB) $(GAME_WOW_LIB) $(RENDERER_WOW_LIB) $(MENU_WOW_LIB) $(APP_SRCS) $(CLIENT_HEADERS) $(COMMON_HEADERS),openwow,$(WOW_CFLAGS),-lsheet -lshared -lgame-wow -lrenderer-wow -lmenu-wow $(LIBS) -lz))

# ---------------------------------------------------------------------------
# In-engine tests (see CONTRIBUTING.md)
# ---------------------------------------------------------------------------
GAME_WOW_TEST_LIB := $(LIB_DIR)/libgame-wow-test$(LIB_EXT)
WOW_TEST_BINARY   := $(BIN_DIR)/openwow-tests$(EXE_EXT)

$(eval $(call unity_lib_schema,$(GAME_WOW_TEST_LIB),$(GAME_BASE_DEPS) $(WOW_GENERATED_SRCS) common/world.c $(WOW_COMMON_SRCS) $(call CSRC,$(WOW_DIR)/game) $(RENDERER_WOW_LIB),game-wow-test,$(WOW_DIR)/game,,$(WOW_CFLAGS) -DBZ_TESTS,common/mpq.c $(SERVER_GAME_SRCS),-lshared -lrenderer-wow $(LIBS) -lm -lz))
$(eval $(call app_schema,$(WOW_TEST_BINARY),$(SHARED_LIB) $(SHEET_LIB) $(GAME_WOW_TEST_LIB) $(RENDERER_WOW_LIB) $(MENU_WOW_LIB) $(APP_SRCS) $(CLIENT_HEADERS) $(COMMON_HEADERS),openwow-tests,$(WOW_CFLAGS),-lsheet -lshared -lgame-wow-test -lrenderer-wow -lmenu-wow $(LIBS) -lz))

openwow-tests: $(WOW_TEST_BINARY)

PATTERN ?= *
test-wow-engine: $(WOW_TEST_BINARY) | test-wow-engine-assets
	$(WOW_TEST_BINARY) -data $(WOW_ENGINE_TEST_DIR) +dedicated 1 +test '$(PATTERN)'

# ---------------------------------------------------------------------------
# Standalone test binaries
# ---------------------------------------------------------------------------
WOW_TEST_RES_DIR    := $(TESTS_DIR)/wow-resources
WOW_TEST_SRC_DIR    := $(WOW_TEST_DIR)/resources-src
WOW_TEST_MPQ        := $(TESTS_DIR)/test-wow.mpq
WOW_MENU_TEST_CFLAGS  := $(WOW_TEST_CFLAGS) $(LUA_CFLAGS) -DTEST_WOW_MPQ=\"$(WOW_TEST_MPQ)\"
WOW_WMO_TEST_DATA_DIR := $(TESTS_DIR)/wow-wmo-data
WOW_WMO_TEST_MPQ    := $(TESTS_DIR)/test-wow-wmo.mpq
WOW_WMO_TEST_CFLAGS := $(WOW_TEST_CFLAGS) -DTEST_WOW_WMO_MPQ=\"$(WOW_WMO_TEST_MPQ)\"

$(eval $(call test_schema,test-wow-appearance,,$(WOW_TEST_CFLAGS),$(BIN_DIR)/test_wow_appearance$(EXE_EXT),tests/test_runner.c $(WOW_TEST_DIR)/test_wow_appearance.c $(WOW_DIR)/renderer/m2/r_dbc.c common/msg.c common/net.c $(call CSRC,shared),-lm $(NET_LIBS),))
$(eval $(call test_schema,test-wow-abilities,$(WOW_GENERATED_SRCS),$(WOW_TEST_CFLAGS),$(BIN_DIR)/test_wow_abilities$(EXE_EXT),tests/test_runner.c $(WOW_TEST_DIR)/test_wow_abilities.c $(WOW_DIR)/game/g_wow.c $(WOW_DIR)/game/g_world.c $(WOW_DIR)/game/g_ai.c $(WOW_DIR)/game/m_creature.c $(WOW_DIR)/game/g_gameobject.c $(WOW_DIR)/game/g_spawn.c $(SERVER_GAME_SRCS) $(COMMON_GAME_SRCS) $(call CSRC,shared),-lm -lz,))
$(eval $(call test_schema,test-wow-game,$(WOW_GENERATED_SRCS),$(WOW_TEST_CFLAGS),$(BIN_DIR)/test_wow_game$(EXE_EXT),tests/test_runner.c $(WOW_TEST_DIR)/test_wow_game.c $(WOW_DIR)/game/g_wow.c $(WOW_DIR)/game/g_ui.c $(WOW_DIR)/game/g_world.c $(WOW_DIR)/game/g_ai.c $(WOW_DIR)/game/m_creature.c $(WOW_DIR)/game/g_gameobject.c $(WOW_DIR)/game/g_spawn.c $(SERVER_GAME_SRCS) $(COMMON_GAME_SRCS) $(call CSRC,shared),-lm -lz,))
$(eval $(call test_schema,test-wow-entities,$(WOW_GENERATED_SRCS),$(WOW_TEST_CFLAGS),$(BIN_DIR)/test_wow_entities$(EXE_EXT),tests/test_runner.c $(WOW_TEST_DIR)/test_wow_entities.c $(WOW_DIR)/game/g_wow.c $(WOW_DIR)/game/g_world.c $(WOW_DIR)/game/g_ai.c $(WOW_DIR)/game/m_creature.c $(WOW_DIR)/game/g_gameobject.c $(WOW_DIR)/game/g_spawn.c $(SERVER_GAME_SRCS) $(COMMON_GAME_SRCS) $(call CSRC,shared),-lm -lz,))
$(eval $(call test_schema,test-wow-menu,test-wow-assets $(LUA_LIB),$(WOW_MENU_TEST_CFLAGS),$(BIN_DIR)/test_wow_ui$(EXE_EXT),tests/test_runner.c $(WOW_TEST_DIR)/test_wow_ui.c $(WOW_DIR)/menu/menu_main.c $(WOW_DIR)/menu/menu_lua.c $(WOW_DIR)/menu/menu_dbc.c $(WOW_DIR)/menu/menu_loading.c $(WOW_DIR)/menu/menu_xml.c $(WOW_DIR)/menu/menu_windows.c common/mpq.c,-lshared -llua -lm -lz,))
$(eval $(call test_schema,test-wow-wmo,test-wow-wmo-assets,$(WOW_WMO_TEST_CFLAGS),$(BIN_DIR)/test_wow_wmo$(EXE_EXT),tests/test_runner.c $(WOW_TEST_DIR)/test_wow_wmo.c $(call CSRC,shared),-lm,))
$(eval $(call test_schema,test-wow-hud-xml,test-wow-assets $(LUA_LIB),$(WOW_MENU_TEST_CFLAGS),$(BIN_DIR)/test_wow_hud_xml$(EXE_EXT),tests/test_runner.c $(WOW_TEST_DIR)/test_wow_hud_xml.c $(WOW_DIR)/menu/menu_main.c $(WOW_DIR)/menu/menu_lua.c $(WOW_DIR)/menu/menu_dbc.c $(WOW_DIR)/menu/menu_loading.c $(WOW_DIR)/menu/menu_xml.c $(WOW_DIR)/menu/menu_windows.c common/mpq.c,-lshared -llua -lm -lz,))

test-wow-assets: blpgen mpqtool | $(TESTS_DIR)
	@echo "[test-wow-assets] generating WoW UI fixtures"
	@mkdir -p $(WOW_TEST_RES_DIR)/Interface/Test
	@$(BIN_DIR)/blpgen$(EXE_EXT) solid 16 8 cc8844ff $(WOW_TEST_RES_DIR)/Interface/Test/LuaPanel.blp
	@$(BIN_DIR)/blpgen$(EXE_EXT) checker 8 8 2 $(WOW_TEST_RES_DIR)/Interface/Test/Inventory.blp
	@echo "[test-wow-assets] packing test-wow.mpq"
	@set --; \
	for f in $$(find $(WOW_TEST_RES_DIR) -type f | sort); do \
		rel=$${f#$(WOW_TEST_RES_DIR)/}; set -- "$$@" "$$f" "$$rel"; \
	done; \
	for f in $$(find $(WOW_TEST_SRC_DIR) -type f | sort); do \
		rel=$${f#$(WOW_TEST_SRC_DIR)/}; set -- "$$@" "$$f" "$$rel"; \
	done; \
	$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WOW_TEST_MPQ) pack "$$@"
	@echo "[test-wow-assets] verifying archive"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WOW_TEST_MPQ) cat Interface/Test/LuaPanel.blp | head -c4 | grep -q "BLP2" && echo "  cat panel OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WOW_TEST_MPQ) cat Interface/FrameXML/GameHUD.lua | grep -q "wow_lua_test" && echo "  cat lua OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WOW_TEST_MPQ) cat Interface/FrameXML/WelcomeFrame.xml | grep -q "WelcomeFrame" && echo "  cat WelcomeFrame.xml OK"

# ---------------------------------------------------------------------------
# test-wow-wmo-assets — generate minimal WMO fixture files for WMO unit tests
# ---------------------------------------------------------------------------
WOW_WMO_FIXTURE := $(WOW_WMO_TEST_DATA_DIR)/World/wmo/test/TestBuilding.wmo

test-wow-wmo-assets: wmogen wdtgen mpqtool | $(TESTS_DIR)
	@echo "[test-wow-wmo-assets] generating WMO fixtures"
	@mkdir -p $(WOW_WMO_TEST_DATA_DIR)/World/wmo/test \
	           $(WOW_WMO_TEST_DATA_DIR)/World/Maps/TestDungeon
	@$(BIN_DIR)/wmogen$(EXE_EXT) $(WOW_WMO_FIXTURE) \
		--amb 80 40 20 \
		--flags 0x00 \
		--mocv 100 200 150 255 \
		--trans-batches 1 \
		--doodad World/props/Barrel.mdx 1.0 2.0 0.5 0 0 0 1 1.5 \
		--doodad World/props/Crate.mdx  -1.0 3.0 0.0 0 0.7071 0 0.7071 1.0 \
		--light 0 0.0 0.0 3.0 200 180 255 1.2
	@$(BIN_DIR)/wdtgen$(EXE_EXT) $(WOW_WMO_TEST_DATA_DIR)/World/Maps/TestDungeon/TestDungeon.wdt \
		--global-wmo World/wmo/test/TestBuilding.wmo
	@echo "[test-wow-wmo-assets] packing $(WOW_WMO_TEST_MPQ)"
	@set --; \
	for f in $$(find $(WOW_WMO_TEST_DATA_DIR) -type f | sort); do \
		rel=$${f#$(WOW_WMO_TEST_DATA_DIR)/}; set -- "$$@" "$$f" "$$rel"; \
	done; \
	$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WOW_WMO_TEST_MPQ) pack "$$@"
	@echo "[test-wow-wmo-assets] verifying archive"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WOW_WMO_TEST_MPQ) \
		info World/wmo/test/TestBuilding.wmo | grep -q "size=" && echo "  root WMO OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WOW_WMO_TEST_MPQ) \
		info World/wmo/test/TestBuilding_000.wmo | grep -q "size=" && echo "  group WMO OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WOW_WMO_TEST_MPQ) \
		info World/Maps/TestDungeon/TestDungeon.wdt | grep -q "size=" && echo "  global WDT OK"

# ---------------------------------------------------------------------------
# test-wow-engine-assets — generate a minimal M2 model for in-engine WoW tests
# ---------------------------------------------------------------------------
WOW_ENGINE_TEST_DIR  := $(TESTS_DIR)/wow-engine-data
WOW_ENGINE_TEST_MPQ  := $(WOW_ENGINE_TEST_DIR)/test-wow-engine.mpq
WOW_ENGINE_MODEL_DIR := $(WOW_ENGINE_TEST_DIR)/Character/Orc/Male
WOW_ENGINE_MODEL_M2  := $(WOW_ENGINE_MODEL_DIR)/OrcMale.m2

test-wow-engine-assets: m2gen mpqtool | $(TESTS_DIR)
	@echo "[test-wow-engine-assets] generating minimal M2 model"
	@mkdir -p $(WOW_ENGINE_MODEL_DIR)
	@$(BIN_DIR)/m2gen$(EXE_EXT) $(WOW_ENGINE_MODEL_M2) \
		Stand=0:0:1000 \
		Death=1:1000:2600 \
		Attack1H=17:2600:3600
	@echo "[test-wow-engine-assets] packing test-wow-engine.mpq"
	@set --; \
	for f in $$(find $(WOW_ENGINE_TEST_DIR) -type f ! -name '*.mpq' | sort); do \
		rel=$${f#$(WOW_ENGINE_TEST_DIR)/}; set -- "$$@" "$$f" "$$rel"; \
	done; \
	$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WOW_ENGINE_TEST_MPQ) pack "$$@"
	@echo "[test-wow-engine-assets] verifying archive"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(WOW_ENGINE_TEST_MPQ) cat Character/Orc/Male/OrcMale.m2 | head -c4 | grep -q "MD20" && echo "  cat M2 OK"

# UI layout sandbox: black background, no world spawns.  Type "quest <id>" in-game.
run-wow-preview: $(WOW_BINARY)
	$(WOW_BINARY) -data $(WOW_INSTALL_DATA_DIR) +map preview $(ARGS)

WOW_RACE_TARGETS := $(foreach r,$(WOW_RACES),build-run-wow-$(word 2,$(subst :, ,$(r))))
WOW_PHONY := renderer-wow game-wow menu-wow openwow openwow-tests \
	run-wow-preview run-wow build-run-wow build-run-wow-map $(WOW_RACE_TARGETS) \
	m2tool-wow-orcmale-player install-wow \
	test-wow-engine test-wow-engine-assets \
	test-wow-appearance test-wow-abilities test-wow-game test-wow-entities \
	test-wow-menu test-wow-assets test-wow-wmo test-wow-wmo-assets test-wow-hud-xml
