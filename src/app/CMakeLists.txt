add_library(manga_app STATIC
    Application.cpp
    Application.h
    ApplicationContext.cpp
    ApplicationContext.h
)
manga_apply_project_options(manga_app)

target_include_directories(manga_app
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_app
    PUBLIC
        manga_application
        manga_core
        manga_data
        manga_network
        manga_services
        manga_ui
        Qt6::Widgets
        FluentQt::FluentQt
)

qt_add_executable(MangaTranslatorApp
    main.cpp
)
# Debug links against the console subsystem so the log stream reaches CLion's Run window, Visual
# Studio's integrated terminal and a plain shell. Other configurations stay a GUI subsystem.
set_target_properties(MangaTranslatorApp PROPERTIES WIN32_EXECUTABLE "$<NOT:$<CONFIG:Debug>>")
manga_apply_project_options(MangaTranslatorApp)

target_link_libraries(MangaTranslatorApp
    PRIVATE
        manga_app
)

manga_deploy_qt_runtime(MangaTranslatorApp)
