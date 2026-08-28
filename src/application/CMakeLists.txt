add_library(manga_application STATIC application.cpp)
manga_apply_project_options(manga_application)

target_include_directories(manga_application
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_application
    PUBLIC
        manga_domain
)
