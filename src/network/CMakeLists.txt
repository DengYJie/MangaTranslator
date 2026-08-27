add_library(manga_network INTERFACE)

target_include_directories(manga_network
    INTERFACE
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_network
    INTERFACE
        manga_application
        Qt6::Core
        Qt6::Network
)
