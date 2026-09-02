add_library(manga_rendering STATIC)

target_sources(manga_rendering
    PRIVATE
        FontRegistry.cpp
        FontRegistry.h
        inpaint/StrokeMaskRasterizer.cpp
        inpaint/StrokeMaskRasterizer.h
        TextLayoutEngine.cpp
        TextRasterizer.cpp
)

target_link_libraries(manga_rendering
    PUBLIC
        manga_domain
        Qt6::Core
        Qt6::Gui
)

manga_apply_project_options(manga_rendering)

target_compile_definitions(manga_rendering
    PRIVATE
        MANGA_RES_FONTS_DIR="${CMAKE_SOURCE_DIR}/res/fonts"
)

# Embed punctuation offset table using qt_add_resources without a .qrc file
qt_add_resources(manga_rendering "manga_rendering_assets"
    PREFIX "/manga_rendering"
    BASE "${CMAKE_SOURCE_DIR}/res"
    FILES
        "${CMAKE_SOURCE_DIR}/res/text/vertical-punctuation-v1.json"
)
