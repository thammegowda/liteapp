foreach(required IN ITEMS READELF BINARY)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "VerifyPortableElf.cmake requires ${required}")
    endif()
endforeach()

execute_process(
    COMMAND "${READELF}" -l "${BINARY}"
    RESULT_VARIABLE program_result
    OUTPUT_VARIABLE program_headers
    ERROR_VARIABLE program_error
)
if(NOT program_result EQUAL 0)
    message(FATAL_ERROR "Failed to inspect ${BINARY}: ${program_error}")
endif()
if(NOT program_headers MATCHES "INTERP")
    message(FATAL_ERROR "Portable Linux executable must use the system glibc loader")
endif()

execute_process(
    COMMAND "${READELF}" -d "${BINARY}"
    RESULT_VARIABLE dynamic_result
    OUTPUT_VARIABLE dynamic_section
    ERROR_VARIABLE dynamic_error
)
if(NOT dynamic_result EQUAL 0)
    message(FATAL_ERROR "Failed to inspect ${BINARY}: ${dynamic_error}")
endif()
if(NOT dynamic_section MATCHES "Shared library: \\[libc\\.so\\.6\\]")
    message(FATAL_ERROR "Portable Linux executable must use the system libc")
endif()

string(REGEX MATCHALL "Shared library: \\[[^]]+\\]" dependencies "${dynamic_section}")
foreach(dependency IN LISTS dependencies)
    if(NOT dependency MATCHES
         "^Shared library: \\[(libc\\.so\\.6|libm\\.so\\.6|libpthread\\.so\\.0|libdl\\.so\\.2|librt\\.so\\.1|ld-linux[^]]*)\\]$")
        message(FATAL_ERROR "Portable Linux executable has non-system dependency: ${dependency}")
    endif()
endforeach()