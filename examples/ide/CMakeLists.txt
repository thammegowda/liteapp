cmake_minimum_required(VERSION 3.25 FATAL_ERROR)
project(liteapp_ide LANGUAGES CXX)

set(LITEAPP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "LiteApp source directory")
set(LITEAPP_BUILD_TESTS OFF CACHE BOOL "Build LiteApp SDK tests" FORCE)
add_subdirectory("${LITEAPP_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/liteapp" EXCLUDE_FROM_ALL)

liteapp_embed_manifest(
    TARGET liteapp_ide_assets
    NAMESPACE liteapp::ide_assets
    HEADER liteapp/ide_assets.h
    MANIFEST "${CMAKE_CURRENT_LIST_DIR}/web/assets.txt"
    BASE "${LITEAPP_SOURCE_DIR}"
)

add_executable(liteapp_ide desktop/main.cpp)
set_target_properties(liteapp_ide PROPERTIES OUTPUT_NAME liteapp-ide)
target_compile_features(liteapp_ide PRIVATE cxx_std_23)
target_link_libraries(liteapp_ide PRIVATE
    liteapp::app
    liteapp_ide_assets
    liteapp::json
    liteapp::service
    liteapp::workspace
)
liteapp_link_embedded_resources(liteapp_ide)
liteapp_harden(liteapp_ide)
