find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(SETTINGS_GUI_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/settings-gui")
set(SETTINGS_GUI_FULL_PATH "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/settings-gui")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.py.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.py"
    @ONLY
)

install(DIRECTORY core ui
        DESTINATION "${SETTINGS_GUI_INSTALL_DIR}"
        FILES_MATCHING PATTERN "*.py" PATTERN "*.qss" PATTERN "*.svg")

install(FILES main.py i18n.py "${CMAKE_CURRENT_BINARY_DIR}/version.py"
        DESTINATION "${SETTINGS_GUI_INSTALL_DIR}")

install(FILES main.py
        DESTINATION "${SETTINGS_GUI_INSTALL_DIR}"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

set(LAUNCHER_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/fcitx5-lotus-settings")

file(WRITE "${LAUNCHER_SCRIPT}"
"#!/bin/sh
exec \"${SETTINGS_GUI_FULL_PATH}/main.py\" \"$@\"
")

install(PROGRAMS "${LAUNCHER_SCRIPT}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(CODE "
    message(STATUS \"Byte-compiling Python scripts...\")
    execute_process(
        COMMAND \"${Python3_EXECUTABLE}\" -m compileall -q \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${SETTINGS_GUI_INSTALL_DIR}\"
    )
")

fcitx5_translate_desktop_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/org.fcitx.Fcitx5.Addon.Lotus.Settings.desktop.in"
    org.fcitx.Fcitx5.Addon.Lotus.Settings.desktop
    DESKTOP
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.Lotus.Settings.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")