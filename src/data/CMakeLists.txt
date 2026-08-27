add_library(manga_data INTERFACE)

target_include_directories(manga_data
    INTERFACE
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_data
    INTERFACE
        manga_application
        Qt6::Core
        Qt6::Sql
)
