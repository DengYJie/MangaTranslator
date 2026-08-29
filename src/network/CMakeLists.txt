add_library(manga_network STATIC
    network.cpp
    credentials/WindowsCredentialStore.cpp
    translation/ChatCompletionsProtocol.cpp
    translation/OpenAiCompatibleTranslator.cpp
    translation/QtHttpTransport.cpp
    translation/RetryPolicy.cpp
)
manga_apply_project_options(manga_network)

target_include_directories(manga_network
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_network
    PUBLIC
        manga_application
        Qt6::Core
        Qt6::Network
    PRIVATE
        Advapi32
)
