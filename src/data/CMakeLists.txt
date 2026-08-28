add_library(manga_data STATIC data.cpp)
manga_apply_project_options(manga_data)

target_include_directories(manga_data
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_data
    PUBLIC
        manga_application
        Qt6::Core
        Qt6::Sql
)
