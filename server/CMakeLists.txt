find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(LIBUDEV  REQUIRED IMPORTED_TARGET libudev)

add_executable(fcitx5-lotus-server lotus-server.cpp lotus-logger.cpp)

target_link_libraries(fcitx5-lotus-server
    PkgConfig::LIBINPUT
    PkgConfig::LIBUDEV
)

set(FCITX5_EXECUTABLE_PATH "${CMAKE_INSTALL_FULL_BINDIR}/fcitx5")
set(FCITX5_OSK_EXECUTABLE_PATH "${CMAKE_INSTALL_FULL_BINDIR}/fcitx5-lotus-osk")

configure_file(lotus-server-config.h.in lotus-server-config.h @ONLY)

target_include_directories(fcitx5-lotus-server PRIVATE ../src ${CMAKE_CURRENT_BINARY_DIR})

install(TARGETS fcitx5-lotus-server DESTINATION ${CMAKE_INSTALL_BINDIR})
