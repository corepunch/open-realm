# Vendored Dependencies

OpenWarcraft compiles its own Lua and XML parser from checked-in sources, matching Quake 3's `code/jpeg-6/` pattern. No `pkg-config`, system `liblua5.4-dev`, or `libxml2-dev` is required.

## Lua 5.4 (`vendor/lua/`)

- Source: `vendor/lua/src/` — a verbatim copy of Lua 5.4.8 (`vendor/lua/LICENSE`, `vendor/lua/README`).
- Built as a static archive `build/lib/liblua.a` from the unity rule in the top-level `Makefile` (search `LUA_LIB`).
- Only the VM library is compiled; the interpreter/compiler drivers `lua.c` and `luac.c` are excluded.
- Variables defined in the top-level `Makefile`:
  - `LUA_DIR` / `LUA_SRCS` / `LUA_OBJ` / `LUA_LIB` / `LUA_CFLAGS` (`-Ivendor/lua/src`).
- Only the WoW UI links it: `games/world-of-warcraft/game.mk` sets `LUA_LIBS := -llua` and lists `$(LUA_LIB)` as a prerequisite of `libmenu-wow`, `test-wow-menu`, and `test-wow-hud-xml`.
- To upgrade: replace the files under `vendor/lua/src/` (and `README`/`LICENSE`); the Makefile wildcard picks up the new set automatically.

## XML parser (`common/tinyxml.h`)

- A self-contained, header-only DOM parser with a libxml2-compatible subset of the API. It replaced `libxml2` for the WoW FrameXML parser (`stb_wowxml.h`, `ui_xml.c`), the SC2 layout parser (`stb_sc2layout.h`), and the SC2 map/catalog parser (`sc2_map.c`).
- API surface provided: `xmlDocPtr`/`xmlNodePtr`/`xmlAttrPtr`, `xmlReadMemory`, `xmlParseMemory`, `xmlDocGetRootElement`, `xmlGetProp`, `xmlNodeGetContent`, `xmlNodeListGetString`, `xmlStrcasecmp`, `XML_ELEMENT_NODE`, `XML_PARSE_*`, `BAD_CAST`.
- Nodes keep libxml2's linked-list layout (`children`/`next` for siblings, `properties`/`next` for attributes, an attribute's value stored as a text node under `attr->children`), so the migrated call sites kept their original traversal loops.
- All strings/nodes come from a per-document **chunked arena** (linked list of blocks, never `realloc`'d in place) so tree pointers stay valid across growth; `xmlFreeDoc` frees the arena and `xmlFree` is a no-op.
- Parsing is lenient (equivalent to `XML_PARSE_RECOVER`): comments, processing instructions, and DOCTYPE are skipped; CDATA becomes text; the five standard entities and numeric character references are decoded.
- Because it is header-only with `static` functions, the include guard prevents redefinition inside unity builds; every consumer compiles with `-Wno-unused-function`.
