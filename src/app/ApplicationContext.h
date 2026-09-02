#pragma once

#include "application/ports/IAppSettingsStore.h"
#include "application/ports/IComicExporter.h"
#include "application/ports/IComicImporter.h"
#include "application/ports/ICredentialStore.h"
#include "application/ports/IDerivedAssetStore.h"
#include "application/ports/IDetectionModelValidator.h"
#include "application/ports/IInpaintModelValidator.h"
#include "application/ports/IInpainter.h"
#include "application/ports/IOcrEngine.h"
#include "application/ports/IOcrModelValidator.h"
#include "application/ports/IPageImageLoader.h"
#include "application/ports/IPageInpainter.h"
#include "application/ports/IProjectRepository.h"
#include "application/ports/IRecentProjectsStore.h"
#include "application/ports/ITextDetector.h"
#include "application/ports/ITextLayouter.h"
#include "application/ports/IThumbnailStore.h"
#include "application/ports/ITranslator.h"
#include "application/usecase/credential/CredentialUseCases.h"
#include "application/workflow/OperationRegistry.h"

#include <QString>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace manga::data {
class ProjectIoCoordinator;
}

namespace manga::network {
struct ProviderConfig;
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
                       std::unique_ptr<application::IOcrModelValidator> ocrModelValidator,
                       std::unique_ptr<application::IComicExporter> comicExporter = nullptr,
                       std::unique_ptr<application::IInpaintModelValidator> inpaintModelValidator = nullptr,
                       std::unique_ptr<application::IPageInpainter> pageInpainter = nullptr);
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
    [[nodiscard]] application::IComicExporter* comicExporter() const {
        return m_comicExporter.get();
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
    [[nodiscard]] application::ITextDetector* textDetector() const {
        return m_textDetector.get();
    }
    [[nodiscard]] application::IDetectionModelValidator* detectionModelValidator() const {
        return m_detectionModelValidator.get();
    }
    [[nodiscard]] bool isDetectionModelAvailable() const noexcept {
        return m_textDetector != nullptr && !m_detectionModelError.has_value();
    }
    [[nodiscard]] QString detectionModelUnavailableReason() const {
        return m_detectionModelError ? m_detectionModelError->safeMessage : QString();
    }
    /// Revalidates and replaces the production text detector for a changed model directory.
    /// Previous adapters remain alive until context shutdown because active workflows are non-owning.
    [[nodiscard]] bool reconfigureDetectionModel(const QString& modelDirectory);
    [[nodiscard]] application::IPageInpainter* pageInpainter() const {
        return m_pageInpainter.get();
    }
    [[nodiscard]] application::IInpaintModelValidator* inpaintModelValidator() const {
        return m_inpaintModelValidator.get();
    }
    [[nodiscard]] bool isInpaintModelAvailable() const noexcept {
        return m_pageInpainter != nullptr && !m_inpaintModelError.has_value();
    }
    [[nodiscard]] QString inpaintModelUnavailableReason() const {
        return m_inpaintModelError ? m_inpaintModelError->safeMessage : QString();
    }
    /// Revalidates and replaces the production page inpainter for a changed model directory.
    /// Previous adapters remain alive until context shutdown because active workflows are non-owning.
    [[nodiscard]] bool reconfigureInpaintModel(const QString& modelDirectory);
    [[nodiscard]] bool isOcrModelAvailable() const noexcept {
        return !m_ocrModelError.has_value();
    }
    [[nodiscard]] QString ocrModelUnavailableReason() const {
        return m_ocrModelError ? m_ocrModelError->safeMessage : QString();
    }

    void updateProviderConfig(std::shared_ptr<const network::ProviderConfig> config);
    [[nodiscard]] std::shared_ptr<const network::ProviderConfig> providerConfig() const;

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
    std::unique_ptr<application::IComicExporter> m_comicExporter;
    std::unique_ptr<application::IOcrEngine> m_ocrEngine;
    std::unique_ptr<application::ITranslator> m_translator;
    std::unique_ptr<application::IInpainter> m_inpainter;
    std::unique_ptr<application::ITextLayouter> m_textLayouter;
    std::unique_ptr<application::IDerivedAssetStore> m_derivedAssetStore;
    std::unique_ptr<application::CredentialOperations> m_credentialOperations;
    std::unique_ptr<application::ICredentialStore> m_credentialStore;
    std::unique_ptr<application::IOcrModelValidator> m_ocrModelValidator;
    std::optional<application::ApplicationError> m_ocrModelError;
    std::unique_ptr<application::IDetectionModelValidator> m_detectionModelValidator;
    std::unique_ptr<application::ITextDetector> m_textDetector;
    std::vector<std::unique_ptr<application::ITextDetector>> m_retiredTextDetectors;
    QString m_detectionModelDirectory;
    std::optional<application::ApplicationError> m_detectionModelError;
    std::unique_ptr<application::IInpaintModelValidator> m_inpaintModelValidator;
    std::unique_ptr<application::IPageInpainter> m_pageInpainter;
    std::vector<std::unique_ptr<application::IPageInpainter>> m_retiredPageInpainters;
    QString m_inpaintModelDirectory;
    std::optional<application::ApplicationError> m_inpaintModelError;
    std::atomic<std::shared_ptr<const network::ProviderConfig>> m_providerConfig;
    bool m_shutdown{false};
};

} // namespace manga::app
