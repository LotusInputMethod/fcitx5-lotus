
file(GLOB BAMBOO_CORE_GO_SRCS CONFIGURE_DEPENDS bamboo-core/*.go)
file(GLOB BAMBOO_GO_SRCS CONFIGURE_DEPENDS *.go)

# go build always rewrites its outputs, so build into staging/ and place them
# with copy_if_different: unchanged content keeps the old timestamps and
# downstream targets are not recompiled/relinked.
set(BAMBOO_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/staging")

# Custom command for 'go build -buildmode=c-archive ...'
# to create a library from Go codes.
add_custom_command(OUTPUT bamboo-core.a bamboo-core.h
    DEPENDS ${BAMBOO_GO_SRCS} ${BAMBOO_CORE_GO_SRCS} go.mod
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BAMBOO_STAGING_DIR}"
    COMMAND env CGO_CFLAGS="-fPIC" CGO_LDFLAGS="-fPIC" go build
                -buildmode=c-archive
                "-ldflags=-s -w"
                -o "${BAMBOO_STAGING_DIR}/bamboo-core.a"
            ${BAMBOO_GO_SRCS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BAMBOO_STAGING_DIR}/bamboo-core.a" "${CMAKE_CURRENT_BINARY_DIR}/bamboo-core.a"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BAMBOO_STAGING_DIR}/bamboo-core.h" "${CMAKE_CURRENT_BINARY_DIR}/bamboo-core.h")
add_custom_target(bamboo-core DEPENDS bamboo-core.a)
# Add a custom target for the library.
add_library(Bamboo::Core UNKNOWN IMPORTED GLOBAL)
add_dependencies(Bamboo::Core bamboo-core)
set_target_properties(Bamboo::Core PROPERTIES
     IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/bamboo-core.a"
     INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}")
