set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(_target x86_64-w64-mingw32)
file(GLOB _local_toolchains "${CMAKE_CURRENT_LIST_DIR}/../../.toolchains/llvm-mingw*/bin")
set(_toolchain_hints "$ENV{LITEAPP_LLVM_MINGW_ROOT}/bin" ${_local_toolchains})

find_program(_clang NAMES ${_target}-clang HINTS ${_toolchain_hints})
find_program(_clangxx NAMES ${_target}-clang++ HINTS ${_toolchain_hints})
find_program(_windres NAMES ${_target}-windres HINTS ${_toolchain_hints})

if(NOT _clang OR NOT _clangxx)
    message(FATAL_ERROR
        "LLVM-MinGW was not found. Extract it under liteapp/.toolchains/ or set "
        "LITEAPP_LLVM_MINGW_ROOT to its root directory.")
endif()

set(CMAKE_C_COMPILER "${_clang}")
set(CMAKE_CXX_COMPILER "${_clangxx}")
set(CMAKE_RC_COMPILER "${_windres}")

get_filename_component(_toolchain_bin "${_clangxx}" DIRECTORY)
get_filename_component(_toolchain_root "${_toolchain_bin}" DIRECTORY)
set(CMAKE_FIND_ROOT_PATH "${_toolchain_root}/${_target}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")