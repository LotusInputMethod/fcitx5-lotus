option(LOTUS_ENABLE_AVX512 "Enable AVX-512 acceleration for lotus-utils" OFF)
option(LOTUS_ENABLE_LOG "Enable LOG" ON)
set(fcitx_lotus_sources
  lotus.cpp
  lotus-engine.cpp
  lotus-state.cpp
  lotus-candidates.cpp
  lotus-utils.cpp
  lotus-monitor.cpp
  emoji.cpp
)

if (LOTUS_ENABLE_AVX512)
  list(APPEND fcitx_lotus_sources lotus-utils-avx512.S)
endif()
enable_language(ASM)
set_source_files_properties(lotus-utils-avx512.S PROPERTIES LANGUAGE ASM)
list(APPEND fcitx_lotus_sources lotus-unikey-backend.cpp)

add_library(lotus MODULE ${fcitx_lotus_sources})
set_target_properties(lotus PROPERTIES OUTPUT_NAME "lotus")

target_link_libraries(lotus
  Fcitx5::Core
  Fcitx5::Config
  Fcitx5::Module::Emoji
  lotus-unikey-bridge
  Pthread::Pthread
  X11::X11
)
if (LOTUS_ENABLE_LOG)
  target_compile_definitions(lotus PRIVATE LOTUS_ENABLE_LOG=1)
endif()
target_compile_definitions(lotus PRIVATE LOTUS_ENGINE_UNIKEY=1)
target_include_directories(lotus PRIVATE "${PROJECT_SOURCE_DIR}/unikey/core")

target_include_directories(lotus PRIVATE
    ${PROJECT_BINARY_DIR}
)
target_compile_definitions(lotus PRIVATE
    FCITX5_LOTUS_SETTINGS_PATH=\"${CMAKE_INSTALL_FULL_BINDIR}/fcitx5-lotus-settings\"
)
if (LOTUS_ENABLE_AVX512)
  target_compile_definitions(lotus PRIVATE LOTUS_ENABLE_AVX512=1)

  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lotus PRIVATE
      -O3
      -mavx512f
      -mavx512bw
      -mbmi
    )
  endif()
endif()
install(TARGETS lotus DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")

fcitx5_translate_desktop_file(lotus.conf.in lotus.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lotus.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")
configure_file(lotus-addon.conf.in.in lotus-addon.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/lotus-addon.conf.in" lotus-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lotus-addon.conf" RENAME lotus.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
