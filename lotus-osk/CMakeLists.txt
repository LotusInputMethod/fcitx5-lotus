cmake_minimum_required(VERSION 3.16)

project(lotus-osk VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Core Gui Widgets WaylandClient DBus REQUIRED)
find_package(LayerShellQt)

if (LayerShellQt_FOUND)
    add_definitions(-DHAVE_LAYER_SHELL)
endif()

add_executable(lotus-osk
    src/main.cpp
    src/osk_window.cpp
    src/osk_window.h
    src/osk_controller.cpp
    src/osk_controller.h
)

target_link_libraries(lotus-osk
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WaylandClient
    Qt6::DBus
)

if (LayerShellQt_FOUND)
    target_link_libraries(lotus-osk PRIVATE LayerShellQt::Interface)
endif()

target_include_directories(lotus-osk PRIVATE src)

install(TARGETS lotus-osk DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/app.lotus.Osk.service.in
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services
    RENAME app.lotus.Osk.service
)
