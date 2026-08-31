add_library(manga_application STATIC
    operation/OperationProgressReporter.cpp
    workflow/OperationRegistry.cpp
    usecase/export/ExportUseCases.cpp
    usecase/region/RegionUseCases.cpp
    usecase/text/TextUseCases.cpp
)
manga_apply_project_options(manga_application)
set_target_properties(manga_application PROPERTIES AUTOMOC ON)

target_include_directories(manga_application
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_application
    PUBLIC
        manga_domain
        Qt6::Core
        Qt6::Gui
)
