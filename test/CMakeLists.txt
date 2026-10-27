# Wraps the core objects compiled once in src/lotus_core_objects for the headless tests.
add_library(lotus_test_core SHARED
    $<TARGET_OBJECTS:lotus_core_objects>
)
target_include_directories(lotus_test_core PUBLIC
    ${PROJECT_BINARY_DIR}
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(lotus_test_core PUBLIC
    lotus_common_executable_path
    Fcitx5::Core
    Fcitx5::Config
    Fcitx5::Module::Emoji
    Lotus::Engine
    PkgConfig::LIBNOTIFY
    Pthread::Pthread
)

# The headless tests compile without PIC while lotus_core_objects builds with
# -fPIC for the module, and clang rejects a reused PCH whose PIC setting
# differs from the current file. Host the tests' PCH in an object library
# whose flags match plain executables instead.
add_library(lotus_test_pch OBJECT EXCLUDE_FROM_ALL lotus-test-pch.cpp)
target_include_directories(lotus_test_pch PRIVATE
    ${PROJECT_BINARY_DIR}
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(lotus_test_pch PRIVATE
    lotus_test_core
    Fcitx5::Core
    Fcitx5::Config
    Fcitx5::Module::Emoji
    Pthread::Pthread
)
target_compile_definitions(lotus_test_pch PRIVATE
    FCITX5_LOTUS_SETTINGS_PATH="${CMAKE_INSTALL_FULL_BINDIR}/fcitx5-lotus-settings"
    LOTUS_SERVER_EXECUTABLE="${LOTUS_SERVER_EXECUTABLE}"
    LOTUS_ALT_EXECUTABLE_PREFIX="${LOTUS_ALT_EXECUTABLE_PREFIX}"
)
target_precompile_headers(lotus_test_pch PRIVATE ${LOTUS_PCH_HEADERS})

# Helper to compile a standalone headless integration test linked with the
# Lotus engine core.
#
# Usage:
#   add_lotus_headless_test(TARGET <target_name> SOURCES <source_files...> LABELS "<labels>" [INCLUDE_DIRECTORIES <dirs...>] [LINK_LIBRARIES <libs...>])
# Example:
#   add_lotus_headless_test(TARGET my_test SOURCES my-test.cpp LABELS "integration;myfeature")
function(add_lotus_headless_test)
    set(options)
    set(oneValueArgs TARGET)
    set(multiValueArgs SOURCES INCLUDE_DIRECTORIES LINK_LIBRARIES LABELS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "add_lotus_headless_test: Unrecognized arguments: '${ARG_UNPARSED_ARGUMENTS}'")
    endif()

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "add_lotus_headless_test: Missing TARGET!")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "add_lotus_headless_test: Target '${ARG_TARGET}' is missing SOURCES!")
    endif()

    add_executable(${ARG_TARGET} ${ARG_SOURCES})
    # lotus_test_pch mirrors the executables' flags, so its prebuilt PCH is
    # reused, not rejected.
    target_compile_definitions(${ARG_TARGET} PRIVATE
        FCITX5_LOTUS_SETTINGS_PATH="${CMAKE_INSTALL_FULL_BINDIR}/fcitx5-lotus-settings"
        LOTUS_SERVER_EXECUTABLE="${LOTUS_SERVER_EXECUTABLE}"
        LOTUS_ALT_EXECUTABLE_PREFIX="${LOTUS_ALT_EXECUTABLE_PREFIX}"
    )
    target_precompile_headers(${ARG_TARGET} REUSE_FROM lotus_test_pch)
    target_link_libraries(${ARG_TARGET} PRIVATE
        lotus_test_core
        Fcitx5::Core
        Fcitx5::Config
        Fcitx5::Module::Emoji
        Pthread::Pthread
        ${ARG_LINK_LIBRARIES}
    )
    if(ARG_INCLUDE_DIRECTORIES)
        target_include_directories(${ARG_TARGET} PRIVATE
            ${ARG_INCLUDE_DIRECTORIES}
        )
    endif()
    add_test(NAME ${ARG_TARGET} COMMAND ${ARG_TARGET})
    set_tests_properties(${ARG_TARGET} PROPERTIES LABELS "${ARG_LABELS}" TIMEOUT 20)
endfunction()

# One binary runs every engine unit test group via "--run <fn>"; ctest keeps
# one entry per group (own process), so labels, timeouts and isolation hold.
#
# Usage:
#   add_lotus_unit_test(<ctest_name> <run_function> <source_file> "<labels>")
function(add_lotus_unit_test name fn source labels)
    target_sources(lotus_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${source})
    add_test(NAME ${name} COMMAND lotus_unit_tests --run ${fn})
    set_tests_properties(${name} PROPERTIES LABELS "${labels}" TIMEOUT 60)
