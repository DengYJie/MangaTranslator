add_library(manga_tests_unit_domain INTERFACE)

target_link_libraries(manga_tests_unit_domain
    INTERFACE
        manga_domain
        Qt6::Test
)
