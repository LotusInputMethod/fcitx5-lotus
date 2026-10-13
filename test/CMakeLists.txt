# Wraps the core objects compiled once in src/lotus_core_objects for the headless tests.
add_library(lotus_test_core SHARED
    $<TARGET_OBJECTS:lotus_core_objects>
    $<TARGET_OBJECTS:lotus_executable_path>
)
target_include_directories(lotus_test_core PUBLIC
    ${PROJECT_BINARY_DIR}
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(lotus_test_core PUBLIC
    Fcitx5::Core
    Fcitx5::Config
    Fcitx5::Module::Emoji
    Lotus::Engine
    Pthread::Pthread
    PkgConfig::LIBEI
    PkgConfig::LIBEIS
)

# Helper to compile a standalone headless integration test linked with the
# Lotus engine core.
#
# Usage:
#   add_lotus_headless_test(<target_name> <source_file> "<labels>")
# Example:
#   add_lotus_headless_test(my_test my-test.cpp "integration;myfeature")
function(add_lotus_headless_test target source labels)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE
        lotus_test_core
        Fcitx5::Core
        Fcitx5::Config
        Fcitx5::Module::Emoji
        Pthread::Pthread
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${labels}" TIMEOUT 20)
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

add_lotus_headless_test(preedit_lifecycle preedit-lifecycle.cpp "integration;preedit")
add_lotus_headless_test(app_rule_reset app-rule-reset.cpp "integration;apprule")
add_lotus_headless_test(preedit_smoke_client preedit-smoke-client.cpp "integration;preedit")
add_lotus_headless_test(preedit_smoke_server preedit-smoke-server.cpp "integration;preedit")
# TODO(EIS test harness): the transport moved from raw KeyMsg sockets to EIS.
# These tests need a mini EIS server (backend_fd + key-stream asserts) before
# they can run again; key_job_builder still pins the pacer job contract.
# add_lotus_headless_test(smooth_buffered_key_replay
#     smooth-buffered-key-replay.cpp "integration;smooth")
# add_lotus_headless_test(select_mode_replacement
#     select-mode-replacement.cpp "integration;select")
# add_lotus_headless_test(app_rule_delays app-rule-delays.cpp "integration;apprule")
# add_lotus_headless_test(replacement_delays replacement-delays.cpp "integration;smooth")
add_lotus_headless_test(app_rule_delays app-rule-delays.cpp "integration;apprule")
add_lotus_headless_test(key_job_builder key-job-builder.cpp "unit;uinput")
add_lotus_headless_test(select_delete_echo_commit select-delete-echo-commit.cpp "integration;select")
add_lotus_headless_test(dict_reload dict-reload.cpp "integration;dictionary")
# add_lotus_headless_test(key_pacer_resilience key-pacer-resilience.cpp "unit;uinput")
# add_lotus_headless_test(app_rule_delay_wire app-rule-delay-wire.cpp "integration;apprule")
# set_tests_properties(smooth_buffered_key_replay select_mode_replacement replacement_delays app_rule_delay_wire
#     PROPERTIES RESOURCE_LOCK lotus_kb_socket)
add_lotus_headless_test(modifier_key_bypasses_surrounding_text
    modifier-key-bypasses-surrounding-text.cpp "integration;surrounding")
add_lotus_headless_test(runtime_safety
    runtime-safety.cpp "integration;raii")
add_lotus_headless_test(emoji_history_page_navigation
    emoji-history-page-navigation.cpp "integration;emoji")
add_lotus_unit_test(numeric_macro_case_preservation numeric_macro_case
    numeric-macro-case-preservation.cpp "unit;macro")

add_executable(executable_path_match
    executable-path-match.cpp
)
target_link_libraries(executable_path_match PRIVATE lotus_executable_path)
add_test(NAME executable_path_match COMMAND executable_path_match)
set_tests_properties(executable_path_match PROPERTIES
    LABELS "unit;packaging"
    TIMEOUT 20
)
