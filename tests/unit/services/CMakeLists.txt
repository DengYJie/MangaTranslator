find_package(ONNXRuntime REQUIRED)

manga_add_test(
    NAME manga_tests_services
    SOURCES
        ServicesTestMain.cpp
        BoundedTaskExecutorTest.cpp
        SolidColorInpainterTest.cpp
        BaberuOcrTest.h
        BaberuOcrTest.cpp
        BaberuOcrIntegrationTest.h
        BaberuOcrIntegrationTest.cpp
        OcrModelValidatorTest.h
        OcrModelValidatorTest.cpp
        QtTextLayouterTest.cpp
    DEFINES
        MANGA_OCR_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/ocr/preprocess"
    LIBRARIES
        manga_services
        manga_application
        onnxruntime::onnxruntime
    ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1"
    LABELS "unit;services"
    TIMEOUT 120
)
