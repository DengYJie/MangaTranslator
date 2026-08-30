#include "ui/screen/import/ImportViewModel.h"

#include "application/mapping/ProjectMapper.h"
#include "application/ports/IComicImporter.h"
#include "application/ports/IProjectRepository.h"
#include "application/usecase/apply/ApplyUseCases.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "application/workflow/OperationRegistry.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace manga::ui::screen::import_ {

ImportViewModel::ImportViewModel(application::IProjectRepository* projectRepository,
                                 application::IComicImporter* comicImporter,
                                 application::OperationRegistry* operationRegistry,
                                 QObject* parent)
    : base::BaseViewModel<ImportViewModel, ImportState>(parent), m_projectRepository(projectRepository),
      m_comicImporter(comicImporter), m_operationRegistry(operationRegistry), m_project(std::string("Stub")) {
    connect(&m_createWatcher, &QFutureWatcherBase::finished, this, &ImportViewModel::handleCreateFinished);
    connect(&m_importWatcher, &QFutureWatcherBase::finished, this, &ImportViewModel::handleImportFinished);
    connect(&m_importWatcher, &QFutureWatcherBase::progressValueChanged, this, [this](int value) {
        handleImportProgress(value, m_importWatcher.progressMaximum());
    });
    connect(&m_importWatcher, &QFutureWatcherBase::progressRangeChanged, this, [this](int /*minimum*/, int maximum) {
        if (m_state.totalPageCount != maximum) {
            updateState([maximum](ImportState& state) { state.totalPageCount = maximum; });
        }
    });
    connect(&m_ackWatcher, &QFutureWatcherBase::finished, this, &ImportViewModel::handleAckFinished);
    connect(&m_saveWatcher, &QFutureWatcherBase::finished, this, &ImportViewModel::handleSaveFinished);
    connect(&m_closeWatcher, &QFutureWatcherBase::finished, this, []() {});
}

void ImportViewModel::addSourcePaths(const QVector<QString>& paths) {
    updateState([&paths](ImportState& state) {
        QSet<QString> existingPaths;
        for (const auto& s : state.sources) {
            existingPaths.insert(s.path.toLower());
        }

        for (const auto& p : paths) {
            QString cleanPath = QDir::cleanPath(p);
            QString lowerPath = cleanPath.toLower();
            if (existingPaths.contains(lowerPath)) {
                continue;
            }
            existingPaths.insert(lowerPath);

            ImportSourceEntry entry;
            entry.path = cleanPath;

            int lastSlash = cleanPath.lastIndexOf(QLatin1Char('/'));
            entry.displayName = lastSlash >= 0 ? cleanPath.mid(lastSlash + 1) : cleanPath;

            int lastDot = entry.displayName.lastIndexOf(QLatin1Char('.'));
            if (lastDot < 0 || lastDot == entry.displayName.length() - 1) {
                entry.isFolder = true;
                entry.supported = true;
            } else {
                QString ext = entry.displayName.mid(lastDot + 1).toLower();
                if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg") || ext == QLatin1String("png") ||
                    ext == QLatin1String("webp")) {
                    entry.isFolder = false;
                    entry.supported = true;
                } else {
                    entry.isFolder = false;
                    entry.supported = false;
                    entry.errorSummary = tr("不支持的格式");
                }
            }
            state.sources.append(entry);
        }
    });
}

void ImportViewModel::removeSource(const QString& path) {
    QString cleanPath = QDir::cleanPath(path);
    updateState([&cleanPath](ImportState& state) {
        state.sources.removeIf([&cleanPath](const ImportSourceEntry& e) { return e.path == cleanPath; });
    });
}

void ImportViewModel::setProjectName(const QString& name) {
    updateState([name](ImportState& state) {
        state.projectName = name;
        state.nameError.clear();
        state.formErrorCount = 0;
    });
}

void ImportViewModel::setSaveLocation(const QString& location) {
    updateState([location](ImportState& state) {
        state.saveLocation = location;
        state.locationError.clear();
        state.formErrorCount = 0;
    });
}

void ImportViewModel::setReadingDirection(domain::ReadingDirection direction) {
    updateState([direction](ImportState& state) { state.readingDirection = direction; });
}

void ImportViewModel::setSourceLanguageCode(const QString& code) {
    updateState([code](ImportState& state) {
        state.sourceLanguageCode = code;
        state.languageCaution = (state.sourceLanguageCode == state.targetLanguageCode);
    });
}

