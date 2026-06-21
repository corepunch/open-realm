ZIP_URL  := https://archive.org/download/warcraft-iii-installer-enus/Warcraft-III-1.29.2-enUS.zip
ZIP_FILE := Warcraft-III-1.29.2-enUS.zip
DATA_DIR := data

CC      := gcc
BIN_DIR := build/bin
LIB_DIR := build/lib
CFLAGS  := -Wall -I. -Ishared -Ishared/types
WOW_DIR := games/world-of-warcraft
WOW_TEST_DIR := $(WOW_DIR)/tests
WOW_DATA_DIR := data/world-of-warcraft
WOW_ISO_DIR ?= $(ISO_DIR)
WOW_ISO_EXTRACT_DIR ?= build/wow-install
WOW_KEEP_EXTRACT ?= 0
WOW_INSTALL_DATA_DIR ?= $(WOW_DATA_DIR)
WOW_INSTALL_MPQS := base.MPQ dbc.MPQ fonts.MPQ interface.MPQ misc.MPQ model.MPQ sound.MPQ speech.MPQ terrain.MPQ texture.MPQ wmo.MPQ
SC2_DIR := games/starcraft-2
SC2_TEST_DIR := $(SC2_DIR)/tests

ifeq ($(DIAG_OUTPUT),1)
	CFLAGS += -DDIAG_OUTPUT
endif
ifeq ($(NO_NETWORK),1)
	CFLAGS += -DBZ_NO_NETWORK
endif
# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
    LIB_EXT   := .dll
    LIB_FLAGS := -shared
    EXE_EXT   := .exe
    INSTALL_NAME =
    RPATH     :=
    LDFLAGS   := -L$(LIB_DIR)
	LIBS      := -lSDL2main -lSDL2 -lm -lopengl32 -lgdi32
else
    UNAME_S := $(shell uname -s)
    EXE_EXT :=
    ifeq ($(UNAME_S),Darwin)
		ARCH ?= arm64
		ifeq ($(ARCH),arm64)
            HOMEBREW_PREFIX := /opt/homebrew
        else
            HOMEBREW_PREFIX := /usr/local
        endif
        LIB_EXT   := .dylib
        LIB_FLAGS := -dynamiclib
        # Set the dylib install name to @rpath/<libname> so the executable
        # resolves it via the @executable_path/../lib rpath, not a build path.
        INSTALL_NAME = -Wl,-install_name,@rpath/$(notdir $@)
        RPATH     := -Wl,-rpath,@executable_path/../lib
		CFLAGS    += -DGL_SILENCE_DEPRECATION -I$(HOMEBREW_PREFIX)/include -arch $(ARCH)
		LDFLAGS   := -L$(LIB_DIR) -L$(HOMEBREW_PREFIX)/lib -arch $(ARCH)
		LIBS      := -lSDL2 -framework AppKit -framework OpenGL
    else
        # Linux
        LIB_EXT   := .so
        LIB_FLAGS := -shared -fPIC
        INSTALL_NAME =
        RPATH     := -Wl,-rpath,'$$ORIGIN/../lib'
        CFLAGS    += -fPIC
        LDFLAGS   := -L$(LIB_DIR) -Wl,-z,defs
		LIBS      := -lSDL2 -lEGL -lGL -lm
    endif
endif

