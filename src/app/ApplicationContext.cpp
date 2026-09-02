#include "app/ApplicationContext.h"

#include "application/ports/IComicExporter.h"
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
#include "data/export/FileSystemComicExporter.h"
#include "data/import/FileSystemComicImporter.h"
#include "data/import/FileSystemPageImageLoader.h"
#include "data/project/FileSystemProjectRepository.h"
#include "data/project/ProjectIoCoordinator.h"
#include "data/settings/QSettingsAppSettingsStore.h"
#include "data/settings/QSettingsRecentProjectsStore.h"
#include "network/credentials/WindowsCredentialStore.h"
#include "network/translation/OpenAiCompatibleTranslator.h"
#include "network/translation/ProviderConfig.h"
#include "network/translation/QtHttpTransport.h"
#include "services/detection/DetectionModelValidator.h"
#include "services/detection/KoharuLayoutManifest.h"
#include "services/detection/OnnxKoharuLayoutDetector.h"
#include "services/execution/BoundedTaskExecutor.h"
#include "services/inpaint/InpaintManifest.h"
#include "services/inpaint/InpaintModelValidator.h"
#include "services/inpaint/OnnxLamaPageInpainter.h"
#include "services/inpaint/SolidColorInpainter.h"
#include "services/layout/QtTextLayouter.h"
#include "services/ocr/OcrFactory.h"
#include "services/ocr/OcrModelValidator.h"

#include <QThreadPool>

namespace manga::app {

struct ApplicationContext::ProductionAdapters {
    std::unique_ptr<data::ProjectIoCoordinator> ioCoordinator;
    services::UniqueOrtEnv ocrEnv;
    std::unique_ptr<services::BoundedTaskExecutor> executor;
    network::QtHttpTransport* httpTransport{nullptr};
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
    m_comicExporter = std::make_unique<data::FileSystemComicExporter>();

    // Credential material stays in Windows Credential Manager; QSettings only holds non-secret settings.
    m_credentialStore = std::make_unique<network::WindowsCredentialStore>();
    m_credentialOperations = std::make_unique<application::CredentialOperations>(m_credentialStore.get());
    m_ocrModelValidator = std::make_unique<services::OcrModelValidator>();
    m_inpaintModelValidator = std::make_unique<services::InpaintModelValidator>();
    m_detectionModelValidator = std::make_unique<services::DetectionModelValidator>();
    m_production->ocrEnv = services::createOcrEnv();
    m_production->executor = std::make_unique<services::BoundedTaskExecutor>();

    const auto settings = application::LoadAppSettingsUseCase(m_appSettingsStore.get()).execute();
    const auto ocrModelStatus = m_ocrModelValidator->validate(settings.ocrModelDirectory);
    if (!ocrModelStatus.has_value()) {
        m_ocrModelError = ocrModelStatus.error();
    }

    const auto detectionManifest = services::loadAndValidateDetectionManifest(settings.detectionModelDirectory);
    m_detectionModelDirectory = settings.detectionModelDirectory.trimmed();
    if (!detectionManifest.has_value()) {
        m_detectionModelError = detectionManifest.error();
        if (m_detectionModelError->safeMessage.trimmed().isEmpty()) {
            m_detectionModelError->safeMessage =
                QStringLiteral("请在设置中选择包含 manifest.json 的版面检测模型目录。");
        }
    } else {
        m_textDetector = std::make_unique<services::OnnxKoharuLayoutDetector>(
            m_production->ocrEnv.get(), m_production->executor.get(), settings.detectionModelDirectory);
    }

    const auto inpaintManifest = services::loadAndValidateManifest(settings.inpaintModelDirectory);
    m_inpaintModelDirectory = settings.inpaintModelDirectory.trimmed();
    if (!inpaintManifest.has_value()) {
        m_inpaintModelError = inpaintManifest.error();
        if (m_inpaintModelError->safeMessage.trimmed().isEmpty()) {
            m_inpaintModelError->safeMessage = QStringLiteral("请在设置中选择包含 manifest.json 的 Inpaint 模型目录。");
        }
    } else {
        m_pageInpainter = std::make_unique<services::OnnxLamaPageInpainter>(
            m_production->ocrEnv.get(), m_production->executor.get(), inpaintManifest.value());
    }
    m_inpainter = std::make_unique<services::SolidColorInpainter>(m_production->executor.get());
    m_textLayouter = std::make_unique<services::QtTextLayouter>(m_production->executor.get());
    m_ocrEngine =
        services::createOcrEngine(m_production->ocrEnv.get(), m_production->executor.get(), settings.ocrModelDirectory);

