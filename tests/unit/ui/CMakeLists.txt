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
