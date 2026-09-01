#include "app/ApplicationContext.h"

#include "application/ports/IComicImporter.h"
#include "application/ports/ICredentialStore.h"
#include "application/ports/IDerivedAssetStore.h"
#include "application/ports/IInpainter.h"
#include "application/ports/IOcrEngine.h"
#include "application/ports/IOcrModelValidator.h"
#include "application/ports/IPageImageLoader.h"
#include "application/ports/ITextLayouter.h"
#include "application/ports/IThumbnailStore.h"
#include "application/ports/ITranslator.h"
#include "application/usecase/credential/CredentialUseCases.h"
#include "application/usecase/settings/AppSettingsUseCases.h"
#include "application/workflow/OperationRegistry.h"
#include "data/derived/FileSystemDerivedAssetStore.h"
#include "data/derived/FileSystemThumbnailStore.h"
#include "data/import/FileSystemComicImporter.h"
#include "data/import/FileSystemPageImageLoader.h"
#include "data/project/FileSystemProjectRepository.h"
#include "data/project/ProjectIoCoordinator.h"
#include "data/settings/QSettingsAppSettingsStore.h"
#include "data/settings/QSettingsRecentProjectsStore.h"
#include "network/credentials/WindowsCredentialStore.h"
#include "network/translation/OpenAiCompatibleTranslator.h"
#include "network/translation/ProviderConfig.h"
#include "services/execution/BoundedTaskExecutor.h"
#include "services/inpaint/SolidColorInpainter.h"
#include "services/layout/QtTextLayouter.h"
#include "services/ocr/OcrFactory.h"
#include "services/ocr/OcrModelValidator.h"

#include <QThreadPool>

namespace manga::app {

struct ApplicationContext::ProductionAdapters {
    std::unique_ptr<data::ProjectIoCoordinator> ioCoordinator;
    std::unique_ptr<services::BoundedTaskExecutor> executor;
};

ApplicationContext::ApplicationContext()
    : m_production(std::make_unique<ProductionAdapters>()),
      m_operationRegistry(std::make_unique<application::OperationRegistry>()) {
    m_production->ioCoordinator = std::make_unique<data::ProjectIoCoordinator>();
    m_projectRepository = std::make_unique<data::FileSystemProjectRepository>(m_production->ioCoordinator.get());
    m_recentProjectsStore = std::make_unique<data::QSettingsRecentProjectsStore>();
    m_appSettingsStore = std::make_unique<data::QSettingsAppSettingsStore>();
    m_thumbnailStore = std::make_unique<data::FileSystemThumbnailStore>(m_production->ioCoordinator.get());
    m_pageImageLoader = std::make_unique<data::FileSystemPageImageLoader>(m_production->ioCoordinator.get());
    m_derivedAssetStore = std::make_unique<data::FileSystemDerivedAssetStore>(m_production->ioCoordinator.get());
    m_comicImporter = std::make_unique<data::FileSystemComicImporter>(m_production->ioCoordinator.get());
    // Credential material stays in Windows Credential Manager; QSettings only holds non-secret settings.
    m_credentialStore = std::make_unique<network::WindowsCredentialStore>();
    m_credentialOperations = std::make_unique<application::CredentialOperations>(m_credentialStore.get());
    m_ocrModelValidator = std::make_unique<services::OcrModelValidator>();
    m_production->executor = std::make_unique<services::BoundedTaskExecutor>();
    const auto settings = application::LoadAppSettingsUseCase(m_appSettingsStore.get()).execute();
    m_inpainter = std::make_unique<services::SolidColorInpainter>(m_production->executor.get());
    m_textLayouter = std::make_unique<services::QtTextLayouter>(m_production->executor.get());
    m_ocrEngine = services::createOcrEngine(m_production->executor.get(), settings.ocrModelDirectory);
    network::ProviderConfig provider;
    provider.providerId = settings.providerId;
    provider.endpoint = QUrl(settings.endpoint);
    provider.model = settings.modelName;
    provider.timeoutMs = settings.timeoutSeconds * 1000;
    m_translator = std::make_unique<network::OpenAiCompatibleTranslator>(
        provider, m_credentialStore.get(), nullptr, network::RetryPolicy(nullptr, nullptr, settings.retryCount));
}

ApplicationContext::ApplicationContext(std::unique_ptr<application::IProjectRepository> projectRepository,
                                       std::unique_ptr<application::IRecentProjectsStore> recentProjectsStore,
                                       std::unique_ptr<application::IAppSettingsStore> appSettingsStore)
    : ApplicationContext(std::move(projectRepository),
                         std::move(recentProjectsStore),
                         std::move(appSettingsStore),
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr) {}

ApplicationContext::ApplicationContext(std::unique_ptr<application::IProjectRepository> projectRepository,
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
                                       std::unique_ptr<application::IOcrModelValidator> ocrModelValidator)
    : m_projectRepository(std::move(projectRepository)), m_recentProjectsStore(std::move(recentProjectsStore)),
      m_appSettingsStore(std::move(appSettingsStore)),
      m_operationRegistry(std::make_unique<application::OperationRegistry>()),
      m_thumbnailStore(std::move(thumbnailStore)), m_pageImageLoader(std::move(pageImageLoader)),
      m_comicImporter(std::move(comicImporter)), m_ocrEngine(std::move(ocrEngine)), m_translator(std::move(translator)),
      m_inpainter(std::move(inpainter)), m_textLayouter(std::move(textLayouter)),
      m_derivedAssetStore(std::move(derivedAssetStore)), m_credentialStore(std::move(credentialStore)),
      m_ocrModelValidator(std::move(ocrModelValidator)) {}

ApplicationContext::~ApplicationContext() {
    shutdown();
}

void ApplicationContext::shutdown() {
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;

    if (m_operationRegistry) {
        m_operationRegistry->cancelAll();
    }

    QThreadPool::globalInstance()->waitForDone(5000);

    // Future executors and active I/O threads should be stopped here before destroying the pointers.
    // Destruction order below is the teardown order.
    // m_production (holding ProjectIoCoordinator) is declared first and destroyed last,
    // ensuring adapters referencing it are destroyed beforehand.
}

} // namespace manga::app
