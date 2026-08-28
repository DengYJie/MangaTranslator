qt_add_executable(manga_tests_application application_init_test.cpp)
manga_apply_project_options(manga_tests_application)
set_target_properties(manga_tests_application PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_application
    PRIVATE
        manga_application
        Qt6::Test
)

manga_deploy_qt_runtime(manga_tests_application)
add_test(NAME manga_tests_application COMMAND manga_tests_application)
set_tests_properties(manga_tests_application PROPERTIES
    LABELS "unit;application"
    TIMEOUT 15
)
