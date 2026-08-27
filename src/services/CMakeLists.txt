add_library(manga_services INTERFACE)

target_include_directories(manga_services
    INTERFACE
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_services
    INTERFACE
        manga_application
)