SHARED_LIB   := $(LIB_DIR)/libshared$(LIB_EXT)
RENDERER_WOW_LIB := $(LIB_DIR)/librenderer-wow$(LIB_EXT)
RENDERER_SC2_LIB := $(LIB_DIR)/librenderer-sc2$(LIB_EXT)
GAME_WOW_LIB     := $(LIB_DIR)/libgame-wow$(LIB_EXT)
GAME_SC2_LIB     := $(LIB_DIR)/libgame-sc2$(LIB_EXT)
UI_WOW_LIB       := $(LIB_DIR)/libui-wow$(LIB_EXT)
UI_SC2_LIB       := $(LIB_DIR)/libui-sc2$(LIB_EXT)
WOW_BINARY   := $(BIN_DIR)/openwow$(EXE_EXT)
SC2_BINARY   := $(BIN_DIR)/opensc2$(EXE_EXT)
WOW_CFLAGS   := $(CFLAGS) -I$(WOW_DIR) -DWOW -DOW3_LOAD_ALL_MPQS -Wno-unused-function
WOW_TEST_CFLAGS := $(WOW_CFLAGS) -DTOOL_COMMON_NO_MPQ -Itests
LUA_CFLAGS   := $(shell pkg-config --cflags lua5.4 2>/dev/null || pkg-config --cflags lua 2>/dev/null)
LUA_LIBS     := $(shell pkg-config --libs lua5.4 2>/dev/null || pkg-config --libs lua 2>/dev/null)
WOW_XML_CFLAGS := $(shell pkg-config --cflags libxml-2.0 2>/dev/null || xml2-config --cflags 2>/dev/null) -I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/libxml2
WOW_XML_LIBS := $(shell pkg-config --libs libxml-2.0 2>/dev/null || xml2-config --libs 2>/dev/null)
WOW_UI_CFLAGS := $(WOW_CFLAGS) $(LUA_CFLAGS) $(WOW_XML_CFLAGS)
SC2_XML_CFLAGS := $(shell pkg-config --cflags libxml-2.0 2>/dev/null || xml2-config --cflags 2>/dev/null) -I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/libxml2
SC2_XML_LIBS := $(shell pkg-config --libs libxml-2.0 2>/dev/null || xml2-config --libs 2>/dev/null)
SC2_CFLAGS   := $(CFLAGS) -I$(SC2_DIR) -DSC2 -DOW3_LOAD_ALL_MPQS -Wno-unused-function $(SC2_XML_CFLAGS)
SC2_UI_CFLAGS := $(SC2_CFLAGS)
SC2_UI_LIBS   := -lshared -lsheet $(SC2_XML_LIBS)
SC2_TEST_CFLAGS := $(SC2_CFLAGS) -I. -Itests -Icommon -DTEST_SC2_MPQ=\"build/tests/test-sc2.SC2Maps\"

TOOL_SRCS := $(shell find tools -maxdepth 1 -name '*.c' ! -name 'jass.c' | sort)
TOOL_NAMES := $(patsubst tools/%.c,%,$(TOOL_SRCS))
TOOL_BINS := $(addprefix $(BIN_DIR)/,$(addsuffix $(EXE_EXT),$(TOOL_NAMES)))
TOOL_DEPS := $(shell find tools -maxdepth 1 -name '*.h' | sort)
CLIENT_HEADERS := $(shell find client -name '*.h' | sort)
COMMON_HEADERS := $(shell find common -name '*.h' | sort)
WOW_COMMON_SRCS := $(shell find $(WOW_DIR)/common -name '*.c' 2>/dev/null | sort)
SC2_COMMON_SRCS := $(shell find $(SC2_DIR)/common -name '*.c' 2>/dev/null | sort)
WORLD_CORE_SRCS := common/world.c common/routing.c
FONT_SRC := renderer/conchars.pcx
FONT_HEADER := renderer/conchars_sysfont.h
FONT_SYMBOL := conchars_sysfont_pcx

# Unity-build helper: pipe all .c files in a directory tree as #include
# directives to gcc's stdin so the whole module is one translation unit.
# Note: awk with octal \043 for '#' avoids Make treating '#' in a variable
# value as a comment character, which would truncate the sed expression.
UNITY = find $1 -name '*.c' $2 | sort | awk '{printf "\043include \"%s\"\n", $$0}'
CSRC = $(shell find $(1) -name '*.c' $(2) | sort)

define unity_lib_schema
$(1): $(2) | $$(LIB_DIR)
	@echo "[$(3)]"
	@$$(call UNITY,$(4),$(5)) | \
		$$(CC) $(6) $$(LIB_FLAGS) $$(INSTALL_NAME) -x c -o $$@ - $(7) $$(LDFLAGS) $(8)
endef

define src_lib_schema
$(1): $(2) | $$(LIB_DIR)
	@echo "[$(3)]"
	@$$(CC) $(4) $$(LIB_FLAGS) $$(INSTALL_NAME) -x c -o $$@ $(5) $$(LDFLAGS) $(6)
endef

define app_schema
$(1): $(2) | $$(BIN_DIR)
	@echo "[$(3)]"
	@$$(call UNITY,client server common sound,! -name 'stb_vorbis.c') | \
		$$(CC) $(4) -x c -o $$@ - $$(RPATH) $$(LDFLAGS) $(5)
endef

define test_schema
$(1): $(2) | $$(BIN_DIR)
	@$$(CC) $(3) -o $(4) $(5) $$(RPATH) $$(LDFLAGS) $(6)
	@$(4) $(7)
endef

