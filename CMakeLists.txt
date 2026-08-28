cmake_minimum_required(VERSION 3.28)

project(MangaTranslator
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTING "Build MangaTranslator tests" ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Network Sql Test)

find_package(FluentQt CONFIG QUIET)
if(NOT TARGET FluentQt::FluentQt)
    include(FetchContent)

    set(FLUENT_QT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FLUENT_QT_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
    set(FLUENT_QT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FLUENT_QT_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(fluentqt
        GIT_REPOSITORY https://github.com/calvinhxx/Fluent-Qt.git
        GIT_TAG v1.7.3
    )
    FetchContent_MakeAvailable(fluentqt)
endif()

if(NOT TARGET FluentQt::FluentQt)
    message(FATAL_ERROR
        "FluentQt was not found. Set FluentQt_DIR or CMAKE_PREFIX_PATH "
        "to a FluentQt installation."
    )
endif()

qt_standard_project_setup()

find_program(MANGA_WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt
    HINTS
        "${Qt6_DIR}/../../../bin"
        "${CMAKE_PREFIX_PATH}/bin"
)

find_file(MANGA_QT_OFFSCREEN_PLUGIN
    NAMES qoffscreen.dll qoffscreend.dll
    HINTS
        "${Qt6_DIR}/../../../plugins/platforms"
        "${CMAKE_PREFIX_PATH}/plugins/platforms"
)

if(MANGA_QT_OFFSCREEN_PLUGIN)
    get_filename_component(MANGA_QT_OFFSCREEN_PLUGIN_NAME
        "${MANGA_QT_OFFSCREEN_PLUGIN}"
        NAME
    )
endif()

function(manga_deploy_qt_runtime target)
    if(WIN32 AND MANGA_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${MANGA_WINDEPLOYQT_EXECUTABLE}"
                --verbose 0
                --no-compiler-runtime
                --no-opengl-sw
                "$<TARGET_FILE:${target}>"
            COMMENT "Deploying Qt runtime for ${target}"
        )
    endif()

    if(WIN32 AND MANGA_QT_OFFSCREEN_PLUGIN)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MANGA_QT_OFFSCREEN_PLUGIN}"
                "$<TARGET_FILE_DIR:${target}>/platforms/${MANGA_QT_OFFSCREEN_PLUGIN_NAME}"
            COMMENT "Deploying Qt offscreen plugin for ${target}"
        )
    endif()
endfunction()

add_subdirectory(src/core)
add_subdirectory(src/domain)
add_subdirectory(src/application)
add_subdirectory(src/services)
add_subdirectory(src/data)
add_subdirectory(src/network)
add_subdirectory(src/ui)
add_subdirectory(src/app)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
