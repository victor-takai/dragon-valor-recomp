cmake_minimum_required(VERSION 3.20)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to psxrecomp")
if(NOT EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    message(FATAL_ERROR
        "PSXRecomp runtime is missing: ${PSXRECOMP_ROOT}/runtime/runtime.cmake\n"
        "This project expects the psxrecomp framework checkout at ${PSXRECOMP_ROOT}.\n"
        "Run: git submodule update --init --recursive\n"
        "If psxrecomp/ is not a submodule, set -DPSXRECOMP_ROOT=<path-to-psxrecomp>.")
endif()

project(DragonValorRecomp LANGUAGES C CXX)

# Local patches to the submodules (patches/README.md): applied with git apply
# at configure time when not applied yet, so clones and CI build the same code.
foreach(_dv_sub recomp-ui)
    file(GLOB _dv_patches "${CMAKE_CURRENT_SOURCE_DIR}/patches/${_dv_sub}/*.patch")
    foreach(_dv_patch ${_dv_patches})
        execute_process(COMMAND git apply --check --reverse "${_dv_patch}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dv_sub}"
            RESULT_VARIABLE _dv_applied OUTPUT_QUIET ERROR_QUIET)
        if(NOT _dv_applied EQUAL 0)
            execute_process(COMMAND git apply "${_dv_patch}"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dv_sub}"
                RESULT_VARIABLE _dv_rc)
            if(NOT _dv_rc EQUAL 0)
                message(FATAL_ERROR "patches/${_dv_sub}: ${_dv_patch} does not apply to the pinned ${_dv_sub}")
            endif()
            message(STATUS "${_dv_sub}: applied ${_dv_patch}")
        endif()
    endforeach()
endforeach()

# Opt-in product surfaces: PSX_RECOMP_UI is ON (recomp-ui submodule present);
# netplay (PSX_NETPLAY) and the first-run setup wizard (PSX_SETUP_WIZARD) stay
# OFF until tested -- see psxrecomp/runtime/runtime.cmake for the options.
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

# Box art for the launcher (psxrecomp/tools/new_project_layout/fetch_boxart.py writes the
# TGA). Optional: the launcher runs without it.
set(DV_BOXART "${CMAKE_CURRENT_SOURCE_DIR}/launcher_assets/img/boxart.tga")
if(EXISTS "${DV_BOXART}")
    set(DV_BOXART_ARG LAUNCHER_BOXART "${DV_BOXART}")
else()
    set(DV_BOXART_ARG "")
    message(STATUS "Dragon Valor: no launcher boxart at ${DV_BOXART}")
endif()

# This project's own mod catalog. The FRAMEWORK stages it -- together with its
# builtin psx.* packages -- into <exe-dir>/mods/bundled, and refuses to
# configure if a catalog exists here but is not declared. Never copy it by
# hand: five titles did, and framework 4cc04be3's rename of the staged
# directory broke all five silently (bead beads-eio.3.101).
set(PSX_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
if(NOT IS_DIRECTORY "${PSX_PRELOADED_MODS}")
    set(PSX_PRELOADED_MODS "NONE")
endif()

psxrecomp_add_game_runtime(psx-runtime
    PRELOADED_MODS_DIR "${PSX_PRELOADED_MODS}"
    # The generic loading detector is deprecated by its own manifest; CD Speed
    # is the supported accelerator (psxrecomp/docs/MOD_PACKAGES.md).
    EXCLUDE_BUILTIN_MODS psx.enhancement.fast-loading
    # PGXP hook flavour: the one runtime tracks GTE precision through every
    # vertex path, so the psx.enhancement.pgxp mod actually corrects this 3D
    # title (the base flavour only shadows swc2 and corrects almost nothing).
    # PGXP stays off unless the mod arms it; save states are flavour-specific
    # (tests/states was re-taken on this flavour). psxrecomp/docs/ENHANCEMENTS.md G1.11.
    PGXP
    # ENABLE_NETPLAY_IF_PRESENT
    # NETPLAY_LOBBY_URL ws://netplay.retcomm.net:8765
    # ENABLE_SETUP_WIZARD
    WINDOW_TITLE "Dragon Valor Recompiled"
    # Without it the exe would be Dragon_Valor_Recompiled (MAKE_C_IDENTIFIER of
    # the title); Play.bat, tools/dvdbg.py, tests/test_menu.py,
    # scripts/package_release.sh and codegen_setup.c name it too.
    EXE_NAME DragonValorRecompiled
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    MAX_PLAYERS 1
    ${DV_BOXART_ARG}
    APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/psxrecomp.ico"
    # Committed game C (psxrecomp_cli.py generate; git add generated). Release
    # CI configures with -DPSXRECOMP_REQUIRE_GAME_C=ON so a checkout without
    # it fails instead of building a setup host.
    # Build-time static overlay shard for the BIN/*.BIN mode modules (profile:
    # aot/overlays.json, written by tools/aot_overlay_pipeline.py `static` or by
    # psxrecomp_cli.py generate). Must name the profile's static_output and stay
    # BEFORE GEN_FULL_GLOB (a multi-value arg would swallow it).
    GAME_OVERLAY_STATIC_C "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c"
    GEN_MARKER "generated/SLUS_010.92_dispatch.c"
    GEN_FULL_GLOB "generated/SLUS_010.92_full_*.c"
    CODEGEN_SETUP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/codegen_setup.c"
)

# Trusted static plugins selected by this project's mod packages: the
# [[plugin]] ids in mods/preloaded/packages/*/manifest.toml resolve to
# implementations linked here (a package never carries native code). See
# psxrecomp/docs/MOD_PACKAGES.md, "Trusted static plugins".
target_sources(psx-runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/plugins/dragonvalor_cheats.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/plugins/dragonvalor_skip_intro.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/plugins/dragonvalor_widescreen.c"
    # In-game menu provider (psx_menu_host.h + recomp-ui's runtime UI); it
    # compiles to nothing when the recomp-ui launcher is off.
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/plugins/dragonvalor_menu.cpp")

# Stage runtime configs next to the exe for direct build/ launches. Only copy
# files that exist — never fail when game_options.toml is absent.
#
# Done by a dedicated ALL target instead of a POST_BUILD step on psx-runtime: a
# POST_BUILD step runs only when the exe is relinked, so a config-only edit was
# never re-staged and the runtime silently kept the old game.toml.
add_custom_target(psx-stage-configs ALL)
add_dependencies(psx-runtime psx-stage-configs)
foreach(_PSX_RUNTIME_CONFIG game.toml game_options.toml)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}")
        add_custom_command(TARGET psx-stage-configs POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}"
                "$<TARGET_FILE_DIR:psx-runtime>/${_PSX_RUNTIME_CONFIG}"
            COMMENT "Staging ${_PSX_RUNTIME_CONFIG}"
            VERBATIM)
    endif()
endforeach()
