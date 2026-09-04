SC2_DIR      := games/starcraft-2
SC2_TEST_DIR := $(SC2_DIR)/tests

RENDERER_SC2_LIB := $(LIB_DIR)/librenderer-sc2$(LIB_EXT)
GAME_SC2_LIB     := $(LIB_DIR)/libgame-sc2$(LIB_EXT)
MENU_SC2_LIB       := $(LIB_DIR)/libmenu-sc2$(LIB_EXT)
SC2_BINARY       := $(BIN_DIR)/opensc2$(EXE_EXT)
SC2_COMMON_SRCS  := $(shell find $(SC2_DIR)/common -name '*.c' 2>/dev/null | sort)

SC2_DEBUG_CFLAGS ?=
SC2_CFLAGS       := $(CFLAGS) $(SC2_DEBUG_CFLAGS) -I$(SC2_DIR) -DSC2 -DOW3_LOAD_ALL_MPQS -Wno-unused-function -DBZ_GAME=\"starcraft-2\" -DUSE_SHADOWMAPS -DSC2_DEFAULT_MAP=\"Maps/Campaign/TRaynor01.SC2Map\"
SC2_IMPL_CFLAGS  := $(SC2_CFLAGS) -DSTB_SC2LAYOUT_IMPLEMENTATION -DSTB_SC2LAYOUT_GLOBALS
SC2_TEST_CFLAGS  := $(SC2_CFLAGS) -I. -Itests -Icommon -Ishared -DTEST_SC2_MPQ=\"build/tests/test-sc2.SC2Maps\"

renderer-sc2: $(RENDERER_SC2_LIB)
game-sc2:     $(GAME_SC2_LIB)
menu-sc2:       $(MENU_SC2_LIB)
opensc2:      $(SC2_BINARY)

run-sc2: $(SC2_BINARY) install-share
	$(SC2_BINARY) -data data/StarCraft2 +map Maps/Campaign/TRaynor01.SC2Map $(ARGS)

build-run-sc2: opensc2 install-share
	$(SC2_BINARY) -data data/StarCraft2 +map Maps/Campaign/TRaynor01.SC2Map

$(BIN_DIR)/m3tool$(EXE_EXT): tools/m3tool.c $(TOOL_DEPS) $(CLIENT_HEADERS) $(COMMON_HEADERS) | $(BIN_DIR) $(SHARED_LIB) $(SHEET_LIB) $(RENDERER_SC2_LIB)
	@$(CC) $(SC2_CFLAGS) -o $@ $< \
		$(RPATH) $(LDFLAGS) -lrenderer-sc2 -lsheet -lshared $(LIBS) -lm -lz

$(BIN_DIR)/sc2map$(EXE_EXT): tools/sc2map.c $(SC2_DIR)/common/sc2_map.c $(SC2_DIR)/common/sc2_map.h common/mpq.c | $(BIN_DIR) $(SHARED_LIB)
	@$(CC) $(SC2_CFLAGS) -o $@ tools/sc2map.c $(SC2_DIR)/common/sc2_map.c common/mpq.c \
		$(RPATH) $(LDFLAGS) -lshared -lm -lz

$(eval $(call unity_lib_schema,$(RENDERER_SC2_LIB),$(RENDERER_BASE_DEPS) $(call CSRC,renderer $(SC2_DIR)/renderer) $(SC2_COMMON_SRCS),renderer-sc2,renderer $(SC2_DIR)/renderer,,$(SC2_CFLAGS),common/mpq.c,$(RENDERER_SHARED_LIBS)))

$(eval $(call unity_lib_schema,$(GAME_SC2_LIB),$(GAME_BASE_DEPS) $(JASS_LIB) $(WORLD_CORE_SRCS) $(SC2_COMMON_SRCS) $(call CSRC,$(SC2_DIR)/game),game-sc2,$(SC2_DIR)/game,,$(SC2_IMPL_CFLAGS),common/mpq.c,-ljass -lshared $(LIBS) -lm -lz))

$(eval $(call unity_lib_schema,$(MENU_SC2_LIB),$(UI_BASE_DEPS) client/ui.h $(call CSRC,$(SC2_DIR)/menu),menu-sc2,$(SC2_DIR)/menu,,$(SC2_IMPL_CFLAGS),,-lshared))

