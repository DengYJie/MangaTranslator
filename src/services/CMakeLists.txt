find_package(ONNXRuntime REQUIRED)

add_library(manga_services STATIC)

target_sources(manga_services
    PRIVATE
        execution/BoundedTaskExecutor.cpp
        inpaint/InpaintManifest.cpp
        inpaint/InpaintModelValidator.cpp
        inpaint/OnnxLamaPageInpainter.cpp
        inpaint/OnnxLamaPageInpainter.h
        inpaint/SolidColorInpainter.cpp
        ocr/BaberuManifest.cpp
        ocr/OcrModelValidator.cpp
        ocr/OcrFactory.cpp
        ocr/BaberuVocab.cpp
        ocr/BaberuOcrPreprocessor.cpp
        ocr/OnnxMangaOcrEngine.cpp
        layout/QtTextLayouter.cpp
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
        manga_rendering
        Qt6::Core
        Qt6::Gui
    PRIVATE
        onnxruntime::onnxruntime
)
