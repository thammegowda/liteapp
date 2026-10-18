include(GoogleTest)

liteapp_embed_archive(
    TARGET liteapp_test_assets
    NAMESPACE liteapp::test_assets
    HEADER liteapp/test_assets.h
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets/vfs"
    FILES index.html module.mjs other.html
)

add_executable(liteapp_tests
    test_database.cpp
    test_filesystem.cpp
    test_generator.cpp
    test_http.cpp
    test_random.cpp
    test_service.cpp
    test_vfs.cpp
)
target_link_libraries(liteapp_tests
    PRIVATE
        GTest::gtest_main
        liteapp::app
        liteapp::db
        liteapp::fs
        liteapp::platform
        liteapp::service
        liteapp::httplib
        liteapp_test_assets
)
liteapp_link_embedded_resources(liteapp_tests)
liteapp_harden(liteapp_tests)

gtest_discover_tests(liteapp_tests)