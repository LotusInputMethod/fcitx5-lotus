# $<TARGET_OBJECTS:> in non-OBJECT target sources requires CMake 3.12.
cmake_minimum_required(VERSION 3.12)

project(fcitx5-lotus VERSION 5.0.1)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(BUILD_TESTING "Build Lotus tests" OFF)

# The addon entry point is intentionally excluded: headless tests link the
# production implementation through the lotus_test_core target, never the
# module build itself.
set(fcitx_lotus_core_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lotus-engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lotus-state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lotus-candidates.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lotus-utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lotus-monitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lotus-plasma-theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/emoji.cpp
)

set(REQUIRED_FCITX_VERSION 5.0.14)
find_package(ECM 1.0.0 REQUIRED)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
include(FeatureSummary)
include(GNUInstallDirs)
include(ECMUninstallTarget)

find_package(Fcitx5Core ${REQUIRED_FCITX_VERSION} REQUIRED)
find_package(Fcitx5ModuleEmoji REQUIRED)
find_package(Pthread REQUIRED)
find_package(Gettext REQUIRED)

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")
add_definitions(-DFCITX_GETTEXT_DOMAIN=\"fcitx5-lotus\") 

if (Fcitx5Utils_VERSION VERSION_GREATER_EQUAL "5.1.13")
    add_definitions(-DLOTUS_USE_MODERN_FCITX_API)
    message(STATUS "Fcitx5 version ${Fcitx5Utils_VERSION} detected: Using modern API")
else()
    message(STATUS "Fcitx5 version ${Fcitx5Utils_VERSION} detected: Using legacy API")
endif()
fcitx5_add_i18n_definition()

# Absolute paths that every packaging has to override.
set(LOTUS_FCITX5_EXECUTABLE "/usr/bin/fcitx5" CACHE STRING
    "fcitx5 executable allowed to connect to the Lotus uinput/mouse sockets")
set(LOTUS_SERVER_EXECUTABLE "/usr/bin/fcitx5-lotus-server" CACHE STRING
    "fcitx5-lotus-server executable allowed to connect to the Lotus mouse socket")
set(LOTUS_ALT_EXECUTABLE_PREFIX "" CACHE STRING
    "Additional prefix whose <prefix>*bin/<name> executables are accepted too (NixOS: /nix/store/)")
set(LOTUS_SETFACL_EXECUTABLE "/usr/bin/setfacl" CACHE FILEPATH
    "setfacl used by the udev rule and the service units")

add_library(lotus_executable_path OBJECT src/lotus-executable-path.cpp)
set_target_properties(lotus_executable_path PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(lotus_executable_path PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory(po)
add_subdirectory(bamboo)
add_subdirectory(src)
add_subdirectory(data)
add_subdirectory(server)
add_subdirectory(misc)
add_subdirectory(settings-gui)

if (BUILD_TESTING)
    include(CTest)
    add_subdirectory(test)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/org.fcitx.Fcitx5.Addon.Lotus.metainfo.xml.in.in"
    "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.Lotus.metainfo.xml.in"
    @ONLY
)

fcitx5_translate_desktop_file(
    "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.Lotus.metainfo.xml.in"
    org.fcitx.Fcitx5.Addon.Lotus.metainfo.xml
    XML
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.Lotus.metainfo.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

install(FILES 
        LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}
)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
