manga_add_test(
    NAME manga_tests_network
    SOURCES
        NetworkTestMain.cpp
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
    LIBRARIES
        manga_network
        manga_application
        Qt6::Core
        Qt6::Network
    LABELS "unit;network"
    TIMEOUT 60
)
