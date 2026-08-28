add_library(manga_domain STATIC
    model/Uuid.cpp
    model/Geometry.cpp
    model/LanguageCode.cpp
    model/TranslationEntry.cpp
    model/TextLayout.cpp
    model/TextRegion.cpp
    model/ComicPage.cpp
    model/ComicProject.cpp
    validation/SemanticValidator.cpp
)
manga_apply_project_options(manga_domain)

target_include_directories(manga_domain
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
