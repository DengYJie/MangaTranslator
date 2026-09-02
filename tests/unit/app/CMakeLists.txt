manga_add_test(
    NAME manga_tests_app
    SOURCES ApplicationContextTest.cpp
    INCLUDES
        "${PROJECT_SOURCE_DIR}/tests/unit/network"
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
    LIBRARIES
        manga_app
        manga_application
        manga_domain
        manga_ui
    LABELS "unit;app"
    TIMEOUT 15
)
