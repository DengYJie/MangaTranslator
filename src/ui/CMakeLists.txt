add_library(manga_ui INTERFACE)

target_include_directories(manga_ui
    INTERFACE
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_ui
    INTERFACE
        manga_application
        manga_data
        manga_network
        manga_services
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        FluentQt::FluentQt
)

add_subdirectory(manga)