endfunction()

add_executable(lotus_unit_tests lotus-unit-test-main.cpp)
target_link_libraries(lotus_unit_tests PRIVATE Lotus::Engine)
target_compile_definitions(lotus_unit_tests PRIVATE LOTUS_TEST_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/test/golden")

set_target_properties(lotus_unit_tests PROPERTIES UNITY_BUILD OFF)

add_lotus_unit_test(lotus_utf8 utf8 lotus-utf8-test.cpp "unit;lotus")
add_lotus_unit_test(lotus_data data lotus-data-test.cpp "unit;lotus")
add_lotus_unit_test(lotus_utils utils lotus-utils-test.cpp "unit;lotus")
add_lotus_unit_test(lotus_encoder encoder lotus-encoder-test.cpp "unit;lotus;charset")
add_lotus_unit_test(lotus_rules_parser rules_parser lotus-rules-parser-test.cpp "unit;lotus;rules")
add_lotus_unit_test(lotus_flattener flattener lotus-flattener-test.cpp "unit;lotus;flattener")
add_lotus_unit_test(lotus_spelling spelling lotus-spelling-test.cpp "unit;lotus;spelling")
add_lotus_unit_test(lotus_engine_utils engine_utils lotus-engine-utils-test.cpp "unit;lotus;engine-utils")
add_lotus_unit_test(lotus_engine_smoke engine_smoke lotus-engine-smoke.cpp "unit;lotus;engine")
add_lotus_unit_test(lotus_engine_test engine lotus-engine-test.cpp "unit;lotus;engine")
add_lotus_unit_test(lotus_rebuild rebuild lotus-rebuild-test.cpp "unit;lotus;rebuild")
add_lotus_unit_test(lotus_macro_dictionary macro_dictionary lotus-macro-dictionary-test.cpp "unit;lotus;macro")
add_lotus_unit_test(lotus_key_processor key_processor lotus-key-processor-test.cpp "unit;lotus;key-processor")
add_lotus_unit_test(lotus_golden_smoke golden_smoke lotus-golden-smoke.cpp "unit;lotus;golden")
add_lotus_unit_test(numeric_macro_case_preservation numeric_macro_case numeric-macro-case-preservation.cpp "unit;macro")

add_lotus_headless_test(TARGET preedit_lifecycle SOURCES preedit-lifecycle.cpp LABELS "integration;preedit")
add_lotus_headless_test(TARGET preedit_smoke_client SOURCES preedit-smoke-client.cpp LABELS "integration;preedit")
add_lotus_headless_test(TARGET preedit_smoke_server SOURCES preedit-smoke-server.cpp LABELS "integration;preedit")
add_lotus_headless_test(TARGET app_rule_reset SOURCES app-rule-reset.cpp LABELS "integration;apprule")
add_lotus_headless_test(TARGET app_rule_delays SOURCES app-rule-delays.cpp LABELS "integration;apprule")
add_lotus_headless_test(TARGET app_rule_delay_wire SOURCES app-rule-delay-wire.cpp LABELS "integration;apprule")
add_lotus_headless_test(TARGET smooth_buffered_key_replay SOURCES smooth-buffered-key-replay.cpp LABELS "integration;smooth")
add_lotus_headless_test(TARGET focus_bounce_word_survival SOURCES focus-bounce-word-survival.cpp LABELS "integration;smooth")
add_lotus_headless_test(TARGET focus_out_mid_replacement SOURCES focus-out-mid-replacement.cpp LABELS "integration;smooth")
add_lotus_headless_test(TARGET anchor_key_protocol SOURCES anchor-key-protocol.cpp LABELS "integration;smooth")
add_lotus_headless_test(TARGET uinput_server_probe SOURCES uinput-server-probe.cpp LABELS "unit;server")
add_lotus_headless_test(TARGET preedit_backspace SOURCES preedit-backspace.cpp LABELS "integration;preedit")
add_lotus_headless_test(TARGET select_mode_replacement SOURCES select-mode-replacement.cpp LABELS "integration;select")
add_lotus_headless_test(TARGET select_modifier_replay SOURCES select-modifier-replay.cpp LABELS "integration;select")
add_lotus_headless_test(TARGET select_anchor_completion SOURCES select-anchor-completion.cpp LABELS "integration;select")
add_lotus_headless_test(TARGET select_cancel SOURCES select-cancel.cpp LABELS "integration;select")
add_lotus_headless_test(TARGET select_cancel_reconcile SOURCES select-cancel-reconcile.cpp LABELS "integration;select")
add_lotus_headless_test(TARGET select_word_break SOURCES select-word-break.cpp LABELS "integration;select")
add_lotus_headless_test(TARGET replacement_delays SOURCES replacement-delays.cpp LABELS "integration;smooth")
add_lotus_headless_test(TARGET merged_replacement_commit SOURCES merged-replacement-commit.cpp LABELS "integration;smooth")
add_lotus_headless_test(TARGET surrounding_commit_replace SOURCES surrounding-commit-replace.cpp LABELS "integration;surrounding")
add_lotus_headless_test(TARGET dict_reload SOURCES dict-reload.cpp LABELS "integration;dictionary")
add_lotus_headless_test(TARGET config_enum_names SOURCES config-enum-names.cpp LABELS "unit;config")
add_lotus_headless_test(
    TARGET uinput_device
    SOURCES
        uinput-device.cpp
        ${PROJECT_SOURCE_DIR}/server/lotus-uinput-device.cpp
    LABELS "unit;server"
    INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/server
    LINK_LIBRARIES PkgConfig::LIBEVDEV
)
add_lotus_headless_test(
    TARGET select_shift_plan
    SOURCES
        select-shift-plan.cpp
        ${PROJECT_SOURCE_DIR}/server/lotus-physical-shift.cpp
        ${PROJECT_SOURCE_DIR}/server/lotus-logger.cpp
    LABELS "unit;server"
    INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/server
    LINK_LIBRARIES PkgConfig::LIBEVDEV PkgConfig::LIBINPUT
)
add_lotus_headless_test(
    TARGET server_socket_init
    SOURCES
        server-socket-init.cpp
        ${PROJECT_SOURCE_DIR}/server/lotus-server-socket.cpp
        ${PROJECT_SOURCE_DIR}/server/lotus-logger.cpp
        ${PROJECT_SOURCE_DIR}/server/lotus-abstract-socket.cpp
    LABELS "unit;server"
    INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/server ${PROJECT_SOURCE_DIR}/common
)
add_lotus_headless_test(TARGET status_icon SOURCES status-icon.cpp LABELS "integration;icons")
add_test(NAME status_icon_gnome COMMAND status_icon gnome)
set_tests_properties(status_icon_gnome PROPERTIES LABELS "integration;icons" TIMEOUT 20)
add_lotus_headless_test(TARGET key_buffer SOURCES key-buffer.cpp LABELS "unit;buffer")
add_lotus_headless_test(TARGET auto_capitalize_reset SOURCES auto-capitalize-reset.cpp LABELS "integration;capitalize")
add_lotus_headless_test(TARGET auto_capitalize_shortcut SOURCES auto-capitalize-shortcut.cpp LABELS "integration;capitalize")
add_lotus_headless_test(TARGET dbus_deferred_commit SOURCES dbus-deferred-commit.cpp LABELS "integration;dbus")
add_lotus_headless_test(TARGET modifier_key_bypasses_surrounding_text SOURCES modifier-key-bypasses-surrounding-text.cpp LABELS "integration;surrounding")
add_lotus_headless_test(TARGET runtime_safety SOURCES runtime-safety.cpp LABELS "integration;raii")
add_lotus_headless_test(TARGET emoji_history_page_navigation SOURCES emoji-history-page-navigation.cpp LABELS "integration;emoji")
add_lotus_headless_test(TARGET emoji_search_history SOURCES emoji-search-history.cpp LABELS "unit;emoji")
add_lotus_headless_test(TARGET emoji_history_unit SOURCES emoji-history-unit.cpp LABELS "unit;emoji")

string(MD5 LOTUS_CTEST_SOCKET_NAMESPACE "${PROJECT_BINARY_DIR}")
set_tests_properties(smooth_buffered_key_replay focus_bounce_word_survival focus_out_mid_replacement anchor_key_protocol uinput_server_probe select_mode_replacement select_modifier_replay select_anchor_completion select_cancel select_cancel_reconcile select_word_break replacement_delays app_rule_delay_wire dbus_deferred_commit merged_replacement_commit runtime_safety
    PROPERTIES RESOURCE_LOCK lotus_kb_socket
    ENVIRONMENT "LOTUS_SOCKET_NAMESPACE=ctest-${LOTUS_CTEST_SOCKET_NAMESPACE}")

add_executable(executable_path_match
    executable-path-match.cpp
)
target_link_libraries(executable_path_match PRIVATE lotus_common_executable_path)
add_test(NAME executable_path_match COMMAND executable_path_match)
set_tests_properties(executable_path_match PROPERTIES
    LABELS "unit;packaging"
    TIMEOUT 20
)

# Headless Python unit tests for the settings-gui, one CTest entry per module.
add_subdirectory(settings-gui)