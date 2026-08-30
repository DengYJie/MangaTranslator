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
