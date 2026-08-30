add_library(manga_ui STATIC
    base/BasePage.cpp
    base/BasePage.h
    base/BaseViewModel.h
)
manga_apply_project_options(manga_ui)
set_target_properties(manga_ui PROPERTIES AUTOMOC ON)

target_include_directories(manga_ui
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

add_subdirectory(manga)

target_link_libraries(manga_ui
    PUBLIC
        manga_application
        manga_rendering
        manga_ui_manga
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        FluentQt::FluentQt
)
