if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
    qt_add_executable(MangaTranslatorApp
        main.cpp
    )

    target_link_libraries(MangaTranslatorApp
        PRIVATE
            manga_application
            manga_data
            manga_network
            manga_services
            manga_ui
            Qt6::Widgets
    )

    manga_deploy_qt_runtime(MangaTranslatorApp)
else()
    message(STATUS "MangaTranslatorApp is deferred until src/app/main.cpp exists")
endif()
