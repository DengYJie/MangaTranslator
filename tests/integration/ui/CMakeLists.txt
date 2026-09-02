# Integration UI Tests

manga_add_test(
    NAME manga_tests_ui_recovery_routes
    SOURCES RecoveryRoutesTest.cpp
    INCLUDES
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
        "${PROJECT_SOURCE_DIR}/tests/unit/ui"
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
    LIBRARIES
        manga_ui
        manga_application
        manga_domain
        manga_data
        Qt6::Widgets
    LABELS "integration;ui"
    TIMEOUT 30
)

manga_add_test(
    NAME manga_tests_integration_ui_editor_region_workflow
    SOURCES EditorRegionWorkflowTest.cpp
    INCLUDES
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
    LIBRARIES
        manga_ui
        manga_application
        manga_domain
        Qt6::Widgets
    LABELS "integration;ui"
    TIMEOUT 30
)