default: build
build:
shared:      $(SHARED_LIB)
renderer-wow: $(RENDERER_WOW_LIB)
renderer-sc2: $(RENDERER_SC2_LIB)
game-wow:     $(GAME_WOW_LIB)
ui-wow:       $(UI_WOW_LIB)
ui-sc2:       $(UI_SC2_LIB)
openwow:      $(WOW_BINARY)
game-sc2:     $(GAME_SC2_LIB)
opensc2:      $(SC2_BINARY)
tools:       $(TOOL_BINS)
	@echo "[tools]"
font:       $(FONT_HEADER)
$(TOOL_NAMES): %: $(BIN_DIR)/%$(EXE_EXT)

run-wow: $(WOW_BINARY)
	$(WOW_BINARY) -data $(WOW_INSTALL_DATA_DIR) +map World/Maps/Azeroth/Azeroth.wdt

build-run-wow: openwow
	$(WOW_BINARY) -data $(WOW_INSTALL_DATA_DIR)

build-run-wow-map: openwow
	$(WOW_BINARY) -data $(WOW_INSTALL_DATA_DIR) +map World/Maps/Azeroth/Azeroth.wdt

run-sc2: $(SC2_BINARY)
	$(SC2_BINARY) -data data/StarCraft2 +map TRaynor01

build-run-sc2: opensc2
	$(SC2_BINARY) -data data/StarCraft2 +map TRaynor01

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

diag: clean
	$(MAKE) DIAG_OUTPUT=1 build
	$(MAKE) DIAG_OUTPUT=1 run

$(BIN_DIR) $(LIB_DIR):
	@mkdir -p $@

APP_SRCS := $(shell find client server common -name '*.c')
RENDERER_BASE_DEPS := $(SHARED_LIB) $(CLIENT_HEADERS) $(COMMON_HEADERS) common/mpq.c common/mpq.h $(FONT_HEADER)
RENDERER_SHARED_LIBS := -lshared $(LIBS) -lz
GAME_BASE_DEPS := $(SHARED_LIB) $(COMMON_HEADERS) common/mpq.c common/mpq.h
UI_BASE_DEPS := $(SHARED_LIB) $(CLIENT_HEADERS) $(COMMON_HEADERS)

$(eval $(call unity_lib_schema,$(SHARED_LIB),$(call CSRC,shared),shared,shared,,$(CFLAGS),,-lm))

include games/warcraft-3/game.mk

WOW_TEST_RES_DIR := $(TESTS_DIR)/wow-resources
WOW_TEST_SRC_DIR := $(WOW_TEST_DIR)/resources-src
WOW_TEST_MPQ     := $(TESTS_DIR)/test-wow.mpq
WOW_UI_TEST_CFLAGS := $(WOW_TEST_CFLAGS) $(LUA_CFLAGS) -DTEST_WOW_MPQ=\"$(WOW_TEST_MPQ)\"
SC2_TEST_RES_DIR := $(TESTS_DIR)/sc2-resources
SC2_TEST_SRC_DIR := $(SC2_TEST_DIR)/resources-src
SC2_TEST_MPQ     := $(TESTS_DIR)/test-sc2.SC2Maps

$(BIN_DIR)/%$(EXE_EXT): tools/%.c $(TOOL_DEPS) $(CLIENT_HEADERS) $(COMMON_HEADERS) | $(BIN_DIR) $(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB) $(RENDERER_LIB) $(GAME_LIB) $(UI_LIB)
	@$(CC) $(CFLAGS) -o $@ $< \
		$(RPATH) $(LDFLAGS) -lsheet -lshared -ljass -lrenderer -lgame -lui $(LIBS) -lm -lz

$(BIN_DIR)/m2tool$(EXE_EXT): tools/m2tool.c $(TOOL_DEPS) $(CLIENT_HEADERS) $(COMMON_HEADERS) | $(BIN_DIR) $(SHARED_LIB) $(SHEET_LIB) $(RENDERER_WOW_LIB)
	@$(CC) $(WOW_CFLAGS) -o $@ $< \
		$(RPATH) $(LDFLAGS) -lrenderer-wow -lsheet -lshared $(LIBS) -lm -lz

$(BIN_DIR)/m3tool$(EXE_EXT): tools/m3tool.c $(TOOL_DEPS) $(CLIENT_HEADERS) $(COMMON_HEADERS) | $(BIN_DIR) $(SHARED_LIB) $(SHEET_LIB) $(RENDERER_SC2_LIB)
	@$(CC) $(SC2_CFLAGS) -o $@ $< \
		$(RPATH) $(LDFLAGS) -lrenderer-sc2 -lsheet -lshared $(LIBS) -lm -lz $(SC2_XML_LIBS)

