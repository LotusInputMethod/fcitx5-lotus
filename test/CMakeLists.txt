# Shared library compiling the 7 core Lotus sources once, so the heavy
# bamboo-core.a (Go runtime) is linked once here instead of once per test.
add_library(lotus_test_core SHARED ${fcitx_lotus_core_sources})
# Test-only .so must export its symbols: fcitx5's Fcitx5CompilerSettings
# hides visibility project-wide.
set_target_properties(lotus_test_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
)
target_include_directories(lotus_test_core PUBLIC
    ${PROJECT_BINARY_DIR}
    ${PROJECT_SOURCE_DIR}/src
)
target_compile_definitions(lotus_test_core PUBLIC
    FCITX5_LOTUS_SETTINGS_PATH="${CMAKE_INSTALL_FULL_BINDIR}/fcitx5-lotus-settings"
    FCITX_LOTUS_ICON_DIR="${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/scalable/apps"
)
target_link_libraries(lotus_test_core PUBLIC
    Fcitx5::Core
    Fcitx5::Config
    Fcitx5::Module::Emoji
    Bamboo::Core
    Pthread::Pthread
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

# Helper to compile lightweight unit test linked with Bamboo engine core.
function(add_bamboo_test target source labels)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE Bamboo::Core)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${labels}" TIMEOUT 20)
endfunction()

add_lotus_headless_test(preedit_lifecycle preedit-lifecycle.cpp "integration;preedit")
add_lotus_headless_test(app_rule_reset app-rule-reset.cpp "integration;apprule")
add_lotus_headless_test(preedit_smoke_client preedit-smoke-client.cpp "integration;preedit")
add_lotus_headless_test(preedit_smoke_server preedit-smoke-server.cpp "integration;preedit")
add_lotus_headless_test(smooth_buffered_key_replay
    smooth-buffered-key-replay.cpp "integration;smooth")
add_lotus_headless_test(modifier_key_bypasses_surrounding_text
    modifier-key-bypasses-surrounding-text.cpp "integration;surrounding")
add_lotus_headless_test(emoji_history_page_navigation
    emoji-history-page-navigation.cpp "integration;emoji")
add_bamboo_test(numeric_macro_case_preservation
    numeric-macro-case-preservation.cpp "unit;macro")

add_executable(emoji_backspace_utf8
    test-emoji-backspace.cpp
    ${PROJECT_SOURCE_DIR}/src/lotus-utils.cpp
)
target_include_directories(emoji_backspace_utf8 PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(emoji_backspace_utf8 PRIVATE
    Fcitx5::Core
)
add_test(NAME emoji_backspace_utf8 COMMAND emoji_backspace_utf8)
set_tests_properties(emoji_backspace_utf8 PROPERTIES
    LABELS "unit;emoji"
    TIMEOUT 20
)
