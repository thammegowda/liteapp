set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "macOS target architecture")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest supported macOS version")

file(GLOB _local_toolchains "${CMAKE_CURRENT_LIST_DIR}/../../.toolchains/osxcross*/target/bin")
set(_toolchain_hints "$ENV{LITEAPP_OSXCROSS_ROOT}/target/bin" ${_local_toolchains})

find_program(_clang NAMES oa64-clang HINTS ${_toolchain_hints})
find_program(_clangxx NAMES oa64-clang++ HINTS ${_toolchain_hints})

if(NOT _clang OR NOT _clangxx)
    message(FATAL_ERROR
        "osxcross was not found. Build it under liteapp/.toolchains/ or set "
        "LITEAPP_OSXCROSS_ROOT to its root directory.")
endif()

set(CMAKE_C_COMPILER "${_clang}")
set(CMAKE_CXX_COMPILER "${_clangxx}")

get_filename_component(_toolchain_bin "${_clangxx}" DIRECTORY)
get_filename_component(_toolchain_target "${_toolchain_bin}" DIRECTORY)

function(_find_osxcross_tool output name)
    file(GLOB candidates "${_toolchain_bin}/arm64-apple-darwin*-${name}")
    list(SORT candidates COMPARE NATURAL ORDER DESCENDING)
    list(LENGTH candidates candidate_count)
    if(candidate_count EQUAL 0)
        message(FATAL_ERROR "osxcross does not provide an ARM64 ${name} wrapper")
    endif()
    list(GET candidates 0 candidate)
    set(${output} "${candidate}" PARENT_SCOPE)
endfunction()

_find_osxcross_tool(_ar ar)
_find_osxcross_tool(_ranlib ranlib)
_find_osxcross_tool(_install_name_tool install_name_tool)
set(CMAKE_AR "${_ar}" CACHE FILEPATH "osxcross archiver" FORCE)
set(CMAKE_RANLIB "${_ranlib}" CACHE FILEPATH "osxcross archive indexer" FORCE)
set(CMAKE_INSTALL_NAME_TOOL "${_install_name_tool}" CACHE FILEPATH
    "osxcross install-name editor" FORCE)

file(GLOB _sdk_roots "${_toolchain_target}/SDK/MacOSX*.sdk")
list(SORT _sdk_roots COMPARE NATURAL ORDER DESCENDING)
list(LENGTH _sdk_roots _sdk_count)
if(_sdk_count EQUAL 0)
    message(FATAL_ERROR "osxcross does not contain a macOS SDK under ${_toolchain_target}/SDK")
endif()
list(GET _sdk_roots 0 _sdk_root)
set(CMAKE_OSX_SYSROOT "${_sdk_root}" CACHE PATH "macOS SDK" FORCE)
set(CMAKE_FIND_ROOT_PATH "${_sdk_root}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)