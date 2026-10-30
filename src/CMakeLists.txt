# Core sources are compiled once here and consumed by both the module and
# lotus_test_core; LOTUS_EXPORT keeps the test-facing API linkable.
add_library(lotus_core_objects OBJECT ${fcitx_lotus_core_sources})
set_target_properties(lotus_core_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(lotus_core_objects PRIVATE
    ${PROJECT_BINARY_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/common
)
target_compile_definitions(lotus_core_objects PRIVATE
    FCITX5_LOTUS_SETTINGS_PATH="${CMAKE_INSTALL_FULL_BINDIR}/fcitx5-lotus-settings"
    LOTUS_SERVER_EXECUTABLE="${LOTUS_SERVER_EXECUTABLE}"
    LOTUS_ALT_EXECUTABLE_PREFIX="${LOTUS_ALT_EXECUTABLE_PREFIX}"
)
target_link_libraries(lotus_core_objects PRIVATE
    Fcitx5::Core
    Fcitx5::Config
    Fcitx5::Module::Emoji
    Lotus::Engine
    PkgConfig::LIBNOTIFY
    PkgConfig::DBUS
    Pthread::Pthread
)

# Parse the shared fcitx5 headers once instead of once per TU.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)
    set(LOTUS_PCH_HEADERS
        <fcitx/instance.h>
        <fcitx/addonmanager.h>
        <fcitx/addonfactory.h>
        <fcitx/inputmethodengine.h>
        <fcitx/action.h>
        <fcitx/inputcontext.h>
        <fcitx/candidatelist.h>
        <fcitx/inputpanel.h>
        <fcitx/text.h>
        <fcitx/menu.h>
        <fcitx/userinterface.h>
        <fcitx/userinterfacemanager.h>
        <fcitx-config/iniparser.h>
        <fcitx-utils/log.h>
        <fcitx-utils/event.h>
        <fcitx-utils/keysym.h>
        <fcitx-utils/utf8.h>
    )
    target_precompile_headers(lotus_core_objects PRIVATE ${LOTUS_PCH_HEADERS})
    # test/ reuses the same list for its own PCH host (sibling scope).
    set(LOTUS_PCH_HEADERS "${LOTUS_PCH_HEADERS}" PARENT_SCOPE)
endif()

add_library(lotus MODULE lotus.cpp
    $<TARGET_OBJECTS:lotus_core_objects>
)
set_target_properties(lotus PROPERTIES OUTPUT_NAME "lotus")
set_target_properties(lotus PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(lotus
        lotus_common_executable_path
        Fcitx5::Core
        Fcitx5::Config
        Fcitx5::Module::Emoji
        Lotus::Engine
        PkgConfig::LIBNOTIFY
        PkgConfig::DBUS
        Pthread::Pthread
)

target_include_directories(lotus PRIVATE
    ${PROJECT_BINARY_DIR}
)
# Same flags as lotus_core_objects so its prebuilt PCH is reused, not rejected.
target_compile_definitions(lotus PRIVATE
    FCITX5_LOTUS_SETTINGS_PATH="${CMAKE_INSTALL_FULL_BINDIR}/fcitx5-lotus-settings"
    LOTUS_SERVER_EXECUTABLE="${LOTUS_SERVER_EXECUTABLE}"
    LOTUS_ALT_EXECUTABLE_PREFIX="${LOTUS_ALT_EXECUTABLE_PREFIX}"
)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)
    target_precompile_headers(lotus REUSE_FROM lotus_core_objects)
endif()
install(TARGETS lotus DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")

fcitx5_translate_desktop_file(lotus.conf.in lotus.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lotus.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")
configure_file(lotus-addon.conf.in.in lotus-addon.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/lotus-addon.conf.in" lotus-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lotus-addon.conf" RENAME lotus.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
