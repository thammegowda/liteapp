include(CMakeParseArguments)
set_property(GLOBAL PROPERTY LITEAPP_EMBED_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

macro(liteapp_embed_archive)
    cmake_parse_arguments(ARG "" "TARGET;NAMESPACE;HEADER;ROOT" "FILES" ${ARGN})
    foreach(required IN ITEMS TARGET NAMESPACE HEADER ROOT)
        if(NOT ARG_${required})
            message(FATAL_ERROR "liteapp_embed_archive requires ${required}")
        endif()
    endforeach()
    if(NOT ARG_FILES)
        message(FATAL_ERROR "liteapp_embed_archive requires FILES")
    endif()
    get_property(LITEAPP_EMBED_CMAKE_DIR GLOBAL PROPERTY LITEAPP_EMBED_CMAKE_DIR)
    if(WIN32)
        enable_language(RC)
    else()
        enable_language(ASM)
    endif()

    list(SORT ARG_FILES)
    set(dependencies)
    foreach(file IN LISTS ARG_FILES)
        set(path "${ARG_ROOT}/${file}")
        get_source_file_property(generated "${path}" GENERATED)
        if(IS_ABSOLUTE "${file}" OR (NOT EXISTS "${path}" AND NOT generated) OR IS_DIRECTORY "${path}")
            message(FATAL_ERROR "Invalid embedded asset: ${file}")
        endif()
        list(APPEND dependencies "${path}")
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
            -P "${LITEAPP_EMBED_CMAKE_DIR}/CreateArchive.cmake"
        WORKING_DIRECTORY "${ARG_ROOT}"
        DEPENDS
            ${dependencies}
            "${LITEAPP_EMBED_CMAKE_DIR}/CreateArchive.cmake"
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
    configure_file("${LITEAPP_EMBED_CMAKE_DIR}/EmbeddedArchive.h.in" "${header}" @ONLY)
    configure_file("${LITEAPP_EMBED_CMAKE_DIR}/EmbeddedArchive.cpp.in" "${source}" @ONLY)
    if(WIN32)
        string(REPLACE "/" "\\\\" LITEAPP_EMBED_ARCHIVE_WINDOWS "${LITEAPP_EMBED_ARCHIVE}")
        set(resource "${generated_root}/${ARG_TARGET}.rc")
        configure_file("${LITEAPP_EMBED_CMAKE_DIR}/EmbeddedArchiveWindows.rc.in" "${resource}" @ONLY)
    elseif(APPLE)
        set(resource "${generated_root}/${ARG_TARGET}.S")
        configure_file("${LITEAPP_EMBED_CMAKE_DIR}/EmbeddedArchiveApple.S.in" "${resource}" @ONLY)
    else()
        set(resource "${generated_root}/${ARG_TARGET}.S")
        configure_file("${LITEAPP_EMBED_CMAKE_DIR}/EmbeddedArchiveElf.S.in" "${resource}" @ONLY)
    endif()
    set_source_files_properties("${resource}" PROPERTIES OBJECT_DEPENDS "${archive}")

    if(WIN32)
        add_library(${ARG_TARGET} STATIC "${source}" "${resource}")
        target_link_options(${ARG_TARGET} INTERFACE "/WHOLEARCHIVE:$<TARGET_FILE:${ARG_TARGET}>")
    else()
        add_library(${ARG_TARGET} OBJECT "${source}" "${resource}")
    endif()
    target_include_directories(${ARG_TARGET} PUBLIC "${generated_root}")
    target_compile_features(${ARG_TARGET} PRIVATE cxx_std_23)
    liteapp_harden(${ARG_TARGET})
endmacro()
