# Vendored Unikey engine (core/) + Lotus wrapper
# SPDX-FileCopyrightText: Unikey authors (LGPL/GPL); Lotus wrapper GPL-3.0-or-later
set(_UK_CORE "${CMAKE_CURRENT_SOURCE_DIR}/core")
set(LOTUS_UNIKEY_CORE_SRCS
    ${_UK_CORE}/byteio.cpp
    ${_UK_CORE}/charset.cpp
    ${_UK_CORE}/convert.cpp
    ${_UK_CORE}/data.cpp
    ${_UK_CORE}/inputproc.cpp
    ${_UK_CORE}/mactab.cpp
    ${_UK_CORE}/pattern.cpp
    ${_UK_CORE}/ukengine.cpp
    ${_UK_CORE}/usrkeymap.cpp
    ${_UK_CORE}/unikeyinputcontext.cpp
)
add_library(lotus-unikey-core STATIC ${LOTUS_UNIKEY_CORE_SRCS})
set_target_properties(lotus-unikey-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(lotus-unikey-core PUBLIC Fcitx5::Utils)
target_include_directories(lotus-unikey-core PUBLIC "${_UK_CORE}")
add_library(lotus-unikey-bridge STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/LotusUnikeyEngine.cpp"
)
target_link_libraries(lotus-unikey-bridge PUBLIC lotus-unikey-core)
target_include_directories(lotus-unikey-bridge PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${_UK_CORE}"
)
