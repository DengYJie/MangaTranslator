# FindONNXRuntime.cmake
#
# Locates the ONNX Runtime library.
#
# This module defines the following variables:
#   ONNXRuntime_FOUND - True if ONNX Runtime was found
#   ONNXRuntime_INCLUDE_DIRS - Include directories for ONNX Runtime
#   ONNXRuntime_LIBRARIES - Libraries to link against ONNX Runtime
#
# This module provides the following imported target:
#   onnxruntime::onnxruntime
#
# Hints:
#   Set ONNXRUNTIME_ROOT_DIR to point to the root directory of a pre-built ONNX Runtime release.
#   Set MANGA_DOWNLOAD_ONNXRUNTIME=ON to explicitly allow a verified pre-built release download.
#

option(MANGA_DOWNLOAD_ONNXRUNTIME "Allow CMake to download a verified ONNX Runtime release" OFF)

# First try the upstream or vcpkg-provided Config module
find_package(onnxruntime CONFIG QUIET)
if(onnxruntime_FOUND)
    set(ONNXRuntime_FOUND TRUE)
    # The config module usually provides onnxruntime::onnxruntime
    return()
endif()

find_path(ONNXRuntime_INCLUDE_DIR

    NAMES onnxruntime_cxx_api.h
    HINTS
        "${ONNXRUNTIME_ROOT_DIR}"
        "$ENV{ONNXRUNTIME_ROOT_DIR}"
    PATH_SUFFIXES include
    DOC "ONNX Runtime include directory"
)

find_library(ONNXRuntime_LIBRARY
    NAMES onnxruntime
    HINTS
        "${ONNXRUNTIME_ROOT_DIR}"
        "$ENV{ONNXRUNTIME_ROOT_DIR}"
    PATH_SUFFIXES lib lib64
    DOC "ONNX Runtime library"
)

# On Windows, we also want to find the .dll to copy it or set it in the target
if(WIN32)
    find_file(ONNXRuntime_DLL
        NAMES onnxruntime.dll
        HINTS
            "${ONNXRUNTIME_ROOT_DIR}"
            "$ENV{ONNXRUNTIME_ROOT_DIR}"
        PATH_SUFFIXES lib bin
        DOC "ONNX Runtime DLL"
    )
endif()

