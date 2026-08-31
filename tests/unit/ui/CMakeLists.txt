qt_add_executable(manga_tests_ui
    base_view_model_test.cpp
)
manga_apply_project_options(manga_tests_ui)
set_target_properties(manga_tests_ui PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui)
add_test(NAME manga_tests_ui COMMAND manga_tests_ui)
set_tests_properties(manga_tests_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

# QTEST_MAIN generates one main() per file, so each UI test file gets its own executable.
qt_add_executable(manga_tests_ui_base_page
    base_page_test.cpp
)
manga_apply_project_options(manga_tests_ui_base_page)
set_target_properties(manga_tests_ui_base_page PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_base_page
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_base_page)
add_test(NAME manga_tests_ui_base_page COMMAND manga_tests_ui_base_page)
set_tests_properties(manga_tests_ui_base_page PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_navigation
    navigation_controller_test.cpp
)
manga_apply_project_options(manga_tests_ui_navigation)
set_target_properties(manga_tests_ui_navigation PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_navigation
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_navigation)
add_test(NAME manga_tests_ui_navigation COMMAND manga_tests_ui_navigation)
set_tests_properties(manga_tests_ui_navigation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_navigation_window
    navigation_window_test.cpp
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
    main_window_test.cpp
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
    relative_time_test.cpp
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

qt_add_executable(manga_tests_ui_home_view_model
    home_view_model_test.cpp
)
manga_apply_project_options(manga_tests_ui_home_view_model)
set_target_properties(manga_tests_ui_home_view_model PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_home_view_model
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_home_view_model)
add_test(NAME manga_tests_ui_home_view_model COMMAND manga_tests_ui_home_view_model)
set_tests_properties(manga_tests_ui_home_view_model PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_home_page
    home_page_test.cpp
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
    manga_canvas_test.cpp
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
    page_list_model_test.cpp
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
    page_thumbnail_list_test.cpp
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
    editor_view_model_test.cpp
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
    editor_page_test.cpp
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

qt_add_executable(manga_tests_ui_editor_save
    editor_save_test.cpp
)
manga_apply_project_options(manga_tests_ui_editor_save)
set_target_properties(manga_tests_ui_editor_save PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_editor_save
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)
manga_deploy_qt_runtime(manga_tests_ui_editor_save)
add_test(NAME manga_tests_ui_editor_save COMMAND manga_tests_ui_editor_save)
set_tests_properties(manga_tests_ui_editor_save PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_import_view_model
    import_view_model_test.cpp
)
manga_apply_project_options(manga_tests_ui_import_view_model)
set_target_properties(manga_tests_ui_import_view_model PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_ui_import_view_model
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
)

target_link_libraries(manga_tests_ui_import_view_model
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_import_view_model)
add_test(NAME manga_tests_ui_import_view_model COMMAND manga_tests_ui_import_view_model)
set_tests_properties(manga_tests_ui_import_view_model PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_import_page
    import_page_test.cpp
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
    region_interaction_test.cpp
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
    inspector_panel_test.cpp
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
    editor_commands_test.cpp
)
manga_apply_project_options(manga_tests_ui_editor_commands)
set_target_properties(manga_tests_ui_editor_commands PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_editor_commands
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_editor_commands)
add_test(NAME manga_tests_ui_editor_commands COMMAND manga_tests_ui_editor_commands)
set_tests_properties(manga_tests_ui_editor_commands PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_inspector_state
    inspector_state_test.cpp
)
manga_apply_project_options(manga_tests_ui_inspector_state)
set_target_properties(manga_tests_ui_inspector_state PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_inspector_state
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_inspector_state)
add_test(NAME manga_tests_ui_inspector_state COMMAND manga_tests_ui_inspector_state)
set_tests_properties(manga_tests_ui_inspector_state PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_text_edit_session
    text_edit_session_test.cpp
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

qt_add_executable(manga_tests_integration_ui_editor_region_workflow
    ../../integration/ui/editor_region_workflow_test.cpp
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
qt_add_executable(manga_tests_ui_processing_adoption
    processing_adoption_commands_test.cpp
)
manga_apply_project_options(manga_tests_ui_processing_adoption)
set_target_properties(manga_tests_ui_processing_adoption PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_processing_adoption
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_processing_adoption)
add_test(NAME manga_tests_ui_processing_adoption COMMAND manga_tests_ui_processing_adoption)
set_tests_properties(manga_tests_ui_processing_adoption PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)

qt_add_executable(manga_tests_ui_editor_processing
    editor_processing_controller_test.cpp
)
manga_apply_project_options(manga_tests_ui_editor_processing)
set_target_properties(manga_tests_ui_editor_processing PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_ui_editor_processing
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
)

target_link_libraries(manga_tests_ui_editor_processing
    PRIVATE
        manga_ui
        manga_application
        manga_domain
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_editor_processing)
add_test(NAME manga_tests_ui_editor_processing COMMAND manga_tests_ui_editor_processing)
set_tests_properties(manga_tests_ui_editor_processing PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)