$(eval $(call app_schema,$(SC2_BINARY),$(SHARED_LIB) $(SHEET_LIB) $(GAME_SC2_LIB) $(RENDERER_SC2_LIB) $(MENU_SC2_LIB) $(APP_SRCS) $(CLIENT_HEADERS),opensc2,$(SC2_IMPL_CFLAGS),-lsheet -lshared -lgame-sc2 -lrenderer-sc2 -lmenu-sc2 $(LIBS) -lz))

# ---------------------------------------------------------------------------
# Standalone test binaries
# ---------------------------------------------------------------------------
SC2_TEST_RES_DIR := $(TESTS_DIR)/sc2-resources
SC2_TEST_SRC_DIR := $(SC2_TEST_DIR)/resources-src
SC2_TEST_MPQ     := $(TESTS_DIR)/test-sc2.SC2Maps

$(eval $(call test_schema,test-sc2,test-sc2-assets $(SHARED_LIB) $(SHEET_LIB),$(SC2_TEST_CFLAGS),$(BIN_DIR)/test_sc2$(EXE_EXT),tests/test_runner.c $(SC2_TEST_DIR)/test_sc2_map.c $(SC2_TEST_DIR)/test_sc2_layout.c $(SC2_TEST_DIR)/test_sc2_consoleui.c $(SC2_TEST_DIR)/stb_sc2layout_impl.c $(SC2_DIR)/common/sc2_map.c common/common.c common/cmd.c common/cvar.c common/msg.c common/net.c common/mpq.c,-lsheet -lshared -lm -lz $(NET_LIBS),))

test-sc2-assets: sc2fixturegen mpqtool sc2map | $(TESTS_DIR)
	@echo "[test-sc2-assets] generating SC2 terrain fixtures"
	@mkdir -p $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) map-info $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/MapInfo
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) height-map $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/t3HeightMap
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) sync-height-map $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/t3SyncHeightMap
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) cell-flags $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/t3CellFlags
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) cliff-levels $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/t3SyncCliffLevel
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) texture-masks $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/t3TextureMasks
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) hard-tiles $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/t3HardTile
	@echo "[test-sc2-assets] packing test-sc2.SC2Maps"
	@set --; \
	for f in $$(find $(SC2_TEST_RES_DIR) -type f | sort); do \
		rel=$${f#$(SC2_TEST_RES_DIR)/}; set -- "$$@" "$$f" "$$rel"; \
	done; \
	for f in $$(find $(SC2_TEST_SRC_DIR) -type f | sort); do \
		rel=$${f#$(SC2_TEST_SRC_DIR)/}; \
		if [ -f "$(SC2_TEST_RES_DIR)/$$rel" ]; then continue; fi; \
		set -- "$$@" "$$f" "$$rel"; \
	done; \
	$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(SC2_TEST_MPQ) pack "$$@"
	@echo "[test-sc2-assets] verifying archive"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(SC2_TEST_MPQ) ls Maps/Test/Tiny.SC2Map | grep -q "MapInfo" && echo "  ls map OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(SC2_TEST_MPQ) cat Maps/Test/Tiny.SC2Map/Objects | grep -q "UnitType=\"Marine\"" && echo "  cat objects OK"
	@$(BIN_DIR)/mpqtool$(EXE_EXT) -mpq $(SC2_TEST_MPQ) info Maps/Test/Tiny.SC2Map/t3CellFlags | grep -q "size=80" && echo "  binary cell flags OK"
	@diag="$$( $(BIN_DIR)/sc2map$(EXE_EXT) -mpq $(SC2_TEST_MPQ) Maps/Test/Tiny.SC2Map )"; \
	echo "$$diag" | grep -q "Objects: units=3 doodads=2 points=1 cameras=1 total=7" && \
	echo "$$diag" | grep -q "MarineManifestModel" && \
	echo "$$diag" | grep -q "footprint=Footprint2x2 size=2.000x2.000 fpRadius=1.414" && \
	echo "  sc2map diag OK"

SC2_PHONY := renderer-sc2 game-sc2 menu-sc2 opensc2 run-sc2 build-run-sc2 \
	test-sc2 test-sc2-assets
