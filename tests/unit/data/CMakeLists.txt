qt_add_executable(manga_tests_data
    data_test_main.cpp
    ProjectSerializerTest.cpp
    FileSystemProjectRepositoryTest.cpp
    FileSystemComicImporterTest.cpp
    FileSystemDerivedAssetStoreTest.cpp
    FileSystemComicExporterTest.cpp
    FileSystemThumbnailStoreTest.h
    FileSystemThumbnailStoreTest.cpp
    QSettingsStoresTest.h
    QSettingsStoresTest.cpp
)
manga_apply_project_options(manga_tests_data)
set_target_properties(manga_tests_data PROPERTIES
    AUTOMOC ON
    WIN32_EXECUTABLE FALSE
)

target_link_libraries(manga_tests_data
    PRIVATE
        manga_data
        manga_domain
        Qt6::Test
)

manga_deploy_qt_runtime(manga_tests_data)

# Image fixtures are embedded into the test binary and read through the resource system
# under :/fixtures/. The baselines are regenerated with fixtures/generate_fixtures.py.
qt_add_resources(manga_tests_data "manga_test_image_fixtures"
    PREFIX "/fixtures"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    FILES
        fixtures/sample.png
        fixtures/sample.jpg
        fixtures/sample.webp
        fixtures/transparent.png
        fixtures/exif-orientation.jpg
        fixtures/icc-profile.png
        fixtures/gray16.png
        fixtures/animated.webp
        fixtures/cmyk.jpg
        fixtures/disguised.jpg
        fixtures/not-an-image.png
        fixtures/too-wide.png
        fixtures/too-many-pixels.png
        fixtures/pages/page1.png
        fixtures/pages/page2.png
        fixtures/pages/page10.png
)

add_test(NAME manga_tests_data COMMAND manga_tests_data)
set_tests_properties(manga_tests_data PROPERTIES
    LABELS "unit;data"
    TIMEOUT 15
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
