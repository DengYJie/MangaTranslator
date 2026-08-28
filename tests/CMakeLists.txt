if(NOT BUILD_TESTING)
    return()
endif()

add_subdirectory(unit)
add_subdirectory(contract)
add_subdirectory(integration)

if(TARGET MangaTranslatorApp)
    add_test(NAME manga_tests_app_smoke COMMAND MangaTranslatorApp --smoke-test)
    set_tests_properties(manga_tests_app_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "smoke;ui"
        TIMEOUT 15
    )
endif()
