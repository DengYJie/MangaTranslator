qt_add_executable(manga_tests_integration_e01_manual_offline
    e01_manual_offline_test.cpp
)
manga_apply_project_options(manga_tests_integration_e01_manual_offline)
set_target_properties(manga_tests_integration_e01_manual_offline PROPERTIES AUTOMOC ON)

target_compile_definitions(manga_tests_integration_e01_manual_offline
    PRIVATE
        MANGA_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)

target_include_directories(manga_tests_integration_e01_manual_offline
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_integration_e01_manual_offline
    PRIVATE
        manga_domain
        manga_application
        manga_services
        manga_data
        manga_rendering
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_integration_e01_manual_offline)
add_test(NAME manga_tests_integration_e01_manual_offline COMMAND manga_tests_integration_e01_manual_offline)
set_tests_properties(manga_tests_integration_e01_manual_offline PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "integration"
    TIMEOUT 60
)

qt_add_executable(manga_tests_integration_e02_ocr
    e02_ocr_scenario_test.cpp
)
manga_apply_project_options(manga_tests_integration_e02_ocr)
set_target_properties(manga_tests_integration_e02_ocr PROPERTIES AUTOMOC ON)

target_compile_definitions(manga_tests_integration_e02_ocr
    PRIVATE
        MANGA_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)

target_include_directories(manga_tests_integration_e02_ocr
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_integration_e02_ocr
    PRIVATE
        manga_domain
        manga_application
        manga_services
        manga_data
        manga_rendering
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_integration_e02_ocr)
add_test(NAME manga_tests_integration_e02_ocr COMMAND manga_tests_integration_e02_ocr)
set_tests_properties(manga_tests_integration_e02_ocr PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "integration"
    TIMEOUT 60
)

qt_add_executable(manga_tests_integration_e03_translation
    e03_translation_scenario_test.cpp
)
manga_apply_project_options(manga_tests_integration_e03_translation)
set_target_properties(manga_tests_integration_e03_translation PROPERTIES AUTOMOC ON)

target_compile_definitions(manga_tests_integration_e03_translation
    PRIVATE
        MANGA_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)

target_include_directories(manga_tests_integration_e03_translation
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_integration_e03_translation
    PRIVATE
        manga_domain
        manga_application
        manga_services
        manga_data
        manga_rendering
        manga_ui
        manga_network
        Qt6::Test
        Qt6::Widgets
        Qt6::Network
)

manga_deploy_qt_runtime(manga_tests_integration_e03_translation)
add_test(NAME manga_tests_integration_e03_translation COMMAND manga_tests_integration_e03_translation)
set_tests_properties(manga_tests_integration_e03_translation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "integration"
    TIMEOUT 60
)

qt_add_executable(manga_tests_integration_e04_processing
    e04_processing_scenario_test.cpp
)
manga_apply_project_options(manga_tests_integration_e04_processing)
set_target_properties(manga_tests_integration_e04_processing PROPERTIES AUTOMOC ON)

target_compile_definitions(manga_tests_integration_e04_processing
    PRIVATE
        MANGA_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)

target_include_directories(manga_tests_integration_e04_processing
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_integration_e04_processing
    PRIVATE
        manga_domain
        manga_application
        manga_services
        manga_data
        manga_rendering
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_integration_e04_processing)
add_test(NAME manga_tests_integration_e04_processing COMMAND manga_tests_integration_e04_processing)
set_tests_properties(manga_tests_integration_e04_processing PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "integration"
    TIMEOUT 60
)

qt_add_executable(manga_tests_integration_e05_export
    e05_page_export_test.cpp
)
manga_apply_project_options(manga_tests_integration_e05_export)
set_target_properties(manga_tests_integration_e05_export PROPERTIES AUTOMOC ON)

target_compile_definitions(manga_tests_integration_e05_export
    PRIVATE
        MANGA_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)

target_include_directories(manga_tests_integration_e05_export
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_integration_e05_export
    PRIVATE
        manga_domain
        manga_application
        manga_services
        manga_data
        manga_rendering
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_integration_e05_export)
add_test(NAME manga_tests_integration_e05_export COMMAND manga_tests_integration_e05_export)
set_tests_properties(manga_tests_integration_e05_export PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "integration"
    TIMEOUT 60
)

qt_add_executable(manga_tests_integration_persistence
    persistence_lifecycle_test.cpp
)
manga_apply_project_options(manga_tests_integration_persistence)
set_target_properties(manga_tests_integration_persistence PROPERTIES AUTOMOC ON)

target_compile_definitions(manga_tests_integration_persistence
    PRIVATE
        MANGA_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)

target_include_directories(manga_tests_integration_persistence
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_integration_persistence
    PRIVATE
        manga_domain
        manga_application
        manga_services
        manga_data
        manga_rendering
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_integration_persistence)
add_test(NAME manga_tests_integration_persistence COMMAND manga_tests_integration_persistence)
set_tests_properties(manga_tests_integration_persistence PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "integration"
    TIMEOUT 60
)

qt_add_executable(manga_tests_integration_session
    session_lifecycle_test.cpp
)
manga_apply_project_options(manga_tests_integration_session)
set_target_properties(manga_tests_integration_session PROPERTIES AUTOMOC ON)

target_compile_definitions(manga_tests_integration_session
    PRIVATE
        MANGA_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)

target_include_directories(manga_tests_integration_session
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests/unit/ui"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_integration_session
    PRIVATE
        manga_app
        manga_domain
        manga_application
        manga_services
        manga_data
        manga_rendering
        manga_ui
        manga_network
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_integration_session)
add_test(NAME manga_tests_integration_session COMMAND manga_tests_integration_session)
set_tests_properties(manga_tests_integration_session PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "integration"
    TIMEOUT 60
)

qt_add_executable(manga_tests_integration_recovery
    recovery_matrix_test.cpp
)
manga_apply_project_options(manga_tests_integration_recovery)
set_target_properties(manga_tests_integration_recovery PROPERTIES AUTOMOC ON)

target_compile_definitions(manga_tests_integration_recovery
    PRIVATE
        MANGA_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)

target_include_directories(manga_tests_integration_recovery
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application"
        "${PROJECT_SOURCE_DIR}/tests/unit/ui"
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/tests/unit"
)

target_link_libraries(manga_tests_integration_recovery
    PRIVATE
        manga_app
        manga_domain
        manga_application
        manga_services
        manga_data
        manga_rendering
        manga_ui
        manga_network
        Qt6::Test
        Qt6::Widgets
        Qt6::Network
)

manga_deploy_qt_runtime(manga_tests_integration_recovery)
add_test(NAME manga_tests_integration_recovery COMMAND manga_tests_integration_recovery)
set_tests_properties(manga_tests_integration_recovery PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "integration"
    TIMEOUT 60
)

add_subdirectory(ui)
