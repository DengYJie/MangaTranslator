add_library(manga_domain INTERFACE)

target_include_directories(manga_domain
    INTERFACE
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_domain
    INTERFACE
        manga_core
)