    network::ProviderConfig provider;
    provider.providerId = settings.providerId;
    provider.endpoint = QUrl(settings.endpoint);
    provider.model = settings.modelName;
    provider.timeoutMs = settings.timeoutSeconds * 1000;
    auto providerSnapshot = std::make_shared<const network::ProviderConfig>(std::move(provider));
    m_providerConfig.store(providerSnapshot);
    auto transport = std::make_unique<network::QtHttpTransport>();
    m_production->httpTransport = transport.get();
    m_translator = std::make_unique<network::OpenAiCompatibleTranslator>(
        providerSnapshot,
        m_credentialStore.get(),
        std::move(transport),
        network::RetryPolicy(nullptr, nullptr, settings.retryCount));

    // Schedule derived-asset garbage collection after successful saves. Test constructor
    // paths never create a derived store, so the dynamic_cast simply skips there.
    if (auto* repository = dynamic_cast<data::FileSystemProjectRepository*>(m_projectRepository.get())) {
        if (m_derivedAssetStore != nullptr) {
            repository->setDerivedAssetStore(m_derivedAssetStore.get());
        }
    }
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
                                       std::unique_ptr<application::IOcrModelValidator> ocrModelValidator,
                                       std::unique_ptr<application::IComicExporter> comicExporter,
                                       std::unique_ptr<application::IInpaintModelValidator> inpaintModelValidator,
                                       std::unique_ptr<application::IPageInpainter> pageInpainter)
    : m_projectRepository(std::move(projectRepository)), m_recentProjectsStore(std::move(recentProjectsStore)),
      m_appSettingsStore(std::move(appSettingsStore)),
      m_operationRegistry(std::make_unique<application::OperationRegistry>()),
      m_thumbnailStore(std::move(thumbnailStore)), m_pageImageLoader(std::move(pageImageLoader)),
      m_comicImporter(std::move(comicImporter)), m_comicExporter(std::move(comicExporter)),
      m_ocrEngine(std::move(ocrEngine)), m_translator(std::move(translator)), m_inpainter(std::move(inpainter)),
      m_textLayouter(std::move(textLayouter)), m_derivedAssetStore(std::move(derivedAssetStore)),
      m_credentialStore(std::move(credentialStore)), m_ocrModelValidator(std::move(ocrModelValidator)),
      m_inpaintModelValidator(std::move(inpaintModelValidator)), m_pageInpainter(std::move(pageInpainter)) {}

ApplicationContext::~ApplicationContext() {
    shutdown();
}

void ApplicationContext::updateProviderConfig(std::shared_ptr<const network::ProviderConfig> config) {
    m_providerConfig.store(config);
    if (auto* openAi = dynamic_cast<network::OpenAiCompatibleTranslator*>(m_translator.get())) {
        openAi->updateConfig(std::move(config));
    }
}

std::shared_ptr<const network::ProviderConfig> ApplicationContext::providerConfig() const {
    return m_providerConfig.load();
}

bool ApplicationContext::reconfigureInpaintModel(const QString& modelDirectory) {
    const QString normalizedDirectory = modelDirectory.trimmed();
    if (normalizedDirectory == m_inpaintModelDirectory) {
        return isInpaintModelAvailable();
    }
    m_inpaintModelDirectory = normalizedDirectory;

    const auto retireCurrent = [this]() {
        if (m_pageInpainter != nullptr) {
            m_retiredPageInpainters.push_back(std::move(m_pageInpainter));
        }
    };

    if (m_inpaintModelValidator == nullptr || m_production == nullptr || m_production->executor == nullptr ||
        m_production->ocrEnv == nullptr) {
        retireCurrent();
        m_inpaintModelError =
            application::makeApplicationError(application::ErrorCategory::Internal,
                                              QStringLiteral("inpaint.reconfigure_unavailable"),
                                              QStringLiteral("Inpaint 模型运行环境不可用，请重启应用后重试。"));
        return false;
    }

    const auto manifest = services::loadAndValidateManifest(normalizedDirectory);
    if (!manifest.has_value()) {
        retireCurrent();
        m_inpaintModelError = manifest.error();
        if (m_inpaintModelError->safeMessage.trimmed().isEmpty()) {
            m_inpaintModelError->safeMessage = QStringLiteral("请在设置中选择包含 manifest.json 的 Inpaint 模型目录。");
        }
        return false;
    }

    auto replacement = std::make_unique<services::OnnxLamaPageInpainter>(
        m_production->ocrEnv.get(), m_production->executor.get(), manifest.value());
    retireCurrent();
    m_pageInpainter = std::move(replacement);
    m_inpaintModelError.reset();
    return true;
}

bool ApplicationContext::reconfigureDetectionModel(const QString& modelDirectory) {
    const QString normalizedDirectory = modelDirectory.trimmed();
    if (normalizedDirectory == m_detectionModelDirectory) {
        return isDetectionModelAvailable();
    }
    m_detectionModelDirectory = normalizedDirectory;

    if (m_detectionModelValidator == nullptr || m_production == nullptr || m_production->executor == nullptr ||
        m_production->ocrEnv == nullptr) {
        if (m_textDetector != nullptr) {
            m_retiredTextDetectors.push_back(std::move(m_textDetector));
        }
        m_detectionModelError =
            application::makeApplicationError(application::ErrorCategory::Internal,
                                              QStringLiteral("detection.reconfigure_unavailable"),
                                              QStringLiteral("版面检测模型运行环境不可用，请重启应用后重试。"));
        return false;
    }

    const auto manifest = services::loadAndValidateDetectionManifest(normalizedDirectory);
    if (!manifest.has_value()) {
        if (m_textDetector != nullptr) {
            m_retiredTextDetectors.push_back(std::move(m_textDetector));
        }
        m_detectionModelError = manifest.error();
        if (m_detectionModelError->safeMessage.trimmed().isEmpty()) {
            m_detectionModelError->safeMessage =
                QStringLiteral("请在设置中选择包含 manifest.json 的版面检测模型目录。");
        }
        return false;
    }

    auto replacement = std::make_unique<services::OnnxKoharuLayoutDetector>(
        m_production->ocrEnv.get(), m_production->executor.get(), normalizedDirectory);
    if (m_textDetector != nullptr) {
        m_retiredTextDetectors.push_back(std::move(m_textDetector));
    }
    m_textDetector = std::move(replacement);
    m_detectionModelError.reset();
    return true;
}

void ApplicationContext::shutdown() {
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;

    // Session teardown (gate, Dirty decision, watcher/UndoStack/lease) has already run in the
    // Application layer before this point. Process-tier teardown:
    // 1. Stop intake and request cancellation of every remaining operation.
    if (m_operationRegistry) {
        m_operationRegistry->cancelAll();
    }

    // 2. Bounded executor shutdown: queued tasks complete as cancelled, running tasks stop
    //    cooperatively; a single uninterruptible ONNX inference may finish but its result is
    //    discarded by the stale generation check (P7 measures its worst-case exit latency).
    if (m_production && m_production->executor) {
        m_production->executor->shutdown();
    }

    // 3. Stop the network thread; replies are aborted and destroyed on their owning thread.
    if (m_production && m_production->httpTransport) {
        m_production->httpTransport->shutdown();
    }

    // 4. Drain fire-and-forget jobs scheduled on the global thread pool (e.g. the post-save
    //    derived asset GC, whose future is deliberately dropped). Without this wait a worker
    //    could still hold a pointer into stores that are destroyed right below; the wait is
    //    time-bounded because cooperative cancellation above should already have drained the
    //    tracked data operations.
    QThreadPool::globalInstance()->waitForDone(5000);

    // 5. Member destruction after this point is the reverse of declaration order:
    //    adapters/stores first, ProjectIoCoordinator (declared first) last.
}

} // namespace manga::app
