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

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Network Sql)
if(BUILD_TESTING)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Test)
endif()

find_package(FluentQt CONFIG QUIET)
if(NOT TARGET FluentQt::FluentQt)
    include(FetchContent)

    set(FLUENT_QT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FLUENT_QT_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
    set(FLUENT_QT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FLUENT_QT_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(fluentqt
        GIT_REPOSITORY https://github.com/calvinhxx/Fluent-Qt.git
        GIT_TAG 5602f5271507972af8292c469972995401dc2e67 # v1.7.4
        UPDATE_DISCONNECTED TRUE
    )
    FetchContent_MakeAvailable(fluentqt)
endif()

if(NOT TARGET FluentQt::FluentQt)
    message(FATAL_ERROR
        "FluentQt was not found. Set FluentQt_DIR or CMAKE_PREFIX_PATH "
        "to a FluentQt installation."
    )
endif()

find_package(QWindowKit CONFIG QUIET)
if(NOT TARGET QWindowKit::Widgets)
    include(FetchContent)

    set(QWINDOWKIT_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(QWINDOWKIT_BUILD_WIDGETS ON CACHE BOOL "" FORCE)
    set(QWINDOWKIT_BUILD_QUICK OFF CACHE BOOL "" FORCE)
    set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(QWINDOWKIT_BUILD_DOCUMENTATIONS OFF CACHE BOOL "" FORCE)
    set(QWINDOWKIT_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(qwindowkit
        GIT_REPOSITORY https://github.com/stdware/qwindowkit.git
        GIT_TAG 35e88f3655720ed0537c7dd1dde243bf4a70c94c # pre-1.5.0, same pin as ForgeAI
        UPDATE_DISCONNECTED TRUE
    )
    FetchContent_MakeAvailable(qwindowkit)
endif()

if(NOT TARGET QWindowKit::Widgets)
    message(FATAL_ERROR
        "QWindowKit was not found. Set QWindowKit_DIR or CMAKE_PREFIX_PATH "
        "to a QWindowKit installation."
    )
endif()

add_library(manga_project_options INTERFACE)
target_compile_options(manga_project_options INTERFACE
    "$<$<CXX_COMPILER_ID:MSVC>:/W4>"
    "$<$<CXX_COMPILER_ID:MSVC>:/permissive->"
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
)

function(manga_apply_project_options target)
    target_link_libraries(${target} PRIVATE manga_project_options)
endfunction()

find_program(MANGA_WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt
    HINTS
        "${Qt6_DIR}/../../../bin"
        "${CMAKE_PREFIX_PATH}/bin"
)

find_file(MANGA_QT_OFFSCREEN_PLUGIN_RELEASE
    NAMES qoffscreen.dll
    HINTS
        "${Qt6_DIR}/../../../plugins/platforms"
        "${CMAKE_PREFIX_PATH}/plugins/platforms"
)

find_file(MANGA_QT_OFFSCREEN_PLUGIN_DEBUG
    NAMES qoffscreend.dll
    HINTS
        "${Qt6_DIR}/../../../plugins/platforms"
        "${CMAKE_PREFIX_PATH}/plugins/platforms"
)

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

    if(WIN32 AND MANGA_QT_OFFSCREEN_PLUGIN_RELEASE AND MANGA_QT_OFFSCREEN_PLUGIN_DEBUG)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<IF:$<CONFIG:Debug>,${MANGA_QT_OFFSCREEN_PLUGIN_DEBUG},${MANGA_QT_OFFSCREEN_PLUGIN_RELEASE}>"
                "$<TARGET_FILE_DIR:${target}>/platforms/$<IF:$<CONFIG:Debug>,qoffscreend.dll,qoffscreen.dll>"
            COMMENT "Deploying the Qt offscreen plugin for ${target}"
        )
    elseif(WIN32 AND MANGA_QT_OFFSCREEN_PLUGIN_RELEASE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MANGA_QT_OFFSCREEN_PLUGIN_RELEASE}"
                "$<TARGET_FILE_DIR:${target}>/platforms/qoffscreen.dll"
            COMMENT "Deploying the Qt offscreen plugin for ${target}"
        )
    endif()
endfunction()

add_subdirectory(src/core)
add_subdirectory(src/domain)
add_subdirectory(src/application)
add_subdirectory(src/rendering)
add_subdirectory(src/services)
add_subdirectory(src/data)
add_subdirectory(src/network)
add_subdirectory(src/ui)
add_subdirectory(src/app)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
