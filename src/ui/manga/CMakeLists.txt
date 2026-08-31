add_library(manga_ui_manga STATIC
    CanvasZOrder.h
    MangaCanvas.cpp
    MangaCanvas.h
    RegionInteraction.cpp
    RegionInteraction.h
    RegionOverlayItem.cpp
    RegionOverlayItem.h
    SelectionHandleItem.cpp
    SelectionHandleItem.h
)
manga_apply_project_options(manga_ui_manga)
set_target_properties(manga_ui_manga PROPERTIES AUTOMOC ON)

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
