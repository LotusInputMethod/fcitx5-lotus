set(LOTUS_UINPUT_PROXY_USER "uinput_proxy" CACHE STRING
    "System user granted rw ACL on /dev/uinput (udev rules, sysusers.d, service scripts)")

option(INSTALL_OPENRC "Install OpenRC script" OFF)
option(INSTALL_RUNIT "Install Runit script" OFF)

configure_file(99-lotus.rules.in ${CMAKE_CURRENT_BINARY_DIR}/99-lotus.rules @ONLY)
configure_file(fcitx5-lotus-server.service.in ${CMAKE_CURRENT_BINARY_DIR}/fcitx5-lotus-server.service @ONLY)
configure_file(user-lotus.conf.in ${CMAKE_CURRENT_BINARY_DIR}/user-lotus.conf @ONLY)

if(INSTALL_OPENRC)
    configure_file(fcitx5-lotus.openrc.in ${CMAKE_CURRENT_BINARY_DIR}/fcitx5-lotus.openrc @ONLY)
endif(INSTALL_OPENRC)

if(INSTALL_RUNIT)
    configure_file(fcitx5-lotus.runit.in ${CMAKE_CURRENT_BINARY_DIR}/fcitx5-lotus.runit @ONLY)
endif(INSTALL_RUNIT)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fcitx5-lotus-server.service"
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/99-lotus.rules"
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)

install(FILES module-fcitx5-lotus.conf
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/modules-load.d
    RENAME fcitx5-lotus.conf)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/user-lotus.conf"
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/sysusers.d
    RENAME lotus.conf)

if(INSTALL_OPENRC)
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/fcitx5-lotus.openrc"
        DESTINATION /etc/init.d
        RENAME fcitx5-lotus)
endif(INSTALL_OPENRC)

if(INSTALL_RUNIT)
    set(RUNIT_SV_DIR "/etc/sv" CACHE PATH
        "Path to runit service definitions directory (e.g. /etc/sv on Void, /etc/runit/sv on Artix)")
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/fcitx5-lotus.runit"
        DESTINATION "${RUNIT_SV_DIR}/fcitx5-lotus"
        RENAME run)
endif(INSTALL_RUNIT)
