add_executable(manga_tests_rendering
    RenderingTestMain.cpp
    TextLayoutEngineTest.h
    TextLayoutEngineTest.cpp
    TextRasterizerTest.h
    TextRasterizerTest.cpp
)

manga_apply_project_options(manga_tests_rendering)
set_target_properties(manga_tests_rendering PROPERTIES AUTOMOC ON)

target_include_directories(manga_tests_rendering PRIVATE "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(manga_tests_rendering
    PRIVATE
        manga_rendering
        Qt6::Test
        Qt6::Gui
)

# For tests, we use the same resources
target_compile_definitions(manga_tests_rendering PRIVATE SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}")

manga_deploy_qt_runtime(manga_tests_rendering)
add_test(NAME manga_tests_rendering COMMAND manga_tests_rendering)
set_tests_properties(manga_tests_rendering PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;rendering"
)
