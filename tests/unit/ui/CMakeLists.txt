qt_add_executable(manga_tests_ui_base_mvvm
    BaseMvvmTest.cpp
)
manga_apply_project_options(manga_tests_ui_base_mvvm)
set_target_properties(manga_tests_ui_base_mvvm PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_base_mvvm
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_base_mvvm)
add_test(NAME manga_tests_ui_base_mvvm COMMAND manga_tests_ui_base_mvvm)
set_tests_properties(manga_tests_ui_base_mvvm PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_navigation_controller
    NavigationControllerTest.cpp
)
manga_apply_project_options(manga_tests_ui_navigation_controller)
set_target_properties(manga_tests_ui_navigation_controller PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_navigation_controller
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_navigation_controller)
add_test(NAME manga_tests_ui_navigation_controller COMMAND manga_tests_ui_navigation_controller)
set_tests_properties(manga_tests_ui_navigation_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_navigation_window
    NavigationWindowTest.cpp
)
manga_apply_project_options(manga_tests_ui_navigation_window)
set_target_properties(manga_tests_ui_navigation_window PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_navigation_window
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_navigation_window)
add_test(NAME manga_tests_ui_navigation_window COMMAND manga_tests_ui_navigation_window)
set_tests_properties(manga_tests_ui_navigation_window PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_main_window
    MainWindowTest.cpp
)
manga_apply_project_options(manga_tests_ui_main_window)
set_target_properties(manga_tests_ui_main_window PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_main_window
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_main_window)
add_test(NAME manga_tests_ui_main_window COMMAND manga_tests_ui_main_window)
set_tests_properties(manga_tests_ui_main_window PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_relative_time
    RelativeTimeTest.cpp
)
manga_apply_project_options(manga_tests_ui_relative_time)
set_target_properties(manga_tests_ui_relative_time PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_relative_time
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_relative_time)
add_test(NAME manga_tests_ui_relative_time COMMAND manga_tests_ui_relative_time)
set_tests_properties(manga_tests_ui_relative_time PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_home_page
    HomePageTest.cpp
)
manga_apply_project_options(manga_tests_ui_home_page)
set_target_properties(manga_tests_ui_home_page PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_home_page
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_home_page)
add_test(NAME manga_tests_ui_home_page COMMAND manga_tests_ui_home_page)
set_tests_properties(manga_tests_ui_home_page PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_manga_canvas
    MangaCanvasTest.cpp
)
manga_apply_project_options(manga_tests_ui_manga_canvas)
set_target_properties(manga_tests_ui_manga_canvas PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_manga_canvas
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_manga_canvas)
add_test(NAME manga_tests_ui_manga_canvas COMMAND manga_tests_ui_manga_canvas)
set_tests_properties(manga_tests_ui_manga_canvas PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_page_list_model
    PageListModelTest.cpp
)
manga_apply_project_options(manga_tests_ui_page_list_model)
set_target_properties(manga_tests_ui_page_list_model PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_page_list_model
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_page_list_model)
add_test(NAME manga_tests_ui_page_list_model COMMAND manga_tests_ui_page_list_model)
set_tests_properties(manga_tests_ui_page_list_model PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_page_thumbnail_list
    PageThumbnailListTest.cpp
)
manga_apply_project_options(manga_tests_ui_page_thumbnail_list)
set_target_properties(manga_tests_ui_page_thumbnail_list PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_page_thumbnail_list
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_page_thumbnail_list)
add_test(NAME manga_tests_ui_page_thumbnail_list COMMAND manga_tests_ui_page_thumbnail_list)
set_tests_properties(manga_tests_ui_page_thumbnail_list PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_editor_view_model
    EditorViewModelTest.cpp
)
manga_apply_project_options(manga_tests_ui_editor_view_model)
set_target_properties(manga_tests_ui_editor_view_model PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_editor_view_model
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_editor_view_model)
add_test(NAME manga_tests_ui_editor_view_model COMMAND manga_tests_ui_editor_view_model)
set_tests_properties(manga_tests_ui_editor_view_model PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_editor_page
    EditorPageTest.cpp
)
manga_apply_project_options(manga_tests_ui_editor_page)
set_target_properties(manga_tests_ui_editor_page PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_editor_page
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_editor_page)
add_test(NAME manga_tests_ui_editor_page COMMAND manga_tests_ui_editor_page)
set_tests_properties(manga_tests_ui_editor_page PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_import_page
    ImportPageTest.cpp
)
manga_apply_project_options(manga_tests_ui_import_page)
set_target_properties(manga_tests_ui_import_page PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_ui_import_page
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
)

