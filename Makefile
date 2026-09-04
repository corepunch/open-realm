CC      := gcc
BIN_DIR := build/bin
LIB_DIR := build/lib
SHARE_INSTALL := build/share
CFLAGS  := -Wall -Wmisleading-indentation -fno-common -I. -Ishared -Ishared/types
BUILD   ?= debug
MSAA    ?= 0
GL_BACKEND ?= gl
GLSL      ?= 140

ifeq ($(BUILD),release)
	CFLAGS += -O2
else ifneq ($(BUILD),debug)
	$(error BUILD must be debug or release)
else
	CFLAGS += -O0 -g
endif
ifeq ($(filter $(MSAA),0 2 4 8),)
	$(error MSAA must be 0, 2, 4, or 8)
endif
ifeq ($(filter $(GLSL),120 140 150),)
	$(error GLSL must be 120, 140, or 150)
endif
ifeq ($(GL_BACKEND),gles3)
	CFLAGS += -DBZ_GL_ES3
else ifeq ($(GL_BACKEND),gl)
	ifeq ($(GLSL),120)
		CFLAGS += -DBZ_GLSL_120
	else ifeq ($(GLSL),150)
		CFLAGS += -DBZ_GLSL_150
	endif
else
	$(error GL_BACKEND must be gl or gles3)
endif
CFLAGS += -DBZ_MSAA_SAMPLES=$(MSAA)
ifneq ($(MSAA),0)
	CFLAGS += -DBZ_USE_MSAA
endif

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
	ifeq ($(GL_BACKEND),gles3)
		$(error GL_BACKEND=gles3 is currently supported on Linux only)
	endif
    LIB_EXT   := .dll
    LIB_FLAGS := -shared
    EXE_EXT   := .exe
    INSTALL_NAME =
    RPATH     :=
    LIB_RPATH :=
    LDFLAGS   := -L$(LIB_DIR)
	LIBS      := -lmingw32 -mwindows -lSDL2main -lSDL2 -lm -lepoxy -lopengl32 -lgdi32 -lws2_32
	NET_LIBS  := -lws2_32
else
    UNAME_S := $(shell uname -s)
    EXE_EXT :=
    ifeq ($(UNAME_S),Darwin)
		ifeq ($(GL_BACKEND),gles3)
			$(error GL_BACKEND=gles3 is currently supported on Linux only)
		endif
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
        LIB_RPATH := -Wl,-rpath,@loader_path
		CFLAGS    += -DGL_SILENCE_DEPRECATION -I$(HOMEBREW_PREFIX)/include -arch $(ARCH)
		LDFLAGS   := -L$(LIB_DIR) -L$(HOMEBREW_PREFIX)/lib -arch $(ARCH)
		LIBS      := -lSDL2 -framework AppKit -framework OpenGL
		NET_LIBS  :=
    else ifeq ($(UNAME_S),Linux)
        LIB_EXT   := .so
        LIB_FLAGS := -shared -fPIC
        INSTALL_NAME =
        # Executables resolve direct shared-lib deps via $ORIGIN/../lib. Shared
        # libraries need their own RUNPATH ($ORIGIN == this lib's dir) because
        # glibc does NOT inherit the executable's RUNPATH when resolving a
        # library's transitive deps (e.g. libgame.so -> libjass.so).
        RPATH     := -Wl,-rpath,'$$ORIGIN/../lib'
        LIB_RPATH := -Wl,-rpath,'$$ORIGIN'
        CFLAGS    += -fPIC
        LDFLAGS   := -L$(LIB_DIR) -Wl,-z,defs
		ifeq ($(GL_BACKEND),gles3)
			LIBS      := -lSDL2 -lEGL -lGLESv2 -lm
		else
			LIBS      := -lSDL2 -lEGL -lGL -lm
		endif
		NET_LIBS  :=
    else ifeq ($(UNAME_S),OpenBSD)
		ifeq ($(GL_BACKEND),gles3)
			$(error GL_BACKEND=gles3 is currently supported on Linux only)
		endif
        # BSD (OpenBSD): /usr/X11R6 for Mesa GL, clang as default CC
        ifeq ($(filter command line environment,$(origin CC)),)
            CC := clang
        else
        endif
        LIB_EXT   := .so
        LIB_FLAGS := -shared -fPIC
        INSTALL_NAME =
        # $ORIGIN doesn't work in RPATH/RUNPATH on OpenBSD
        RPATH     := -Wl,-rpath,$(abspath $(LIB_DIR))
        LIB_RPATH := -Wl,-rpath,$(abspath $(LIB_DIR))
        CFLAGS    += -fPIC -I/usr/local/include -I/usr/X11R6/include
        LDFLAGS   := -L$(LIB_DIR) -L/usr/local/lib -L/usr/X11R6/lib
		LIBS      := -lSDL2 -lGL -lm
		NET_LIBS  :=
    endif
endif

SHARED_LIB := $(LIB_DIR)/libshared$(LIB_EXT)

