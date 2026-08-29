qt_add_executable(manga_tests_services
    services_test_main.cpp
    BoundedTaskExecutorTest.cpp
)
manga_apply_project_options(manga_tests_services)
set_target_properties(manga_tests_services PROPERTIES
    AUTOMOC ON
    WIN32_EXECUTABLE FALSE
)

target_link_libraries(manga_tests_services
    PRIVATE
        manga_services
        manga_application
        Qt6::Test
)

manga_deploy_qt_runtime(manga_tests_services)
add_test(NAME manga_tests_services COMMAND manga_tests_services)
set_tests_properties(manga_tests_services PROPERTIES
    LABELS "unit;services"
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1"
)
