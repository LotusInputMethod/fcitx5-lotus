find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(LIBUDEV  REQUIRED IMPORTED_TARGET libudev)

add_executable(fcitx5-lotus-server lotus-server.cpp lotus-logger.cpp lotus-eis-server.cpp)

target_link_libraries(fcitx5-lotus-server
    lotus_common_executable_path
    PkgConfig::LIBINPUT
    PkgConfig::LIBUDEV
    PkgConfig::LIBEIS
    PkgConfig::LIBEVDEV
)

target_compile_definitions(fcitx5-lotus-server PRIVATE
    LOTUS_FCITX5_EXECUTABLE="${LOTUS_FCITX5_EXECUTABLE}"
    LOTUS_ALT_EXECUTABLE_PREFIX="${LOTUS_ALT_EXECUTABLE_PREFIX}"
)

install(TARGETS fcitx5-lotus-server DESTINATION ${CMAKE_INSTALL_BINDIR})
