# Vendored Unikey engine.
# SPDX-FileCopyrightText: Unikey authors (LGPL/GPL)

set(UNIKEY_SRCS
    byteio.cpp
    charset.cpp
    convert.cpp
    data.cpp
    inputproc.cpp
    mactab.cpp
    pattern.cpp
    ukengine.cpp
    usrkeymap.cpp
    unikeyinputcontext.cpp
)

add_library(unikey-lib STATIC ${UNIKEY_SRCS})
target_link_libraries(unikey-lib PUBLIC Fcitx5::Utils)
set_target_properties(unikey-lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(unikey-lib PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

if (NOT TARGET Unikey::Core)
    add_library(Unikey::Core INTERFACE IMPORTED GLOBAL)
    set_target_properties(Unikey::Core PROPERTIES
        INTERFACE_LINK_LIBRARIES unikey-lib
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
