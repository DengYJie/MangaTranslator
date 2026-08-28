add_library(manga_services STATIC services.cpp)
manga_apply_project_options(manga_services)

target_include_directories(manga_services
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_services
    PUBLIC
        manga_application
)
