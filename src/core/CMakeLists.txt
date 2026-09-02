add_library(manga_core STATIC
    core.cpp
    Logger.cpp
    Logger.h
)
manga_apply_project_options(manga_core)

target_include_directories(manga_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_core
    PUBLIC
        Qt6::Core
)
