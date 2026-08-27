add_library(manga_ui_manga INTERFACE)

target_include_directories(manga_ui_manga
    INTERFACE
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_ui_manga
    INTERFACE
        manga_ui
        Qt6::Gui
        Qt6::Widgets
)
