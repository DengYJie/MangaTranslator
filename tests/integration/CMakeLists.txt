# Integration Tests

set(INTEGRATION_DEFAULT_INCLUDES
    "${PROJECT_SOURCE_DIR}/tests/unit/application"
    "${PROJECT_SOURCE_DIR}/tests"
    "${PROJECT_SOURCE_DIR}/tests/unit"
)

set(INTEGRATION_DEFAULT_LIBS
    manga_domain
    manga_application
    manga_services
    manga_data
    manga_rendering
    manga_ui
    Qt6::Widgets
)

manga_add_test(
    NAME manga_tests_integration_manual_offline
    SOURCES ManualOfflineScenarioTest.cpp
    INCLUDES ${INTEGRATION_DEFAULT_INCLUDES}
    LIBRARIES ${INTEGRATION_DEFAULT_LIBS}
    LABELS "integration"
    TIMEOUT 60
)

manga_add_test(
    NAME manga_tests_integration_ocr
    SOURCES OcrScenarioTest.cpp
    INCLUDES ${INTEGRATION_DEFAULT_INCLUDES}
    LIBRARIES ${INTEGRATION_DEFAULT_LIBS}
    LABELS "integration"
    TIMEOUT 60
)

manga_add_test(
    NAME manga_tests_integration_translation
    SOURCES TranslationScenarioTest.cpp
    INCLUDES ${INTEGRATION_DEFAULT_INCLUDES}
    LIBRARIES ${INTEGRATION_DEFAULT_LIBS} manga_network Qt6::Network
    LABELS "integration"
    TIMEOUT 60
)

manga_add_test(
    NAME manga_tests_integration_processing
    SOURCES ProcessingScenarioTest.cpp
    INCLUDES ${INTEGRATION_DEFAULT_INCLUDES}
    LIBRARIES ${INTEGRATION_DEFAULT_LIBS}
    LABELS "integration"
    TIMEOUT 60
)

manga_add_test(
    NAME manga_tests_integration_page_export
    SOURCES PageExportScenarioTest.cpp
    INCLUDES ${INTEGRATION_DEFAULT_INCLUDES}
    LIBRARIES ${INTEGRATION_DEFAULT_LIBS}
    LABELS "integration"
    TIMEOUT 60
)

manga_add_test(
    NAME manga_tests_integration_persistence
    SOURCES PersistenceLifecycleTest.cpp
    INCLUDES ${INTEGRATION_DEFAULT_INCLUDES}
    LIBRARIES ${INTEGRATION_DEFAULT_LIBS}
    LABELS "integration"
    TIMEOUT 60
)

manga_add_test(
    NAME manga_tests_integration_session
    SOURCES SessionLifecycleTest.cpp
    INCLUDES ${INTEGRATION_DEFAULT_INCLUDES} "${PROJECT_SOURCE_DIR}/tests/unit/ui"
    LIBRARIES manga_app ${INTEGRATION_DEFAULT_LIBS} manga_network
    LABELS "integration"
    TIMEOUT 60
)

manga_add_test(
    NAME manga_tests_integration_recovery
    SOURCES RecoveryMatrixTest.cpp
    INCLUDES ${INTEGRATION_DEFAULT_INCLUDES} "${PROJECT_SOURCE_DIR}/tests/unit/ui"
    LIBRARIES manga_app ${INTEGRATION_DEFAULT_LIBS} manga_network Qt6::Network
    LABELS "integration"
    TIMEOUT 60
)

add_subdirectory(ui)