# Vendored Lua 5.4 (checked in under vendor/lua, compiled like Quake 3's jpeg).
# The interpreter/compiler drivers (lua.c, luac.c) are excluded; only the VM
# library is built, into a static archive linked by the WoW UI module.
LUA_DIR    := vendor/lua/src
LUA_SRCS   := $(filter-out $(LUA_DIR)/lua.c $(LUA_DIR)/luac.c, $(wildcard $(LUA_DIR)/*.c))
LUA_OBJ    := $(LIB_DIR)/lua.o
LUA_LIB    := $(LIB_DIR)/liblua.a
LUA_CFLAGS := -I$(LUA_DIR)

$(LUA_LIB): $(LUA_SRCS) $(wildcard $(LUA_DIR)/*.h) | $(LIB_DIR)
	@echo "[lua]"
	@$(call UNITY,$(LUA_DIR),! -name 'lua.c' ! -name 'luac.c') | \
		$(CC) $(CFLAGS) $(LUA_CFLAGS) -c -x c -o $(LUA_OBJ) -
	@ar rcs $@ $(LUA_OBJ)

TOOL_SRCS := $(shell find tools -maxdepth 1 -name '*.c' ! -name 'jass.c' | sort)
TOOL_NAMES := $(patsubst tools/%.c,%,$(TOOL_SRCS))
TOOL_BINS := $(addprefix $(BIN_DIR)/,$(addsuffix $(EXE_EXT),$(TOOL_NAMES)))
TOOL_DEPS := $(shell find tools -maxdepth 1 -name '*.h' | sort) common/mpq.c common/mpq.h
CLIENT_HEADERS := $(shell find client -name '*.h' | sort)
COMMON_HEADERS := $(shell find common -name '*.h' | sort)
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
COMMON_SRCS      := $(filter-out common/main.c, $(call CSRC,common))
COMMON_GAME_SRCS := common/mpq.c

define unity_lib_schema
$(1): $(2) | $$(LIB_DIR)
	@echo "[$(3)]"
	@$$(call UNITY,$(4),$(5)) | \
		$$(CC) $(6) $$(LIB_FLAGS) $$(INSTALL_NAME) $$(LIB_RPATH) -x c -o $$@ - $(7) $$(LDFLAGS) $(8)
endef

define src_lib_schema
$(1): $(2) | $$(LIB_DIR)
	@echo "[$(3)]"
	@$$(CC) $(4) $$(LIB_FLAGS) $$(INSTALL_NAME) $$(LIB_RPATH) -x c -o $$@ $(5) $$(LDFLAGS) $(6)
endef

define app_schema
$(1): $(2) | $$(BIN_DIR) install-share
	@echo "[$(3)]"
	@$$(call UNITY,client server common sound,! -name 'stb_vorbis.c') | \
		$$(CC) $(4) -x c -o $$@ - $$(RPATH) $$(LDFLAGS) $(5)
endef

define test_schema

$(4): $(2) $(5) | $$(BIN_DIR)
	@$$(CC) $(3) -o $(4) $(5) $$(RPATH) $$(LDFLAGS) $(6)

$(1): $(4)
	@$(4) $(7)
endef

default: build
build: install-share
shared:      $(SHARED_LIB)

# Install project-owned read-only assets (engine fonts + per-game defaults) into
# the FHS-style build tree: build/bin/<exe>, build/lib/, build/share/<game>/.
# Mirrors the flat layout of a deployed portable app (share/ beside the exe).
install-share:
	@mkdir -p $(SHARE_INSTALL)
	@cp -R share/. $(SHARE_INSTALL)/
	@for g in warcraft-3 world-of-warcraft starcraft-2; do \
		if [ -d games/$$g/share ]; then \
			mkdir -p $(SHARE_INSTALL)/$$g; \
			cp -R games/$$g/share/. $(SHARE_INSTALL)/$$g/; \
		fi; \
	done
	@echo "[install-share]"
tools:       $(TOOL_BINS)
	@echo "[tools]"
font:        $(FONT_HEADER)
$(TOOL_NAMES): %: $(BIN_DIR)/%$(EXE_EXT)

diag: clean
	$(MAKE) DIAG_OUTPUT=1 build
	$(MAKE) DIAG_OUTPUT=1 run

$(BIN_DIR) $(LIB_DIR):
	@mkdir -p $@

APP_SRCS          := $(shell find client server common sound -name '*.c')
RENDERER_BASE_DEPS  := $(SHARED_LIB) $(CLIENT_HEADERS) $(COMMON_HEADERS) $(COMMON_SRCS) $(FONT_HEADER)
RENDERER_SHARED_LIBS := -lshared $(LIBS) -lz
SERVER_GAME_SRCS  := server/sv_quest.c
GAME_BASE_DEPS    := $(SHARED_LIB) $(COMMON_HEADERS) $(COMMON_SRCS) $(SERVER_GAME_SRCS)
UI_BASE_DEPS      := $(SHARED_LIB) $(CLIENT_HEADERS) $(COMMON_HEADERS)

$(eval $(call unity_lib_schema,$(SHARED_LIB),$(call CSRC,shared),shared,shared,,$(CFLAGS),,-lm))

TESTS_DIR := build/tests

include games/warcraft-3/game.mk
include games/world-of-warcraft/game.mk
include games/starcraft-2/game.mk

$(BIN_DIR)/%$(EXE_EXT): tools/%.c $(TOOL_DEPS) $(CLIENT_HEADERS) $(COMMON_HEADERS) | $(BIN_DIR) $(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB) $(RENDERER_LIB) $(GAME_LIB) $(MENU_LIB)
	@$(CC) $(CFLAGS) -o $@ $< \
		$(RPATH) $(LDFLAGS) -lsheet -lshared -ljass -lrenderer -lgame -lmenu $(LIBS) -lm -lz

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

clean:
	rm -rf build

.PHONY: default build shared tools font $(TOOL_NAMES) diag clean install-share $(WC3_PHONY) $(WOW_PHONY) $(SC2_PHONY)
