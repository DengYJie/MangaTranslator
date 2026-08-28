qt_add_executable(manga_tests_data
    data_test_main.cpp
    ProjectSerializerTest.cpp
)
manga_apply_project_options(manga_tests_data)
set_target_properties(manga_tests_data PROPERTIES
    AUTOMOC ON
    WIN32_EXECUTABLE FALSE
)

target_link_libraries(manga_tests_data
    PRIVATE
        manga_data
        manga_domain
        Qt6::Test
)

manga_deploy_qt_runtime(manga_tests_data)

add_test(NAME manga_tests_data COMMAND manga_tests_data)
set_tests_properties(manga_tests_data PROPERTIES
    LABELS "unit;data"
    TIMEOUT 15
)