$(BIN_DIR)/sc2map$(EXE_EXT): tools/sc2map.c $(SC2_DIR)/common/sc2_map.c $(SC2_DIR)/common/sc2_map.h common/mpq.c | $(BIN_DIR)
	@$(CC) $(SC2_CFLAGS) -o $@ tools/sc2map.c $(SC2_DIR)/common/sc2_map.c common/mpq.c \
		$(RPATH) $(LDFLAGS) -lm -lz $(SC2_XML_LIBS)

$(BIN_DIR)/img2sysfont$(EXE_EXT): tools/img2sysfont.c | $(BIN_DIR)
	@$(CC) $(CFLAGS) -o $@ tools/img2sysfont.c

$(BIN_DIR)/isoextract$(EXE_EXT): tools/isoextract.c | $(BIN_DIR)
	@$(CC) $(CFLAGS) -o $@ tools/isoextract.c

# imgdiff is standalone (stb only); it must NOT link the renderer, which also
# defines STB_IMAGE_IMPLEMENTATION (duplicate symbols otherwise).
$(BIN_DIR)/imgdiff$(EXE_EXT): tools/imgdiff.c renderer/stb/stb_image.h renderer/stb/stb_image_write.h | $(BIN_DIR)
	@echo "[imgdiff]"
	@$(CC) $(CFLAGS) -o $@ tools/imgdiff.c -lm

$(FONT_HEADER): $(FONT_SRC) $(BIN_DIR)/img2sysfont$(EXE_EXT)
	@$(BIN_DIR)/img2sysfont$(EXE_EXT) $(FONT_SRC) $(FONT_HEADER) $(FONT_SYMBOL)

$(eval $(call unity_lib_schema,$(RENDERER_WOW_LIB),$(RENDERER_BASE_DEPS) $(call CSRC,renderer $(WOW_DIR)/renderer),renderer-wow,renderer $(WOW_DIR)/renderer,,$(WOW_CFLAGS),common/mpq.c,$(RENDERER_SHARED_LIBS)))
$(eval $(call unity_lib_schema,$(RENDERER_SC2_LIB),$(RENDERER_BASE_DEPS) $(call CSRC,renderer $(SC2_DIR)/renderer) $(SC2_COMMON_SRCS),renderer-sc2,renderer $(SC2_DIR)/renderer,,$(SC2_CFLAGS),common/mpq.c,$(RENDERER_SHARED_LIBS) $(SC2_XML_LIBS)))

$(eval $(call unity_lib_schema,$(GAME_WOW_LIB),$(GAME_BASE_DEPS) common/world.c $(WOW_COMMON_SRCS) $(call CSRC,$(WOW_DIR)/game),game-wow,$(WOW_DIR)/game,,$(WOW_CFLAGS),common/mpq.c,-lshared $(LIBS) -lm -lz))
$(eval $(call unity_lib_schema,$(GAME_SC2_LIB),$(GAME_BASE_DEPS) $(WORLD_CORE_SRCS) $(SC2_COMMON_SRCS) $(call CSRC,$(SC2_DIR)/game),game-sc2,$(SC2_DIR)/game,,$(SC2_CFLAGS),common/mpq.c,-lshared $(LIBS) -lm -lz $(SC2_XML_LIBS)))

$(eval $(call unity_lib_schema,$(UI_WOW_LIB),$(UI_BASE_DEPS) client/ui.h $(call CSRC,$(WOW_DIR)/ui),ui-wow,$(WOW_DIR)/ui,,$(WOW_UI_CFLAGS),,-lshared $(LUA_LIBS) $(WOW_XML_LIBS)))

$(eval $(call unity_lib_schema,$(UI_SC2_LIB),$(UI_BASE_DEPS) client/ui.h $(call CSRC,$(SC2_DIR)/ui),ui-sc2,$(SC2_DIR)/ui,,$(SC2_UI_CFLAGS),,$(SC2_UI_LIBS)))

$(eval $(call app_schema,$(WOW_BINARY),$(SHARED_LIB) $(SHEET_LIB) $(GAME_WOW_LIB) $(RENDERER_WOW_LIB) $(UI_WOW_LIB) $(APP_SRCS) $(CLIENT_HEADERS) $(COMMON_HEADERS),openwow,$(WOW_CFLAGS),-lsheet -lshared -lgame-wow -lrenderer-wow -lui-wow $(LIBS) -lz))
$(eval $(call app_schema,$(SC2_BINARY),$(SHARED_LIB) $(SHEET_LIB) $(GAME_SC2_LIB) $(RENDERER_SC2_LIB) $(UI_SC2_LIB) $(APP_SRCS) $(CLIENT_HEADERS),opensc2,$(SC2_CFLAGS),-lsheet -lshared -lgame-sc2 -lrenderer-sc2 -lui-sc2 $(LIBS) -lz))

