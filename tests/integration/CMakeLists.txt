qt_add_executable(manga_tests_integration integration_init_test.cpp)
manga_apply_project_options(manga_tests_integration)
set_target_properties(manga_tests_integration PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_integration
    PRIVATE
        manga_domain
        manga_application
        manga_services
        manga_data
        manga_network
        Qt6::Test
)

manga_deploy_qt_runtime(manga_tests_integration)
add_test(NAME manga_tests_integration COMMAND manga_tests_integration)
set_tests_properties(manga_tests_integration PROPERTIES
    LABELS "integration"
    TIMEOUT 30
)

add_subdirectory(ui)
