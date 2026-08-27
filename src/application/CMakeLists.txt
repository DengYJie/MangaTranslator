add_library(manga_application INTERFACE)

target_include_directories(manga_application
    INTERFACE
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_application
    INTERFACE
        manga_domain
)
