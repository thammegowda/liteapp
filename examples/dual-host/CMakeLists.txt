cmake_minimum_required(VERSION 3.25 FATAL_ERROR)
project(liteapp_dual_host LANGUAGES CXX)

set(LITEAPP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "LiteApp source directory")
set(LITEAPP_BUILD_TESTS OFF CACHE BOOL "Build LiteApp SDK tests" FORCE)
add_subdirectory("${LITEAPP_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/liteapp" EXCLUDE_FROM_ALL)

set(DUAL_HOST_STAGE "${CMAKE_CURRENT_BINARY_DIR}/generated/web")
set(DUAL_HOST_FILES
    app.mjs
    index.html
    services.mjs
    style.css
    wasm-worker.mjs
    webgpu-worker.mjs
)
set(LITEAPP_WEB_FILES
    liteapp.mjs
    host/event-client.mjs
    host/variant-client.mjs
    transport/channel.mjs
    transport/message-transport.mjs
    transport/protocol.mjs
    transport/service-router.mjs
    transport/websocket-transport.mjs
)
file(MAKE_DIRECTORY
    "${DUAL_HOST_STAGE}"
    "${DUAL_HOST_STAGE}/liteapp/host"
    "${DUAL_HOST_STAGE}/liteapp/transport"
)
foreach(file IN LISTS DUAL_HOST_FILES)
    configure_file("web/${file}" "${DUAL_HOST_STAGE}/${file}" COPYONLY)
endforeach()
foreach(file IN LISTS LITEAPP_WEB_FILES)
    configure_file("${LITEAPP_SOURCE_DIR}/web/${file}" "${DUAL_HOST_STAGE}/liteapp/${file}" COPYONLY)
endforeach()
set(DUAL_HOST_ARCHIVE_FILES ${DUAL_HOST_FILES})
foreach(file IN LISTS LITEAPP_WEB_FILES)
    list(APPEND DUAL_HOST_ARCHIVE_FILES "liteapp/${file}")
endforeach()
liteapp_embed_archive(
    TARGET liteapp_dual_host_assets
    NAMESPACE liteapp::dual_host_assets
    HEADER liteapp/dual_host_assets.h
    ROOT "${DUAL_HOST_STAGE}"
    FILES ${DUAL_HOST_ARCHIVE_FILES}
)

add_executable(liteapp_dual_host desktop/main.cpp)
set_target_properties(liteapp_dual_host PROPERTIES OUTPUT_NAME liteapp-dual-host)
target_compile_features(liteapp_dual_host PRIVATE cxx_std_23)
target_link_libraries(liteapp_dual_host PRIVATE
    liteapp::app
    liteapp_dual_host_assets
    liteapp::json
    liteapp::service
    liteapp::vfs
)
liteapp_link_embedded_resources(liteapp_dual_host)
liteapp_harden(liteapp_dual_host)
