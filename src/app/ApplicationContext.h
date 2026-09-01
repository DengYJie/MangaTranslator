#pragma once

#include <memory>

namespace manga::application {
class IAppSettingsStore;
class IProjectRepository;
class IRecentProjectsStore;
class OperationRegistry;
class IComicImporter;
class IPageImageLoader;
class IThumbnailStore;
class IOcrEngine;
class IOcrModelValidator;
class ITranslator;
class IInpainter;
class ITextLayouter;
class IDerivedAssetStore;
class ICredentialStore;
class CredentialOperations;
} // namespace manga::application

namespace manga::data {
class ProjectIoCoordinator;
}

namespace manga::app {

/// Composition root: the only place that constructs the production object graph.
/// Ports are exposed as interface pointers; leaf classes never query this context.
class ApplicationContext {
public:
    /// Production wiring: filesystem repository, QSettings-backed stores.
    ApplicationContext();
    /// Test wiring: inject fake ports; no production adapters are constructed.
    ApplicationContext(std::unique_ptr<application::IProjectRepository> projectRepository,
                       std::unique_ptr<application::IRecentProjectsStore> recentProjectsStore,
                       std::unique_ptr<application::IAppSettingsStore> appSettingsStore);
    ApplicationContext(std::unique_ptr<application::IProjectRepository> projectRepository,
                       std::unique_ptr<application::IRecentProjectsStore> recentProjectsStore,
                       std::unique_ptr<application::IAppSettingsStore> appSettingsStore,
                       std::unique_ptr<application::IThumbnailStore> thumbnailStore,
                       std::unique_ptr<application::IPageImageLoader> pageImageLoader,
                       std::unique_ptr<application::IComicImporter> comicImporter,
                       std::unique_ptr<application::IOcrEngine> ocrEngine,
                       std::unique_ptr<application::ITranslator> translator,
                       std::unique_ptr<application::IInpainter> inpainter,
                       std::unique_ptr<application::ITextLayouter> textLayouter,
                       std::unique_ptr<application::IDerivedAssetStore> derivedAssetStore,
                       std::unique_ptr<application::ICredentialStore> credentialStore,
                       std::unique_ptr<application::IOcrModelValidator> ocrModelValidator);
    ~ApplicationContext();

    ApplicationContext(const ApplicationContext&) = delete;
    ApplicationContext& operator=(const ApplicationContext&) = delete;

    [[nodiscard]] application::IProjectRepository* projectRepository() const {
        return m_projectRepository.get();
    }
    [[nodiscard]] application::IRecentProjectsStore* recentProjectsStore() const {
        return m_recentProjectsStore.get();
    }
    [[nodiscard]] application::IAppSettingsStore* appSettingsStore() const {
        return m_appSettingsStore.get();
    }
    [[nodiscard]] application::OperationRegistry* operationRegistry() const {
        return m_operationRegistry.get();
    }
    [[nodiscard]] application::IThumbnailStore* thumbnailStore() const {
        return m_thumbnailStore.get();
    }
    [[nodiscard]] application::IPageImageLoader* pageImageLoader() const {
        return m_pageImageLoader.get();
    }
    [[nodiscard]] application::IComicImporter* comicImporter() const {
        return m_comicImporter.get();
    }

    [[nodiscard]] application::IOcrEngine* ocrEngine() const {
        return m_ocrEngine.get();
    }
    [[nodiscard]] application::ITranslator* translator() const {
        return m_translator.get();
    }
    [[nodiscard]] application::IInpainter* inpainter() const {
        return m_inpainter.get();
    }
    [[nodiscard]] application::ITextLayouter* textLayouter() const {
        return m_textLayouter.get();
    }
    [[nodiscard]] application::IDerivedAssetStore* derivedAssetStore() const {
        return m_derivedAssetStore.get();
    }
    [[nodiscard]] application::ICredentialStore* credentialStore() const {
        return m_credentialStore.get();
    }
    [[nodiscard]] application::CredentialOperations* credentialOperations() const {
        return m_credentialOperations.get();
    }
    [[nodiscard]] application::IOcrModelValidator* ocrModelValidator() const {
        return m_ocrModelValidator.get();
    }

    /// Deterministic reverse-order teardown entry point. Idempotent.
    void shutdown();

private:
    // Production-only members stay behind the pimpl so the injecting constructor never
    // constructs production adapters.
    struct ProductionAdapters;
    std::unique_ptr<ProductionAdapters> m_production;
    std::unique_ptr<application::IProjectRepository> m_projectRepository;
    std::unique_ptr<application::IRecentProjectsStore> m_recentProjectsStore;
    std::unique_ptr<application::IAppSettingsStore> m_appSettingsStore;
    std::unique_ptr<application::OperationRegistry> m_operationRegistry;
    std::unique_ptr<application::IThumbnailStore> m_thumbnailStore;
    std::unique_ptr<application::IPageImageLoader> m_pageImageLoader;
    std::unique_ptr<application::IComicImporter> m_comicImporter;
    std::unique_ptr<application::IOcrEngine> m_ocrEngine;
    std::unique_ptr<application::ITranslator> m_translator;
    std::unique_ptr<application::IInpainter> m_inpainter;
    std::unique_ptr<application::ITextLayouter> m_textLayouter;
    std::unique_ptr<application::IDerivedAssetStore> m_derivedAssetStore;
    std::unique_ptr<application::CredentialOperations> m_credentialOperations;
    std::unique_ptr<application::ICredentialStore> m_credentialStore;
    std::unique_ptr<application::IOcrModelValidator> m_ocrModelValidator;
    bool m_shutdown{false};
};

} // namespace manga::app
