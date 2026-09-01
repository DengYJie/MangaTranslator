qt_add_executable(manga_tests_app application_context_test.cpp)
manga_apply_project_options(manga_tests_app)
set_target_properties(manga_tests_app PROPERTIES AUTOMOC ON)
target_compile_definitions(manga_tests_app PRIVATE QT_NO_KEYWORDS)

target_include_directories(manga_tests_app
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/network"
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
)

target_link_libraries(manga_tests_app
    PRIVATE
        manga_app
        manga_application
        manga_domain
        manga_ui
        Qt6::Test
)

manga_deploy_qt_runtime(manga_tests_app)
add_test(NAME manga_tests_app COMMAND manga_tests_app)
set_tests_properties(manga_tests_app PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;app"
    TIMEOUT 15
)
