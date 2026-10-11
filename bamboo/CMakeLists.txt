
file(GLOB BAMBOO_CORE_GO_SRCS CONFIGURE_DEPENDS bamboo-core/*.go)
file(GLOB BAMBOO_GO_SRCS CONFIGURE_DEPENDS *.go)

# Go side: the engine is now a standalone helper executable built with
# CGO_ENABLED=0 (fully static, pure-Go, glibc and musl alike), talking to the
# C++ side over a private abstract unix socket (src/bamboo-ipc.cpp).
#
# go build always rewrites its outputs, so build into staging/ and place them
# with copy_if_different: unchanged content keeps the old timestamps and
# downstream targets are not recompiled/relinked.
set(BAMBOO_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/staging")

add_custom_command(OUTPUT lotus-engine-helper
    DEPENDS ${BAMBOO_GO_SRCS} ${BAMBOO_CORE_GO_SRCS} go.mod
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BAMBOO_STAGING_DIR}"
    COMMAND env CGO_ENABLED=0 go build
                "-ldflags=-s -w"
                -o "${BAMBOO_STAGING_DIR}/lotus-engine-helper"
            ${BAMBOO_GO_SRCS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BAMBOO_STAGING_DIR}/lotus-engine-helper" "${CMAKE_CURRENT_BINARY_DIR}/lotus-engine-helper")
add_custom_target(lotus_engine_helper DEPENDS lotus-engine-helper)

# Imported executable wrapper so tests can reference the built path with
# $<TARGET_FILE:Lotus::Helper> without knowing how it was produced.
add_executable(Lotus::Helper IMPORTED GLOBAL)
set_target_properties(Lotus::Helper PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/lotus-engine-helper")
add_dependencies(Lotus::Helper lotus_engine_helper)

# C++ side: the IPC shim that keeps the old bamboo-core.h C API. It replaces
# the linked Go archive, so liblotus no longer embeds the Go runtime at all.
# The alias preserves the Bamboo::Core name used by src/ and test/.
add_library(bamboo_ipc STATIC ${PROJECT_SOURCE_DIR}/src/bamboo-ipc.cpp)
set_target_properties(bamboo_ipc PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(bamboo_ipc PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(bamboo_ipc PUBLIC Pthread::Pthread)
target_compile_definitions(bamboo_ipc PRIVATE
    LOTUS_ENGINE_HELPER_EXECUTABLE="${CMAKE_INSTALL_FULL_LIBDIR}/fcitx5/lotus-engine-helper"
)
add_dependencies(bamboo_ipc lotus_engine_helper)
add_library(Bamboo::Core ALIAS bamboo_ipc)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lotus-engine-helper"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
