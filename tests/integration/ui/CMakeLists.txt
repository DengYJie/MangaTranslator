qt_add_executable(manga_tests_ui_recovery_routes
    RecoveryRoutesTest.cpp
)
manga_apply_project_options(manga_tests_ui_recovery_routes)
set_target_properties(manga_tests_ui_recovery_routes PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_ui_recovery_routes
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
        "${PROJECT_SOURCE_DIR}/tests/unit/ui"
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
)

target_link_libraries(manga_tests_ui_recovery_routes
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        manga_data
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_recovery_routes)
add_test(NAME manga_tests_ui_recovery_routes COMMAND manga_tests_ui_recovery_routes)
set_tests_properties(manga_tests_ui_recovery_routes PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "integration;ui"
    TIMEOUT 30
)

qt_add_executable(manga_tests_integration_ui_editor_region_workflow
    EditorRegionWorkflowTest.cpp
)
manga_apply_project_options(manga_tests_integration_ui_editor_region_workflow)
set_target_properties(manga_tests_integration_ui_editor_region_workflow PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_integration_ui_editor_region_workflow
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
)

target_link_libraries(manga_tests_integration_ui_editor_region_workflow
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_integration_ui_editor_region_workflow)
add_test(NAME manga_tests_integration_ui_editor_region_workflow COMMAND manga_tests_integration_ui_editor_region_workflow)
set_tests_properties(manga_tests_integration_ui_editor_region_workflow PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "integration;ui"
    TIMEOUT 30
)
