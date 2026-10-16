include(CMakeParseArguments)

function(liteapp_embed_archive)
    cmake_parse_arguments(ARG "" "TARGET;NAMESPACE;HEADER;ROOT" "FILES" ${ARGN})
    foreach(required IN ITEMS TARGET NAMESPACE HEADER ROOT)
        if(NOT ARG_${required})
            message(FATAL_ERROR "liteapp_embed_archive requires ${required}")
        endif()
    endforeach()
    if(NOT ARG_FILES)
        message(FATAL_ERROR "liteapp_embed_archive requires FILES")
    endif()

    list(SORT ARG_FILES)
    set(dependencies)
    foreach(file IN LISTS ARG_FILES)
        if(IS_ABSOLUTE "${file}" OR NOT EXISTS "${ARG_ROOT}/${file}" OR IS_DIRECTORY "${ARG_ROOT}/${file}")
            message(FATAL_ERROR "Invalid embedded asset: ${file}")
        endif()
        list(APPEND dependencies "${ARG_ROOT}/${file}")
    endforeach()

    set(generated_root "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(archive "${generated_root}/${ARG_TARGET}.zip")
    set(header "${generated_root}/${ARG_HEADER}")
    set(source "${generated_root}/${ARG_TARGET}.cpp")
    add_custom_command(
        OUTPUT "${archive}" "${header}" "${source}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_root}"
        COMMAND "${CMAKE_COMMAND}"
            "-DOUTPUT=${archive}"
            "-DFILES=${ARG_FILES}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CreateArchive.cmake"
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${archive}"
            "-DOUTPUT_HEADER=${header}"
            "-DOUTPUT_SOURCE=${source}"
            "-DHEADER=${ARG_HEADER}"
            "-DNAMESPACE=${ARG_NAMESPACE}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedBinary.cmake"
        WORKING_DIRECTORY "${ARG_ROOT}"
        DEPENDS
            ${dependencies}
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CreateArchive.cmake"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedBinary.cmake"
        COMMENT "Embedding ${ARG_TARGET} assets"
        VERBATIM
    )
    add_library(${ARG_TARGET} STATIC "${source}")
    target_include_directories(${ARG_TARGET} PUBLIC "${generated_root}")
    liteapp_harden(${ARG_TARGET})
endfunction()
