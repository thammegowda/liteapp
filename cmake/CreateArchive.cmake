if(NOT DEFINED OUTPUT OR NOT DEFINED FILES)
    message(FATAL_ERROR "CreateArchive.cmake requires OUTPUT and FILES")
endif()

file(ARCHIVE_CREATE
    OUTPUT "${OUTPUT}"
    PATHS ${FILES}
    FORMAT zip
    MTIME "1970-01-01 00:00:00 UTC"
)
