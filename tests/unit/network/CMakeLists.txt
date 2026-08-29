qt_add_executable(manga_tests_network
    network_test_main.cpp
    ProviderConfigTest.cpp
    ProviderConfigTest.h
    ChatCompletionsProtocolTest.cpp
    ChatCompletionsProtocolTest.h
    RetryPolicyTest.cpp
    RetryPolicyTest.h
    OpenAiCompatibleTranslatorTest.cpp
    OpenAiCompatibleTranslatorTest.h
    WindowsCredentialStoreTest.cpp
    WindowsCredentialStoreTest.h
)
manga_apply_project_options(manga_tests_network)
set_target_properties(manga_tests_network PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)

target_link_libraries(manga_tests_network
    PRIVATE
        manga_network
        manga_application
        Qt6::Core
        Qt6::Network
        Qt6::Test
)
manga_deploy_qt_runtime(manga_tests_network)

add_test(NAME manga_tests_network COMMAND manga_tests_network)
set_tests_properties(manga_tests_network PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;network"
    TIMEOUT 60
)
