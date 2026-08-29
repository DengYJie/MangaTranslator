find_package(ONNXRuntime REQUIRED)

add_library(manga_services STATIC
    execution/BoundedTaskExecutor.cpp
    inpaint/SolidColorInpainter.cpp
    ocr/BaberuManifest.cpp
    ocr/BaberuVocab.cpp
    ocr/BaberuOcrPreprocessor.cpp
    ocr/OnnxMangaOcrEngine.cpp
    services.cpp
)
manga_apply_project_options(manga_services)

target_include_directories(manga_services
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_services
    PUBLIC
        manga_application
        Qt6::Core
        Qt6::Gui
    PRIVATE
        onnxruntime::onnxruntime
)
