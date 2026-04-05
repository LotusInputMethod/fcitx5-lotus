find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(LIBUDEV  REQUIRED IMPORTED_TARGET libudev)

add_executable(fcitx5-lotus-server lotus-server.cpp lotus-logger.cpp)

target_link_libraries(fcitx5-lotus-server
    PkgConfig::LIBINPUT
    PkgConfig::LIBUDEV
)

target_include_directories(fcitx5-lotus-server PRIVATE ../src)

target_compile_definitions(fcitx5-lotus-server PRIVATE
    FCITX5_EXECUTABLE_PATH=\"${CMAKE_INSTALL_FULL_BINDIR}/fcitx5\"
    LOTUS_OSK_EXECUTABLE_PATH=\"${CMAKE_INSTALL_FULL_BINDIR}/lotus-osk\"
)

install(TARGETS fcitx5-lotus-server DESTINATION ${CMAKE_INSTALL_BINDIR})
