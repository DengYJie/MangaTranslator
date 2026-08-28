add_library(manga_core STATIC core.cpp)
manga_apply_project_options(manga_core)

target_include_directories(manga_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
