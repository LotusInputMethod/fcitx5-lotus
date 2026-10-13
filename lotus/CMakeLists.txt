# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Portable Vietnamese input engine, ported from bamboo/bamboo-core (MIT, kept in CREDITS).
set(CMAKE_UNITY_BUILD_BATCH_SIZE 8)
if(NOT DEFINED CMAKE_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD OFF)
endif()
add_library(lotus_engine STATIC
    case_map.cpp
    composition.cpp
    dictionary.cpp
    encoder.cpp
    engine.cpp
    engine_utils.cpp
    flattener.cpp
    input_method.cpp
    key_processor.cpp
    macro_table.cpp
    rebuild.cpp
    rules_parser.cpp
    spelling.cpp
    utf8.cpp
    utils.cpp
    data/case_data.cpp
    data/charset_data.cpp
    data/input_method_data.cpp
    data/spelling_data.cpp
    data/vowel_data.cpp
)
set_target_properties(lotus_engine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_compile_options(lotus_engine PRIVATE -Wall -Wextra)
# Consumers use bare includes ("utf8.h", "data/data.h"), so the engine directory is the include root
# and the whole engine can be moved or vendored without touching the sources.
target_include_directories(lotus_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_library(Lotus::Engine ALIAS lotus_engine)