download: $(ZIP_FILE)
	mkdir -p $(DATA_DIR)
	unzip -o $(ZIP_FILE) -d $(DATA_DIR)

$(ZIP_FILE):
	curl -L -o $(ZIP_FILE) $(ZIP_URL)

clean:
	rm -rf build

$(eval $(call test_schema,test-wow-appearance,,$(WOW_TEST_CFLAGS),$(BIN_DIR)/test_wow_appearance$(EXE_EXT),$(WOW_TEST_DIR)/test_wow_appearance.c common/msg.c common/net.c $(call CSRC,shared),-lm,))
$(eval $(call test_schema,test-wow-combat,,$(WOW_TEST_CFLAGS),$(BIN_DIR)/test_wow_combat$(EXE_EXT),$(WOW_TEST_DIR)/test_wow_combat.c $(WOW_DIR)/game/g_ai.c $(call CSRC,shared),-lm,))
$(eval $(call test_schema,test-wow-game,,$(WOW_TEST_CFLAGS),$(BIN_DIR)/test_wow_game$(EXE_EXT),$(WOW_TEST_DIR)/test_wow_game.c $(WOW_DIR)/game/g_wow.c $(WOW_DIR)/game/g_world.c $(WOW_DIR)/game/g_ai.c $(WOW_DIR)/game/m_creature.c common/mpq.c $(call CSRC,shared),-lm -lz,))
$(eval $(call test_schema,test-wow-ui,test-wow-assets,$(WOW_UI_TEST_CFLAGS),$(BIN_DIR)/test_wow_ui$(EXE_EXT),$(WOW_TEST_DIR)/test_wow_ui.c $(WOW_DIR)/ui/ui_main.c $(WOW_DIR)/ui/ui_lua.c $(WOW_DIR)/ui/ui_dbc.c $(WOW_DIR)/ui/ui_loading.c $(WOW_DIR)/ui/ui_xml.c common/mpq.c,-lshared $(LUA_LIBS) $(WOW_XML_LIBS) -lz,))
$(eval $(call test_schema,test-sc2,test-sc2-assets $(SHARED_LIB) $(SHEET_LIB),$(SC2_TEST_CFLAGS),$(BIN_DIR)/test_sc2$(EXE_EXT),$(SC2_TEST_DIR)/test_sc2_main.c $(SC2_TEST_DIR)/test_sc2_map.c $(SC2_TEST_DIR)/test_sc2_layout.c $(SC2_DIR)/common/sc2_map.c $(SC2_DIR)/ui/sc2_layout.c common/common.c common/cmd.c common/cvar.c common/msg.c common/net.c common/mpq.c,-lsheet -lshared -lm -lz $(SC2_XML_LIBS),))

test-sc2-assets: sc2fixturegen mpqtool sc2map | $(TESTS_DIR)
	@echo "[test-sc2-assets] generating SC2 terrain fixtures"
	@mkdir -p $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) map-info $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/MapInfo
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) height-map $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/t3HeightMap
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) sync-height-map $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/t3SyncHeightMap
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) cell-flags $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/t3CellFlags
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) cliff-levels $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/t3SyncCliffLevel
	@$(BIN_DIR)/sc2fixturegen$(EXE_EXT) texture-masks $(SC2_TEST_RES_DIR)/Maps/Test/Tiny.SC2Map/t3TextureMasks
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
	@$(BIN_DIR)/sc2map$(EXE_EXT) -mpq $(SC2_TEST_MPQ) Maps/Test/Tiny.SC2Map | grep -q "Objects: units=2 doodads=2 points=1 cameras=1 total=6" && echo "  sc2map diag OK"

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

.PHONY: default build shared tools font $(TOOL_NAMES) diag clean download renderer-wow game-wow ui-wow openwow renderer-sc2 game-sc2 opensc2 run run-sc2 build-run-sc2 m2tool-wow-orcmale-player install-wow test-wow-appearance test-wow-combat test-wow-game test-wow-ui test-wow-assets test-sc2 test-sc2-assets $(WC3_PHONY)
