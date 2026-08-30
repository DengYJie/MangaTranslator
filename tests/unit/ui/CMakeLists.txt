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
