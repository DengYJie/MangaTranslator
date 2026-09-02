if(NOT BUILD_TESTING)
    return()
endif()

# Central test helper for all Qt6 unit and integration test targets
function(manga_add_test)
    cmake_parse_arguments(PARSE_ARGV 0 ARG
        ""
        "NAME;TIMEOUT;ENVIRONMENT"
        "SOURCES;LIBRARIES;INCLUDES;DEFINES;LABELS"
    )

    if(NOT ARG_NAME)
        message(FATAL_ERROR "manga_add_test: NAME is required")
    endif()

    if(NOT ARG_TIMEOUT)
        set(ARG_TIMEOUT 30)
    endif()

    if(NOT ARG_ENVIRONMENT)
        set(ARG_ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    qt_add_executable(${ARG_NAME} ${ARG_SOURCES})
    set_target_properties(${ARG_NAME} PROPERTIES
        AUTOMOC ON
        WIN32_EXECUTABLE FALSE
    )

    manga_apply_project_options(${ARG_NAME})
    manga_deploy_qt_runtime(${ARG_NAME})

    target_compile_definitions(${ARG_NAME} PRIVATE
        MANGA_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
        ${ARG_DEFINES}
    )

    if(ARG_INCLUDES)
        target_include_directories(${ARG_NAME} PRIVATE ${ARG_INCLUDES})
    endif()

    target_link_libraries(${ARG_NAME} PRIVATE
        Qt6::Test
        ${ARG_LIBRARIES}
    )

    add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME})
    set_tests_properties(${ARG_NAME} PROPERTIES
        ENVIRONMENT "${ARG_ENVIRONMENT}"
        TIMEOUT ${ARG_TIMEOUT}
        LABELS "${ARG_LABELS}"
    )
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)

if(TARGET MangaTranslatorApp)
    add_test(NAME manga_tests_app_smoke COMMAND MangaTranslatorApp --smoke-test)
    set_tests_properties(manga_tests_app_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "smoke;ui"
        TIMEOUT 15
    )
endif()
