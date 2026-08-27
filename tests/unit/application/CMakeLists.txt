add_library(manga_tests_unit_application INTERFACE)

target_link_libraries(manga_tests_unit_application
    INTERFACE
        manga_application
        Qt6::Test
)
