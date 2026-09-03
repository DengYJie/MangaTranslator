add_library(manga_core STATIC
    core.cpp
    DebugOutputSink.h
    Logger.cpp
    Logger.h
)
manga_apply_project_options(manga_core)

if(WIN32)
    target_sources(manga_core PRIVATE DebugOutputSinkWin.cpp)
else()
    target_sources(manga_core PRIVATE DebugOutputSinkFallback.cpp)
endif()

target_include_directories(manga_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_core
    PUBLIC
        Qt6::Core
)
