add_library(manga_tests_unit_ui INTERFACE)

target_link_libraries(manga_tests_unit_ui
    INTERFACE
        manga_ui
        Qt6::Test
)
