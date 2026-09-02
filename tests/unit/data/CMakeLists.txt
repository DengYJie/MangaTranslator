manga_add_test(
    NAME manga_tests_data
    SOURCES
        DataTestMain.cpp
        ProjectSerializerTest.cpp
        FileSystemProjectRepositoryTest.cpp
        FileSystemComicImporterTest.cpp
        FileSystemDerivedAssetStoreTest.cpp
        FileSystemComicExporterTest.cpp
        FileSystemThumbnailStoreTest.h
        FileSystemThumbnailStoreTest.cpp
        QSettingsStoresTest.h
        QSettingsStoresTest.cpp
    LIBRARIES
        manga_data
        manga_domain
    LABELS "unit;data"
    TIMEOUT 15
)

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
