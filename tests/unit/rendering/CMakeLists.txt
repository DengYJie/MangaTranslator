manga_add_test(
    NAME manga_tests_rendering
    SOURCES
        RenderingTestMain.cpp
        TextLayoutEngineTest.h
        TextLayoutEngineTest.cpp
        TextRasterizerTest.h
        TextRasterizerTest.cpp
    INCLUDES
        "${PROJECT_SOURCE_DIR}/src"
    DEFINES
        SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
    LIBRARIES
        manga_rendering
        Qt6::Gui
    LABELS "unit;rendering"
    TIMEOUT 30
)
