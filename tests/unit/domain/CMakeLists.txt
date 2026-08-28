add_executable(manga_tests_domain domain_init_test.cpp)
manga_apply_project_options(manga_tests_domain)

target_link_libraries(manga_tests_domain
    PRIVATE
        manga_domain
)

add_test(NAME manga_tests_domain COMMAND manga_tests_domain)
set_tests_properties(manga_tests_domain PROPERTIES
    LABELS "unit;domain"
    TIMEOUT 15
)
