cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(liteapp VERSION 0.1.0 LANGUAGES CXX)
    include(CTest)
endif()

include(cmake/Safety.cmake)
include(cmake/EmbedArchive.cmake)

if(NOT CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif()
endif()

option(LITEAPP_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(LITEAPP_PORTABLE_RUNTIME "Bundle non-system executable runtimes" OFF)

enable_language(C)
find_package(Threads REQUIRED)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/httplib/httplib.h")
    message(FATAL_ERROR "Missing liteapp/libs/httplib/httplib.h")
endif()

add_library(liteapp_httplib INTERFACE)
add_library(liteapp::httplib ALIAS liteapp_httplib)
target_include_directories(liteapp_httplib SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/libs/httplib")
target_link_libraries(liteapp_httplib INTERFACE Threads::Threads)
target_compile_features(liteapp_httplib INTERFACE cxx_std_23)
if(WIN32)
    target_link_libraries(liteapp_httplib INTERFACE ws2_32)
    target_compile_definitions(liteapp_httplib INTERFACE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/json/CMakeLists.txt")
    message(FATAL_ERROR "Missing JSON submodule. Run: git submodule update --init --recursive")
endif()
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(libs/json EXCLUDE_FROM_ALL)
add_library(liteapp_json INTERFACE)
add_library(liteapp::json ALIAS liteapp_json)
target_link_libraries(liteapp_json INTERFACE nlohmann_json::nlohmann_json)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/miniz/miniz.c")
    message(FATAL_ERROR "Missing liteapp/libs/miniz amalgamation")
endif()
add_library(liteapp_miniz STATIC libs/miniz/miniz.c)
target_include_directories(liteapp_miniz SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs/miniz")
target_compile_definitions(liteapp_miniz PUBLIC
    MINIZ_NO_DEFLATE_APIS
    MINIZ_NO_STDIO
    MINIZ_NO_TIME
    MINIZ_NO_ZLIB_APIS
)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/sqlite/sqlite3.c")
    message(FATAL_ERROR "Missing liteapp/libs/sqlite amalgamation")
endif()
add_library(liteapp_sqlite STATIC libs/sqlite/sqlite3.c)
target_include_directories(liteapp_sqlite SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs/sqlite")
target_compile_definitions(liteapp_sqlite PRIVATE
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_DQS=0
    SQLITE_ENABLE_FTS5
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_SECURE_DELETE=1
    SQLITE_TEMP_STORE=2
    SQLITE_THREADSAFE=1
)
target_link_libraries(liteapp_sqlite PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

add_library(liteapp_core
    src/liteapp/core/random.cpp
)
add_library(liteapp::core ALIAS liteapp_core)
target_include_directories(liteapp_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(WIN32)
    target_link_libraries(liteapp_core PRIVATE bcrypt)
elseif(APPLE)
    target_link_libraries(liteapp_core PRIVATE "-framework Security")
endif()
liteapp_harden(liteapp_core)

add_library(liteapp_db src/liteapp/db/database.cpp)
add_library(liteapp::db ALIAS liteapp_db)
target_include_directories(liteapp_db PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(liteapp_db PUBLIC liteapp::core PRIVATE liteapp_sqlite)
liteapp_harden(liteapp_db)

add_library(liteapp_vfs src/liteapp/vfs/filesystem.cpp)
add_library(liteapp::vfs ALIAS liteapp_vfs)
target_include_directories(liteapp_vfs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(liteapp_vfs PUBLIC liteapp::core PRIVATE liteapp_miniz)
liteapp_harden(liteapp_vfs)

add_library(liteapp_http
    src/liteapp/http/server.cpp
)
add_library(liteapp::http ALIAS liteapp_http)
target_include_directories(liteapp_http PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(liteapp_http PUBLIC liteapp::core liteapp::vfs PRIVATE liteapp::httplib)
liteapp_harden(liteapp_http)

set(LITEAPP_NATIVE_WATCHER_SOURCE)
if(WIN32)
    set(LITEAPP_NATIVE_WATCHER_SOURCE src/liteapp/fs/watcher_windows.cpp)
elseif(APPLE)
    set(LITEAPP_NATIVE_WATCHER_SOURCE src/liteapp/fs/watcher_macos.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LITEAPP_NATIVE_WATCHER_SOURCE src/liteapp/fs/watcher_linux.cpp)
else()
    message(FATAL_ERROR "Liteapp filesystem watching currently supports Windows, macOS, and Linux")
endif()
add_library(liteapp_fs
    src/liteapp/fs/watcher.cpp
    ${LITEAPP_NATIVE_WATCHER_SOURCE}
)
add_library(liteapp::fs ALIAS liteapp_fs)
target_include_directories(liteapp_fs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(liteapp_fs PUBLIC liteapp::core PRIVATE Threads::Threads)
if(APPLE)
    target_link_libraries(liteapp_fs PRIVATE "-framework CoreFoundation" "-framework CoreServices")
endif()
liteapp_harden(liteapp_fs)

if(WIN32)
    set(LITEAPP_WEBVIEW2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libs/webview2")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
        set(LITEAPP_WEBVIEW2_ROOT "${LITEAPP_WEBVIEW2_ROOT}/arm64")
    endif()
    if(NOT EXISTS "${LITEAPP_WEBVIEW2_ROOT}/WebView2Loader.dll")
        message(FATAL_ERROR
            "Missing WebView2Loader.dll for ${CMAKE_SYSTEM_PROCESSOR} under "
            "${LITEAPP_WEBVIEW2_ROOT}")
    endif()
    liteapp_embed_archive(
        TARGET liteapp_webview2_loader
        NAMESPACE liteapp::embedded_webview2
        HEADER liteapp/embedded_webview2.h
        ROOT "${LITEAPP_WEBVIEW2_ROOT}"
        FILES WebView2Loader.dll
    )
    set(LITEAPP_SINGLE_INSTANCE_SOURCE src/liteapp/platform/single_instance_windows.cpp)
    set(LITEAPP_WEBVIEW_WINDOW_SOURCE src/liteapp/platform/webview_window_windows.cpp)
else()
    set(LITEAPP_SINGLE_INSTANCE_SOURCE src/liteapp/platform/single_instance.cpp)
    set(LITEAPP_WEBVIEW_WINDOW_SOURCE src/liteapp/platform/webview_window.cpp)
endif()
add_library(liteapp_platform
    src/liteapp/platform/openers.cpp
    ${LITEAPP_SINGLE_INSTANCE_SOURCE}
    ${LITEAPP_WEBVIEW_WINDOW_SOURCE}
)
add_library(liteapp::platform ALIAS liteapp_platform)
target_include_directories(liteapp_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(liteapp_platform PUBLIC liteapp::core)
if(WIN32)
    target_include_directories(liteapp_platform SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/webview2")
    target_link_libraries(liteapp_platform PRIVATE liteapp::vfs liteapp_webview2_loader ole32 shell32 uuid)
endif()
liteapp_harden(liteapp_platform)

if(BUILD_TESTING)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/googletest/CMakeLists.txt")
        message(FATAL_ERROR "Missing GoogleTest submodule. Run: git submodule update --init --recursive")
    endif()
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(libs/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()