add_library(manga_ui_manga STATIC manga.cpp)
manga_apply_project_options(manga_ui_manga)

target_include_directories(manga_ui_manga
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_ui_manga
    PUBLIC
        manga_application
        Qt6::Gui
        Qt6::Widgets
        FluentQt::FluentQt
)
