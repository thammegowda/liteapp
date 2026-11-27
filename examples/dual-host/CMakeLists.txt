cmake_minimum_required(VERSION 3.25 FATAL_ERROR)
project(liteapp_dual_host LANGUAGES CXX)

set(LITEAPP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "LiteApp source directory")
set(LITEAPP_BUILD_TESTS OFF CACHE BOOL "Build LiteApp SDK tests" FORCE)
add_subdirectory("${LITEAPP_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/liteapp" EXCLUDE_FROM_ALL)

liteapp_embed_manifest(
    TARGET liteapp_dual_host_assets
    NAMESPACE liteapp::dual_host_assets
    HEADER liteapp/dual_host_assets.h
    MANIFEST "${CMAKE_CURRENT_LIST_DIR}/web/assets.txt"
    BASE "${LITEAPP_SOURCE_DIR}"
)

add_executable(liteapp_dual_host desktop/main.cpp)
set_target_properties(liteapp_dual_host PROPERTIES OUTPUT_NAME liteapp-dual-host)
target_compile_features(liteapp_dual_host PRIVATE cxx_std_23)
target_link_libraries(liteapp_dual_host PRIVATE
    liteapp::app
    liteapp_dual_host_assets
    liteapp::json
    liteapp::service
)
liteapp_link_embedded_resources(liteapp_dual_host)
liteapp_harden(liteapp_dual_host)
