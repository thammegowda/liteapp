function(liteapp_harden target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(WIN32)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /sdl /utf-8)
        target_link_options(${target} PRIVATE /guard:cf)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wconversion
            -Wformat=2
            -Wshadow
            -Wsign-conversion
            -fstack-protector-strong
        )
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_options(${target} PRIVATE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3)
            target_link_options(${target} PRIVATE -Wl,-z,relro,-z,now)
        endif()
        if(LITEAPP_ENABLE_SANITIZERS AND NOT CMAKE_CROSSCOMPILING)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

function(liteapp_portable_runtime target)
    if(NOT LITEAPP_PORTABLE_RUNTIME)
        return()
    endif()
    if(MSVC)
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(${target} PRIVATE -fPIE)
        target_link_options(${target} PRIVATE -pie -static-libstdc++ -static-libgcc)
        if(NOT CMAKE_READELF)
            message(FATAL_ERROR "Portable Linux packaging requires readelf")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DREADELF=${CMAKE_READELF}"
                "-DBINARY=$<TARGET_FILE:${target}>"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/VerifyPortableElf.cmake"
            COMMENT "Verifying ${target} bundles non-system Linux runtimes"
            VERBATIM
        )
    elseif(MINGW)
        target_link_options(${target} PRIVATE -static)
    else()
        message(FATAL_ERROR "Portable runtime packaging is not configured for ${CMAKE_SYSTEM_NAME}")
    endif()
endfunction()