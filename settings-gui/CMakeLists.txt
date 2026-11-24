set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(LOTUS_SETTINGS_SOURCES
    cpp/main.cpp
    cpp/i18n_bridge.h
    cpp/i18n_bridge.cpp
    cpp/theme_bridge.h
    cpp/theme_bridge.cpp
    cpp/dbus_client.h
    cpp/dbus_client.cpp
    cpp/settings_backend.h
    cpp/settings_backend.cpp
    lotus_settings.qrc
)

add_executable(fcitx5-lotus-settings ${LOTUS_SETTINGS_SOURCES})

target_compile_features(fcitx5-lotus-settings PRIVATE cxx_std_20)

target_compile_definitions(fcitx5-lotus-settings PRIVATE
    LOTUS_VERSION="${PROJECT_VERSION}"
    LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
    DICTIONARY_PATH="${FCITX_INSTALL_PKGDATADIR}/lotus/vietnamese.cm.dict"
)

target_link_libraries(fcitx5-lotus-settings PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
    Qt6::Widgets
    $<$<BOOL:${Intl_FOUND}>:Intl::Intl>
)

install(TARGETS fcitx5-lotus-settings
        DESTINATION "${CMAKE_INSTALL_BINDIR}")

fcitx5_translate_desktop_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/org.fcitx.Fcitx5.Addon.Lotus.Settings.desktop.in"
    org.fcitx.Fcitx5.Addon.Lotus.Settings.desktop
    DESKTOP
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.Lotus.Settings.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
