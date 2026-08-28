qt_add_executable(manga_tests_contract contract_init_test.cpp)
manga_apply_project_options(manga_tests_contract)
set_target_properties(manga_tests_contract PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_contract
    PRIVATE
        manga_domain
        manga_application
        Qt6::Test
)

manga_deploy_qt_runtime(manga_tests_contract)
add_test(NAME manga_tests_contract COMMAND manga_tests_contract)
set_tests_properties(manga_tests_contract PROPERTIES
    LABELS "contract"
    TIMEOUT 15
)
