add_library(manga_data STATIC
    import/FileSystemComicImporter.cpp
    import/FileSystemPageImageLoader.cpp
    project/FileSystemProjectRepository.cpp
    project/ProjectIoCoordinator.cpp
    project/ProjectMigrator.cpp
    project/PathSafety.cpp
    project/ProjectSchemaValidator.cpp
    project/ProjectSerializer.cpp
    DataErrorMapping.cpp
    image/ImageNormalization.cpp
)
manga_apply_project_options(manga_data)

target_include_directories(manga_data
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_data
    PUBLIC
        manga_domain
        manga_application
        Qt6::Core
        Qt6::Gui
        Qt6::Sql
)
