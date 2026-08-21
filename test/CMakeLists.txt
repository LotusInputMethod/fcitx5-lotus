# Helper macro to compile standalone headless integration test linked with Lotus engine.
#
# Usage:
#   add_lotus_headless_test(<target_name> <source_file> "<labels>")
# Example:
#   add_lotus_headless_test(my_test my-test.cpp "integration;myfeature")
function(add_lotus_headless_test target source labels)
    add_executable(${target} ${source} ${fcitx_lotus_core_sources})
    target_include_directories(${target} PRIVATE
        ${PROJECT_BINARY_DIR}
        ${PROJECT_SOURCE_DIR}/src
    )
    target_compile_definitions(${target} PRIVATE
        FCITX5_LOTUS_SETTINGS_PATH="${CMAKE_INSTALL_FULL_BINDIR}/fcitx5-lotus-settings"
        FCITX_LOTUS_ICON_DIR="${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/scalable/apps"
    )
    target_link_libraries(${target} PRIVATE
        Fcitx5::Core
        Fcitx5::Config
        Fcitx5::Module::Emoji
        Bamboo::Core
        Pthread::Pthread
        X11::X11
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${labels}" TIMEOUT 20)
endfunction()

# Helper macro to compile lightweight unit test linked with Bamboo engine core.
function(add_bamboo_test target source labels)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE Bamboo::Core)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${labels}" TIMEOUT 20)
endfunction()

add_lotus_headless_test(preedit_lifecycle preedit-lifecycle.cpp "integration;preedit")
add_lotus_headless_test(preedit_smoke_client preedit-smoke-client.cpp "integration;preedit")
add_lotus_headless_test(preedit_smoke_server preedit-smoke-server.cpp "integration;preedit")
