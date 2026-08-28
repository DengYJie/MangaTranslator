add_library(manga_domain STATIC domain.cpp)
manga_apply_project_options(manga_domain)

target_include_directories(manga_domain
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
