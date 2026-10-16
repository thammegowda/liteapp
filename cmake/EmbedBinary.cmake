foreach(required IN ITEMS INPUT OUTPUT_HEADER OUTPUT_SOURCE HEADER NAMESPACE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "EmbedBinary.cmake requires ${required}")
    endif()
endforeach()

file(READ "${INPUT}" archive_hex HEX)
string(LENGTH "${archive_hex}" archive_hex_length)
math(EXPR archive_size "${archive_hex_length} / 2")
set(archive_bytes "")
set(offset 0)
while(offset LESS archive_hex_length)
    string(SUBSTRING "${archive_hex}" ${offset} 32 row)
    string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "std::byte{0x\\1}, " row "${row}")
    string(APPEND archive_bytes "    ${row}\n")
    math(EXPR offset "${offset} + 32")
endwhile()

get_filename_component(header_directory "${OUTPUT_HEADER}" DIRECTORY)
get_filename_component(source_directory "${OUTPUT_SOURCE}" DIRECTORY)
file(MAKE_DIRECTORY "${header_directory}" "${source_directory}")
string(CONCAT header_contents
    "#pragma once\n\n"
    "#include <cstddef>\n"
    "#include <span>\n\n"
    "namespace ${NAMESPACE} {\n\n"
    "[[nodiscard]] std::span<const std::byte> archive() noexcept;\n\n"
    "}  // namespace ${NAMESPACE}\n"
)
string(CONCAT source_contents
    "#include <${HEADER}>\n\n"
    "#include <array>\n\n"
    "namespace ${NAMESPACE} {\n"
    "namespace {\n\n"
    "constexpr std::array<std::byte, ${archive_size}> archive_bytes{\n${archive_bytes}};\n\n"
    "}  // namespace\n\n"
    "std::span<const std::byte> archive() noexcept { return archive_bytes; }\n\n"
    "}  // namespace ${NAMESPACE}\n"
)
file(WRITE "${OUTPUT_HEADER}" "${header_contents}")
file(WRITE "${OUTPUT_SOURCE}" "${source_contents}")