target_link_libraries(manga_tests_ui_import_page
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_import_page)
add_test(NAME manga_tests_ui_import_page COMMAND manga_tests_ui_import_page)
set_tests_properties(manga_tests_ui_import_page PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_region_interaction
    RegionInteractionTest.cpp
)
manga_apply_project_options(manga_tests_ui_region_interaction)
set_target_properties(manga_tests_ui_region_interaction PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_region_interaction
    PRIVATE
        manga_ui_manga
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_region_interaction)
add_test(NAME manga_tests_ui_region_interaction COMMAND manga_tests_ui_region_interaction)
set_tests_properties(manga_tests_ui_region_interaction PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_inspector_panel
    InspectorPanelTest.cpp
)
manga_apply_project_options(manga_tests_ui_inspector_panel)
set_target_properties(manga_tests_ui_inspector_panel PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_inspector_panel
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_inspector_panel)
add_test(NAME manga_tests_ui_inspector_panel COMMAND manga_tests_ui_inspector_panel)
set_tests_properties(manga_tests_ui_inspector_panel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_editor_commands
    EditorCommandsTest.cpp
)
manga_apply_project_options(manga_tests_ui_editor_commands)
set_target_properties(manga_tests_ui_editor_commands PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_ui_editor_commands
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_ui_editor_commands
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        manga_rendering
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_editor_commands)
add_test(NAME manga_tests_ui_editor_commands COMMAND manga_tests_ui_editor_commands)
set_tests_properties(manga_tests_ui_editor_commands PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 30
)

qt_add_executable(manga_tests_ui_text_edit_session
    TextEditSessionTest.cpp
)
manga_apply_project_options(manga_tests_ui_text_edit_session)
set_target_properties(manga_tests_ui_text_edit_session PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_text_edit_session
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_text_edit_session)
add_test(NAME manga_tests_ui_text_edit_session COMMAND manga_tests_ui_text_edit_session)
set_tests_properties(manga_tests_ui_text_edit_session PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_editor_processing_controller
    EditorProcessingControllerTest.cpp
)
manga_apply_project_options(manga_tests_ui_editor_processing_controller)
set_target_properties(manga_tests_ui_editor_processing_controller PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_ui_editor_processing_controller
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
)

target_link_libraries(manga_tests_ui_editor_processing_controller
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_editor_processing_controller)
add_test(NAME manga_tests_ui_editor_processing_controller COMMAND manga_tests_ui_editor_processing_controller)
set_tests_properties(manga_tests_ui_editor_processing_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_operation_list_model
    OperationListModelTest.cpp
)
manga_apply_project_options(manga_tests_ui_operation_list_model)
set_target_properties(manga_tests_ui_operation_list_model PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_operation_list_model
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_operation_list_model)
add_test(NAME manga_tests_ui_operation_list_model COMMAND manga_tests_ui_operation_list_model)
set_tests_properties(manga_tests_ui_operation_list_model PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_app_status_task_strip
    AppStatusTaskStripTest.cpp
)
manga_apply_project_options(manga_tests_ui_app_status_task_strip)
set_target_properties(manga_tests_ui_app_status_task_strip PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_app_status_task_strip
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_app_status_task_strip)
add_test(NAME manga_tests_ui_app_status_task_strip COMMAND manga_tests_ui_app_status_task_strip)
set_tests_properties(manga_tests_ui_app_status_task_strip PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_settings_page
    SettingsPageTest.cpp
)
manga_apply_project_options(manga_tests_ui_settings_page)
set_target_properties(manga_tests_ui_settings_page PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_ui_settings_page
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_ui_settings_page
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_settings_page)
add_test(NAME manga_tests_ui_settings_page COMMAND manga_tests_ui_settings_page)
set_tests_properties(manga_tests_ui_settings_page PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_recovery_presenter
    RecoveryPresenterTest.cpp
)
manga_apply_project_options(manga_tests_ui_recovery_presenter)
set_target_properties(manga_tests_ui_recovery_presenter PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_recovery_presenter
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_recovery_presenter)
add_test(NAME manga_tests_ui_recovery_presenter COMMAND manga_tests_ui_recovery_presenter)
set_tests_properties(manga_tests_ui_recovery_presenter PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_responsive_layout
    ResponsiveLayoutTest.cpp
)
manga_apply_project_options(manga_tests_ui_responsive_layout)
set_target_properties(manga_tests_ui_responsive_layout PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_ui_responsive_layout
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_ui_responsive_layout
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_responsive_layout)
add_test(NAME manga_tests_ui_responsive_layout COMMAND manga_tests_ui_responsive_layout)
set_tests_properties(manga_tests_ui_responsive_layout PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_accessibility_contract
    AccessibilityContractTest.cpp
)
manga_apply_project_options(manga_tests_ui_accessibility_contract)
set_target_properties(manga_tests_ui_accessibility_contract PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_ui_accessibility_contract
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_ui_accessibility_contract
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_accessibility_contract)
add_test(NAME manga_tests_ui_accessibility_contract COMMAND manga_tests_ui_accessibility_contract)
set_tests_properties(manga_tests_ui_accessibility_contract PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_export_snapshot_builder
    ExportSnapshotBuilderTest.cpp
)
manga_apply_project_options(manga_tests_ui_export_snapshot_builder)
set_target_properties(manga_tests_ui_export_snapshot_builder PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_ui_export_snapshot_builder
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_ui_export_snapshot_builder
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        manga_rendering
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_export_snapshot_builder)
add_test(NAME manga_tests_ui_export_snapshot_builder COMMAND manga_tests_ui_export_snapshot_builder)
set_tests_properties(manga_tests_ui_export_snapshot_builder PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)
