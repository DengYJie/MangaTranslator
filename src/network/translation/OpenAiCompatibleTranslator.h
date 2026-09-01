#pragma once

#include "application/ports/ICredentialStore.h"
#include "application/ports/ITranslator.h"
#include "network/translation/IHttpTransport.h"
#include "network/translation/ProviderConfig.h"
#include "network/translation/RetryPolicy.h"

#include <atomic>
#include <memory>

namespace manga::network {

/// OpenAI-compatible Chat Completions adapter. One instance is bound to one provider
/// configuration; the API key is read at the start of every request, so changing key, model or
/// endpoint never needs a restart. Results are suggestions: nothing here writes the Domain.
class OpenAiCompatibleTranslator final : public application::ITranslator {
public:
    OpenAiCompatibleTranslator(std::shared_ptr<const ProviderConfig> config,
                               application::ICredentialStore* store,
                               std::unique_ptr<IHttpTransport> transport = nullptr,
                               RetryPolicy retryPolicy = RetryPolicy(nullptr, nullptr),
                               EndpointPolicy endpointPolicy = EndpointPolicy::HttpsOnly);
    OpenAiCompatibleTranslator(ProviderConfig config,
                               application::ICredentialStore* store,
                               std::unique_ptr<IHttpTransport> transport = nullptr,
                               RetryPolicy retryPolicy = RetryPolicy(nullptr, nullptr),
                               EndpointPolicy endpointPolicy = EndpointPolicy::HttpsOnly);
    ~OpenAiCompatibleTranslator() override;

    OpenAiCompatibleTranslator(const OpenAiCompatibleTranslator&) = delete;
    OpenAiCompatibleTranslator& operator=(const OpenAiCompatibleTranslator&) = delete;

    void updateConfig(std::shared_ptr<const ProviderConfig> config);
    [[nodiscard]] std::shared_ptr<const ProviderConfig> config() const;

    [[nodiscard]] QFuture<application::Result<application::TranslationResponse>>
    translate(application::TranslationRequest request, application::OperationContext context) override;

private:
    struct Job;

    std::atomic<std::shared_ptr<const ProviderConfig>> m_config;
    application::ICredentialStore* m_store;
    std::shared_ptr<IHttpTransport> m_transport;
    RetryPolicy m_retryPolicy;
    EndpointPolicy m_endpointPolicy;
};

} // namespace manga::network
