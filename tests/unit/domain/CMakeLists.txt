manga_add_test(
    NAME manga_tests_domain
    SOURCES
        DomainTestMain.cpp
        UuidTest.cpp
        GeometryTest.cpp
        TranslationEntryTest.cpp
        TextLayoutTest.cpp
        ClearSettingsTest.cpp
        ComicPageTest.cpp
        ComicProjectTest.cpp
        SemanticValidatorTest.cpp
    LIBRARIES
        manga_domain
    LABELS "unit;domain"
    TIMEOUT 15
)
