qt_add_executable(manga_tests_domain
    DomainTestMain.cpp
    UuidTest.cpp
    GeometryTest.cpp
    TranslationEntryTest.cpp
    TextLayoutTest.cpp
    ClearSettingsTest.cpp
    ComicPageTest.cpp
    ComicProjectTest.cpp
    SemanticValidatorTest.cpp
)
manga_apply_project_options(manga_tests_domain)
set_target_properties(manga_tests_domain PROPERTIES
    AUTOMOC ON
    WIN32_EXECUTABLE FALSE
)

target_link_libraries(manga_tests_domain
    PRIVATE
        manga_domain
        Qt6::Test
)

manga_deploy_qt_runtime(manga_tests_domain)

add_test(NAME manga_tests_domain COMMAND manga_tests_domain)
set_tests_properties(manga_tests_domain PROPERTIES
    LABELS "unit;domain"
    TIMEOUT 15
)
