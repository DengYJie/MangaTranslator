manga_add_test(
    NAME manga_tests_core
    SOURCES LoggerTest.cpp
    LIBRARIES
        manga_core
    LABELS "unit;core"
    TIMEOUT 15
)
