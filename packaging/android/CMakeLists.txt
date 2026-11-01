cmake_minimum_required(VERSION 3.18)
project(openrealm_probe C)

if(NOT ANDROID OR NOT IS_DIRECTORY "${SDL_SOURCE_DIR}/include")
    message(FATAL_ERROR "Use the Android NDK toolchain and set SDL_SOURCE_DIR to SDL2 sources")
endif()
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/sdl")

get_filename_component(REALM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
# Existing engine headers use <SDL2/SDL.h>; SDL's build exports <SDL.h>.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include")
file(CREATE_LINK "${SDL_SOURCE_DIR}/include" "${CMAKE_BINARY_DIR}/include/SDL2" SYMBOLIC)
add_library(main SHARED probe.c "${REALM_ROOT}/renderer/r_shader.c" "${REALM_ROOT}/shared/source/matrix4.c")
target_include_directories(main PRIVATE "${REALM_ROOT}" "${REALM_ROOT}/shared" "${REALM_ROOT}/shared/types"
    "${CMAKE_BINARY_DIR}/include")
target_compile_definitions(main PRIVATE BZ_GL_ES3 USE_FOGOFWAR)
# Retain production descriptors/generator without linking unrelated renderer runtime globals.
target_compile_options(main PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter -fvisibility=hidden
    -ffunction-sections -fdata-sections)
target_link_options(main PRIVATE -Wl,--gc-sections -Wl,--no-undefined)
target_link_libraries(main PRIVATE SDL2::SDL2 GLESv3 log m)

option(BZ_ANDROID_GAME "Build the Warcraft III runtime instead of the shader probe" OFF)
if(BZ_ANDROID_GAME)
    # ELF binding keeps game/menu private FDF registries and world globals independent,
    # just as the desktop DLLs and iPad frameworks do.
    function(realm_module name)
        set(files ${ARGN})
        list(SORT files)
        set(unit "${CMAKE_BINARY_DIR}/${name}_unity.c")
        set(body "/* Generated Android module compilation unit. */\n")
        foreach(src IN LISTS files)
            string(APPEND body "#include \"${src}\"\n")
        endforeach()
        file(CONFIGURE OUTPUT "${unit}" CONTENT "${body}" @ONLY)
        add_library(${name} SHARED "${unit}")
        target_include_directories(${name} PRIVATE "${REALM_ROOT}" "${REALM_ROOT}/shared"
            "${REALM_ROOT}/shared/types" "${CMAKE_BINARY_DIR}/include"
            "${REALM_ROOT}/games/warcraft-3" "${REALM_ROOT}/games/warcraft-3/common"
            "${REALM_ROOT}/games/warcraft-3/game" "${REALM_ROOT}/games/warcraft-3/game/api"
            "${REALM_ROOT}/games/warcraft-3/game/skills")
        # Desktop release keeps assertions (including the JASS loop bound); CMake release adds NDEBUG.
        target_compile_options(${name} PRIVATE -Wall -Wmisleading-indentation -fno-common -UNDEBUG)
        target_link_options(${name} PRIVATE -Wl,-Bsymbolic -Wl,--no-undefined)
        target_compile_definitions(${name} PRIVATE WC3 USE_FOGOFWAR BZ_GL_ES3 BZ_MSAA_SAMPLES=0 BZ_GAME="warcraft-3")
    endfunction()
    file(GLOB_RECURSE shared CONFIGURE_DEPENDS "${REALM_ROOT}/shared/*.c")
    list(REMOVE_ITEM shared "${REALM_ROOT}/shared/test.c")
    realm_module(realm_shared ${shared} "${REALM_ROOT}/vendor/blast/blast.c")
    target_sources(realm_shared PRIVATE "${REALM_ROOT}/shared/test.c")
    target_link_libraries(realm_shared PRIVATE SDL2::SDL2 z m)
    file(GLOB_RECURSE jass CONFIGURE_DEPENDS "${REALM_ROOT}/games/warcraft-3/jass/*.c")
    realm_module(realm_jass ${jass})
    target_link_libraries(realm_jass PRIVATE realm_shared m)
    realm_module(realm_sheet "${REALM_ROOT}/games/warcraft-3/sheet/parser.c" "${REALM_ROOT}/games/warcraft-3/sheet/sheet.c")
    file(GLOB_RECURSE render CONFIGURE_DEPENDS "${REALM_ROOT}/renderer/*.c" "${REALM_ROOT}/games/warcraft-3/renderer/*.c")
    realm_module(realm_renderer ${render} "${REALM_ROOT}/common/mpq.c")
    target_link_libraries(realm_renderer PRIVATE realm_shared realm_sheet SDL2::SDL2 GLESv3 z m)
    file(GLOB_RECURSE common CONFIGURE_DEPENDS "${REALM_ROOT}/games/warcraft-3/common/*.c")
    list(FILTER common EXCLUDE REGEX "/world_w3\\.c$")
    foreach(mod game menu)
        file(GLOB_RECURSE files CONFIGURE_DEPENDS "${REALM_ROOT}/games/warcraft-3/${mod}/*.c")
        realm_module(realm_${mod} ${files} ${common} "${REALM_ROOT}/common/mpq.c")
        target_compile_definitions(realm_${mod} PRIVATE STB_FDF_IMPLEMENTATION STB_FDF_GLOBALS)
        target_link_libraries(realm_${mod} PRIVATE realm_shared realm_sheet SDL2::SDL2 z m)
    endforeach()
    target_link_libraries(realm_game PRIVATE realm_jass)
    # The debug APK can exercise real native startup without retail data. Tests stay in an opt-in library.
    add_library(realm_smoke SHARED "${REALM_ROOT}/games/warcraft-3/game/tests/t_smoke.c")
    target_include_directories(realm_smoke PRIVATE "${REALM_ROOT}" "${REALM_ROOT}/shared" "${REALM_ROOT}/shared/types"
        "${REALM_ROOT}/games/warcraft-3" "${REALM_ROOT}/games/warcraft-3/game")
    target_compile_definitions(realm_smoke PRIVATE BZ_TESTS WC3)
    target_link_libraries(realm_smoke PRIVATE realm_game realm_shared realm_jass)
    file(GLOB_RECURSE engine CONFIGURE_DEPENDS "${REALM_ROOT}/client/*.c" "${REALM_ROOT}/server/*.c"
        "${REALM_ROOT}/common/*.c" "${REALM_ROOT}/sound/*.c")
    list(FILTER engine EXCLUDE REGEX "/(stb_vorbis|sv_routing)\\.c$")
    realm_module(warcraft3 ${engine} "${REALM_ROOT}/games/warcraft-3/common/world_w3.c")
    target_compile_definitions(warcraft3 PRIVATE STB_FDF_IMPLEMENTATION STB_FDF_GLOBALS BZ_CLIENT_WORLD BZ_MUSIC_MP3)
    target_link_libraries(warcraft3 PRIVATE realm_game realm_menu realm_renderer realm_shared realm_sheet realm_jass SDL2::SDL2 z m log)
    add_dependencies(warcraft3 realm_smoke)
    add_library(mpqcheck SHARED archive_check.c "${REALM_ROOT}/common/mpq.c" "${REALM_ROOT}/vendor/blast/blast.c")
    target_include_directories(mpqcheck PRIVATE "${REALM_ROOT}" "${REALM_ROOT}/shared" "${REALM_ROOT}/shared/types")
    target_link_libraries(mpqcheck PRIVATE z)
endif()
