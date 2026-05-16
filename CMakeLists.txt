cmake_minimum_required(VERSION 3.10)

project(fcitx5-lotus VERSION 1.8.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/lotus-version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/lotus-version.h"
    @ONLY
)

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

set(REQUIRED_FCITX_VERSION 5.0.5)
find_package(ECM 1.0.0 REQUIRED)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
include(FeatureSummary)
include(GNUInstallDirs)
include(ECMUninstallTarget)

option(ENABLE_QT "Enable Qt based GUI" On)

find_package(Fcitx5Core ${REQUIRED_FCITX_VERSION} REQUIRED)
find_package(Fcitx5ModuleEmoji REQUIRED)
find_package(Pthread REQUIRED)
find_package(Gettext REQUIRED)
find_package(X11 REQUIRED)

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")
add_definitions(-DFCITX_GETTEXT_DOMAIN=\"fcitx5-lotus\") 
add_definitions(-DQT_NO_KEYWORDS)

find_package(Fcitx5Utils REQUIRED)
if (Fcitx5Utils_VERSION VERSION_GREATER_EQUAL "5.1.13")
    add_definitions(-DLOTUS_USE_MODERN_FCITX_API)
    message(STATUS "Fcitx5 version ${Fcitx5Utils_VERSION} detected: Using modern API")
else()
    message(STATUS "Fcitx5 version ${Fcitx5Utils_VERSION} detected: Using legacy API")
endif()
fcitx5_add_i18n_definition()

add_subdirectory(unikey)
if (ENABLE_QT)
    set(QT_MAJOR_VERSION 6)
    find_package(Qt${QT_MAJOR_VERSION} REQUIRED COMPONENTS Core Gui Widgets)
    find_package(Fcitx5Qt${QT_MAJOR_VERSION}WidgetsAddons 5.0.12 REQUIRED)
    add_subdirectory(macro-editor)
    add_subdirectory(keymap-editor)
endif()
add_subdirectory(po)
add_subdirectory(src)
add_subdirectory(data)
add_subdirectory(server)
add_subdirectory(misc)

set(LOTUS_METAINFO_ID org.fcitx.Fcitx5.Addon.Lotus.metainfo)
set(LOTUS_METAINFO_TRANSLATION_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/${LOTUS_METAINFO_ID}.xml.in")
set(LOTUS_METAINFO_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${LOTUS_METAINFO_ID}.xml")

fcitx5_translate_desktop_file(
    "${LOTUS_METAINFO_TRANSLATION_INPUT}"
    "${LOTUS_METAINFO_ID}.xml"
    XML
)

install(FILES "${LOTUS_METAINFO_OUTPUT}" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

install(FILES
        LICENSES/GPL-3.0-or-later.txt
        LICENSES/LGPL-2.1-or-later.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}
)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