# Downloads are opt-in so ordinary configure and rebuilds remain offline.
if(MANGA_DOWNLOAD_ONNXRUNTIME AND
   (NOT ONNXRuntime_INCLUDE_DIR OR NOT ONNXRuntime_LIBRARY OR (WIN32 AND NOT ONNXRuntime_DLL)))
    message(STATUS "ONNX Runtime not found locally. Downloading prebuilt binaries...")

    set(ONNXRUNTIME_VERSION "1.26.0")
    set(ONNXRUNTIME_BASE_DIR "${CMAKE_BINARY_DIR}/_deps/onnxruntime-local")

    if(WIN32)
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip")
        set(ONNXRUNTIME_ARCHIVE "${ONNXRUNTIME_BASE_DIR}/onnxruntime.zip")
        set(ONNXRUNTIME_EXTRACT_DIR "${ONNXRUNTIME_BASE_DIR}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}")
        set(ONNXRUNTIME_HASH "6ebe99b5564bf4d029b6e93eac9ff423682b6212eade769e9ca3f685eaf500b4")
    elseif(APPLE)
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz")
        set(ONNXRUNTIME_ARCHIVE "${ONNXRUNTIME_BASE_DIR}/onnxruntime.tgz")
        set(ONNXRUNTIME_EXTRACT_DIR "${ONNXRUNTIME_BASE_DIR}/onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}")
        set(ONNXRUNTIME_HASH "7a1280bbb1701ea514f71828765237e7896e0f2e1cd332f1f70dbd5c3e33aca3")
    else()
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz")
        set(ONNXRUNTIME_ARCHIVE "${ONNXRUNTIME_BASE_DIR}/onnxruntime.tgz")
        set(ONNXRUNTIME_EXTRACT_DIR "${ONNXRUNTIME_BASE_DIR}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}")
        set(ONNXRUNTIME_HASH "1254da24fb389cf39dc0ff3451ab48301740ffbfcbaf646849df92f80ee92c57")
    endif()

    if(NOT EXISTS "${ONNXRUNTIME_EXTRACT_DIR}/include/onnxruntime_cxx_api.h")
        file(MAKE_DIRECTORY "${ONNXRUNTIME_BASE_DIR}")
        message(STATUS "Downloading ONNX Runtime from ${ONNXRUNTIME_URL} ...")
        file(DOWNLOAD "${ONNXRUNTIME_URL}" "${ONNXRUNTIME_ARCHIVE}"
             EXPECTED_HASH SHA256=${ONNXRUNTIME_HASH}
             SHOW_PROGRESS STATUS DL_STATUS)

        list(GET DL_STATUS 0 DL_ERROR)
        if(DL_ERROR)
            message(FATAL_ERROR "Failed to download ONNX Runtime: ${DL_STATUS}")
        endif()

        message(STATUS "Extracting ONNX Runtime (skipping debug symbols) ...")
        if(WIN32)
            set(EXTRACT_PATTERNS
                "onnxruntime-*/include/*"
                "onnxruntime-*/lib/*.dll"
                "onnxruntime-*/lib/*.lib"
            )
        elseif(APPLE)
            set(EXTRACT_PATTERNS
                "onnxruntime-*/include/*"
                "onnxruntime-*/lib/*.dylib"
            )
        else()
            set(EXTRACT_PATTERNS
                "onnxruntime-*/include/*"
                "onnxruntime-*/lib/*.so*"
                "onnxruntime-*/lib/*.a"
            )
        endif()


        file(ARCHIVE_EXTRACT
            INPUT "${ONNXRUNTIME_ARCHIVE}"
            DESTINATION "${ONNXRUNTIME_BASE_DIR}"
            PATTERNS ${EXTRACT_PATTERNS}
        )


        file(REMOVE "${ONNXRUNTIME_ARCHIVE}")
    endif()

    set(ONNXRUNTIME_ROOT_DIR "${ONNXRUNTIME_EXTRACT_DIR}" CACHE PATH "ONNX Runtime root directory" FORCE)

    # Run the find logic again
    unset(ONNXRuntime_INCLUDE_DIR CACHE)
    unset(ONNXRuntime_LIBRARY CACHE)
    unset(ONNXRuntime_DLL CACHE)

    find_path(ONNXRuntime_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h
        HINTS "${ONNXRUNTIME_ROOT_DIR}" PATH_SUFFIXES include NO_DEFAULT_PATH
    )
    find_library(ONNXRuntime_LIBRARY
        NAMES onnxruntime
        HINTS "${ONNXRUNTIME_ROOT_DIR}" PATH_SUFFIXES lib lib64 NO_DEFAULT_PATH
    )
    if(WIN32)
        find_file(ONNXRuntime_DLL
            NAMES onnxruntime.dll
            HINTS "${ONNXRUNTIME_ROOT_DIR}" PATH_SUFFIXES lib bin NO_DEFAULT_PATH
        )
    endif()
endif()

include(FindPackageHandleStandardArgs)
if(WIN32)
    find_package_handle_standard_args(ONNXRuntime
        REQUIRED_VARS ONNXRuntime_LIBRARY ONNXRuntime_INCLUDE_DIR ONNXRuntime_DLL
    )
else()
    find_package_handle_standard_args(ONNXRuntime
        REQUIRED_VARS ONNXRuntime_LIBRARY ONNXRuntime_INCLUDE_DIR
    )
endif()

if(ONNXRuntime_FOUND AND NOT TARGET onnxruntime::onnxruntime)
    if(WIN32)
        add_library(onnxruntime::onnxruntime SHARED IMPORTED)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_IMPLIB "${ONNXRuntime_LIBRARY}"
            IMPORTED_LOCATION "${ONNXRuntime_DLL}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRuntime_INCLUDE_DIR}"
        )
    else()
        add_library(onnxruntime::onnxruntime UNKNOWN IMPORTED)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_LOCATION "${ONNXRuntime_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRuntime_INCLUDE_DIR}"
        )
    endif()

    set(ONNXRuntime_INCLUDE_DIRS "${ONNXRuntime_INCLUDE_DIR}")
    set(ONNXRuntime_LIBRARIES onnxruntime::onnxruntime)
endif()

mark_as_advanced(ONNXRuntime_INCLUDE_DIR ONNXRuntime_LIBRARY ONNXRuntime_DLL)