void ImportViewModel::setTargetLanguageCode(const QString& code) {
    updateState([code](ImportState& state) {
        state.targetLanguageCode = code;
        state.languageCaution = (state.sourceLanguageCode == state.targetLanguageCode);
    });
}

bool ImportViewModel::validateForm() {
    bool valid = true;
    updateState([&valid](ImportState& state) {
        state.nameError.clear();
        state.locationError.clear();
        state.formErrorCount = 0;
        state.notice = std::nullopt;

        QString trimmedName = state.projectName.trimmed();
        if (trimmedName.isEmpty()) {
            state.nameError = tr("项目名称不能为空");
            state.formErrorCount++;
            valid = false;
        } else if (trimmedName.contains(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")))) {
            state.nameError = tr("项目名称包含非法字符");
            state.formErrorCount++;
            valid = false;
        }

        if (state.saveLocation.trimmed().isEmpty()) {
            state.locationError = tr("保存位置不能为空");
            state.formErrorCount++;
            valid = false;
        }

        auto srcLang = domain::LanguageCode::fromString(state.sourceLanguageCode.toStdString());
        auto tgtLang = domain::LanguageCode::fromString(state.targetLanguageCode.toStdString());
        if (!srcLang || !tgtLang) {
            state.formErrorCount++;
            valid = false;
        }

        int unsupportedCount = 0;
        for (const auto& s : state.sources) {
            if (!s.supported) {
                unsupportedCount++;
            }
        }
        if (unsupportedCount > 0) {
            state.notice = ImportNotice{ImportNotice::Severity::Error, tr("有 %1 项需要修正").arg(unsupportedCount)};
            valid = false;
        }
    });

    return valid;
}

void ImportViewModel::submit() {
    if (m_stage != Stage::None) {
        return; // debounce
    }

    if (!validateForm()) {
        return;
    }

    m_stage = Stage::Creating;
    updateState([](ImportState& state) {
        state.importInProgress = true;
        state.importedPageCount = 0;
        state.totalPageCount = 0;
        state.notice.reset();
    });

    m_requestToken = beginRequest();
    m_currentContext = m_operationRegistry->createOperation(0, 0);
    m_currentOperation = m_currentContext.operationId;

    application::CreateProjectRequest req;
    req.name = m_state.projectName.trimmed();
    req.path = m_state.saveLocation.trimmed() + QLatin1Char('/') + req.name;
    req.sourceLanguage =
        domain::LanguageCode::fromString(m_state.sourceLanguageCode.toStdString()).value_or(domain::LanguageCode{});
    req.targetLanguage =
        domain::LanguageCode::fromString(m_state.targetLanguageCode.toStdString()).value_or(domain::LanguageCode{});
    req.readingDirection = m_state.readingDirection;

    application::CreateProjectUseCase useCase(m_projectRepository);
    m_createWatcher.setFuture(useCase.execute(req, m_currentContext));
}

void ImportViewModel::cancelImport() {
    if (m_stage == Stage::Importing) {
        m_operationRegistry->cancelOperation(m_currentOperation);
    }
}

void ImportViewModel::dismissNotice() {
    updateState([](ImportState& state) { state.notice.reset(); });
}

void ImportViewModel::handleCreateFinished() {
    if (!isRequestCurrent(m_requestToken)) {
        return;
    }

    auto result = m_createWatcher.result();
    if (!result.has_value()) {
        const auto& error = result.error();
        if (error.code == QLatin1String("persistence.target_not_empty")) {
            updateState([&error](ImportState& state) {
                state.locationError = error.safeMessage;
                state.importInProgress = false;
            });
            m_stage = Stage::None;
        } else {
            failToEditingState(error);
        }
        return;
    }

    const auto& desc = result.value();
    m_projectRoot = desc.path;

    // Construct local domain project
    m_project = domain::ComicProject(desc.name.toStdString(), desc.id);
    m_project.setSourceLanguage(
        domain::LanguageCode::fromString(m_state.sourceLanguageCode.toStdString()).value_or(domain::LanguageCode{}));
    m_project.setTargetLanguage(
        domain::LanguageCode::fromString(m_state.targetLanguageCode.toStdString()).value_or(domain::LanguageCode{}));
    m_project.setReadingDirection(m_state.readingDirection);

    m_stage = Stage::Importing;

    application::ImportImagesRequest importReq;
    importReq.projectId = desc.id;
    importReq.projectRoot = m_projectRoot;
    for (const auto& s : m_state.sources) {
        importReq.filePaths.append(s.path);
    }

    application::ImportImagesUseCase useCase(m_comicImporter);
    m_importWatcher.setFuture(useCase.execute(importReq, m_currentContext));
}

void ImportViewModel::handleImportFinished() {
    if (!isRequestCurrent(m_requestToken)) {
        return;
    }

    auto result = m_importWatcher.result();
    if (!result.has_value()) {
        const auto& error = result.error();
        if (error.category == application::ErrorCategory::Cancelled) {
            updateState([](ImportState& state) { state.importInProgress = false; });
            m_stage = Stage::None;
            releaseCreatedProject();
        } else {
            failToEditingState(error);
            releaseCreatedProject();
        }
        return;
    }

    m_importResult = result.value();

    if (m_importResult.importedPages.isEmpty()) {
        QString noticeMessage = tr("没有可导入的页面");
        if (!m_importResult.failures.isEmpty()) {
            noticeMessage = m_importResult.failures.first().error.safeMessage;
        }
        updateState([&noticeMessage](ImportState& state) {
            state.notice = ImportNotice{ImportNotice::Severity::Error, noticeMessage};
            state.importInProgress = false;
        });
        m_stage = Stage::None;
        releaseCreatedProject();
        return;
    }

    // Apply
    application::ApplyImportResultRequest applyReq;
    applyReq.projectId = m_project.id();
    applyReq.result = m_importResult;
    applyReq.sessionGeneration = 0; // The brief specifies "gen, gen", i.e. we use 0 since we just created it
    applyReq.currentSessionGeneration = 0;

    application::ApplyImportResultUseCase applyUseCase(&m_project);
    auto applyRes = applyUseCase.execute(applyReq);
    if (!applyRes.has_value()) {
        failToEditingState(applyRes.error());
        releaseCreatedProject();
        return;
    }

    // Acknowledge
    application::AcknowledgeImportedAssetsRequest ackReq;
    ackReq.projectId = m_project.id();
    ackReq.projectRoot = m_projectRoot;
    ackReq.batchId = m_importResult.batchId;

    application::AcknowledgeImportedAssetsUseCase ackUseCase(m_comicImporter);
    m_ackWatcher.setFuture(ackUseCase.execute(ackReq, m_currentContext));
}

void ImportViewModel::handleAckFinished() {
    if (!isRequestCurrent(m_requestToken)) {
        return;
    }

    auto result = m_ackWatcher.result();
    if (!result.has_value()) {
        failToEditingState(result.error());
        releaseCreatedProject();
        return;
    }

    m_stage = Stage::Finalizing;

    application::SaveProjectRequest saveReq;
    saveReq.project = m_project;

    application::SaveProjectUseCase saveUseCase(m_projectRepository);
    m_saveWatcher.setFuture(saveUseCase.execute(saveReq, m_currentContext));
}

void ImportViewModel::handleImportProgress(int value, int maximum) {
    if (!isRequestCurrent(m_requestToken)) {
        return;
    }
    updateState([value, maximum](ImportState& state) {
        state.importedPageCount = value;
        state.totalPageCount = maximum;
    });
}

void ImportViewModel::handleSaveFinished() {
    if (!isRequestCurrent(m_requestToken)) {
        return;
    }

    auto result = m_saveWatcher.result();
    if (!result.has_value()) {
        failToEditingState(result.error());
        releaseCreatedProject();
        return;
    }

    updateState([](ImportState& state) { state.importInProgress = false; });
    m_stage = Stage::None;

    auto session = application::mapping::ProjectMapper::toSessionDescriptor(m_project, m_projectRoot);

    // Add failures to warnings
    for (const auto& fail : m_importResult.failures) {
        application::ProjectWarning warn;
        warn.code = fail.error.code;
        warn.safeMessage = fail.error.safeMessage;
        warn.path = fail.path;
        session.warnings.append(warn);
    }

    Q_EMIT projectImported(session);
}

void ImportViewModel::failToEditingState(const application::ApplicationError& error) {
    updateState([&error](ImportState& state) {
        state.notice = ImportNotice{ImportNotice::Severity::Error, error.safeMessage};
        state.importInProgress = false;
    });
    m_stage = Stage::None;
}

void ImportViewModel::releaseCreatedProject() {
    application::CloseProjectRequest closeReq;
    closeReq.projectId = m_project.id();
    closeReq.projectRoot = m_projectRoot;
    application::CloseProjectUseCase closeUseCase(m_projectRepository);
    m_closeWatcher.setFuture(closeUseCase.execute(closeReq, m_operationRegistry->createOperation(0, 0)));
}

} // namespace manga::ui::screen::import_
