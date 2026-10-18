include(CMakeParseArguments)

if(WIN32)
    enable_language(RC)
else()
    enable_language(ASM)
endif()

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
        OUTPUT "${archive}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_root}"
        COMMAND "${CMAKE_COMMAND}"
            "-DOUTPUT=${archive}"
            "-DFILES=${ARG_FILES}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CreateArchive.cmake"
        WORKING_DIRECTORY "${ARG_ROOT}"
        DEPENDS
            ${dependencies}
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CreateArchive.cmake"
        COMMENT "Archiving ${ARG_TARGET} assets"
        VERBATIM
    )

    string(MAKE_C_IDENTIFIER "liteapp_${ARG_TARGET}_archive" LITEAPP_EMBED_SYMBOL)
    set(LITEAPP_EMBED_NAMESPACE "${ARG_NAMESPACE}")
    set(LITEAPP_EMBED_HEADER "${ARG_HEADER}")
    set(LITEAPP_EMBED_RESOURCE "${ARG_TARGET}")
    file(TO_CMAKE_PATH "${archive}" LITEAPP_EMBED_ARCHIVE)
    get_filename_component(header_directory "${header}" DIRECTORY)
    file(MAKE_DIRECTORY "${header_directory}")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbeddedArchive.h.in" "${header}" @ONLY)
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbeddedArchive.cpp.in" "${source}" @ONLY)
    if(WIN32)
        string(REPLACE "/" "\\\\" LITEAPP_EMBED_ARCHIVE_WINDOWS "${LITEAPP_EMBED_ARCHIVE}")
        set(resource "${generated_root}/${ARG_TARGET}.rc")
        configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbeddedArchiveWindows.rc.in" "${resource}" @ONLY)
    elseif(APPLE)
        set(resource "${generated_root}/${ARG_TARGET}.S")
        configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbeddedArchiveApple.S.in" "${resource}" @ONLY)
    else()
        set(resource "${generated_root}/${ARG_TARGET}.S")
        configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbeddedArchiveElf.S.in" "${resource}" @ONLY)
    endif()
    set_source_files_properties("${resource}" PROPERTIES OBJECT_DEPENDS "${archive}")

    add_library(${ARG_TARGET} OBJECT "${source}" "${resource}")
    target_include_directories(${ARG_TARGET} PUBLIC "${generated_root}")
    target_compile_features(${ARG_TARGET} PRIVATE cxx_std_23)
    liteapp_harden(${ARG_TARGET})
endfunction()
