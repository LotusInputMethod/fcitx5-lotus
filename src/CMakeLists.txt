# Core sources are compiled once here and consumed by both the module and
# lotus_test_core; LOTUS_EXPORT keeps the test-facing API linkable.
# The injected application hook is a separate library; the Fcitx addon owns its IPC server.
add_library(lotus-apphook SHARED
    apphook/lotus-apphook-target.cpp
    apphook/lotus-apphook-protocol.cpp
    apphook/lotus-apphook-x11.cpp
    apphook/lotus-apphook-xcb.cpp
    apphook/lotus-apphook-wayland.cpp
    apphook/lotus-apphook-elfhook.cpp
)
set_target_properties(lotus-apphook PROPERTIES OUTPUT_NAME "lotus-apphook")
target_compile_options(lotus-apphook PRIVATE -fvisibility=hidden)
target_include_directories(lotus-apphook PRIVATE ${PROJECT_SOURCE_DIR}/common)
target_link_libraries(lotus-apphook PRIVATE ${CMAKE_DL_LIBS} Pthread::Pthread)
target_link_options(lotus-apphook PRIVATE
    "-Wl,-Bsymbolic-functions"
    "-Wl,-z,nodelete"
)
install(TARGETS lotus-apphook LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
add_library(lotus_core_objects OBJECT ${fcitx_lotus_core_sources})
set_target_properties(lotus_core_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(lotus_core_objects PRIVATE
    ${PROJECT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
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
    PkgConfig::XCB
    PkgConfig::XCB_XTEST
    PkgConfig::WAYLAND_CLIENT
    Pthread::Pthread
)

set(LOTUS_VK_HEADER "${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-unstable-v1-client-protocol.h")
set(LOTUS_VK_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-unstable-v1-protocol.c")
add_custom_command(
    OUTPUT "${LOTUS_VK_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${LOTUS_VK_XML}" "${LOTUS_VK_HEADER}"
    DEPENDS "${LOTUS_VK_XML}"
    VERBATIM
)
add_custom_command(
    OUTPUT "${LOTUS_VK_SOURCE}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${LOTUS_VK_XML}" "${LOTUS_VK_SOURCE}"
    DEPENDS "${LOTUS_VK_XML}"
    VERBATIM
)
set_source_files_properties("${LOTUS_VK_SOURCE}" PROPERTIES
    LANGUAGE C
    SKIP_PRECOMPILE_HEADERS ON
)
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/lotus-wlr-virtual-keyboard.cpp"
    PROPERTIES OBJECT_DEPENDS "${LOTUS_VK_HEADER}"
)
target_sources(lotus_core_objects PRIVATE
    "${LOTUS_VK_HEADER}"
    "${LOTUS_VK_SOURCE}"
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
    # genex needed because target has a .c source.
    target_precompile_headers(lotus_core_objects PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/instance.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/addonmanager.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/addonfactory.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/inputmethodengine.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/action.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/inputcontext.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/candidatelist.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/inputpanel.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/text.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/menu.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/userinterface.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx/userinterfacemanager.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx-config/iniparser.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx-utils/log.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx-utils/event.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx-utils/keysym.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fcitx-utils/utf8.h$<ANGLE-R>>"
    )
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
        PkgConfig::XCB
        PkgConfig::XCB_XTEST
        PkgConfig::WAYLAND_CLIENT
        Pthread::Pthread
)

target_include_directories(lotus PRIVATE
    ${PROJECT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
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
