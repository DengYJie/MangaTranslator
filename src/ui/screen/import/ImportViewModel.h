#pragma once

#include "application/dto/ImportDto.h"
#include "application/dto/ProjectDto.h"
#include "application/operation/AsyncResult.h"
#include "application/operation/OperationContext.h"
#include "domain/model/ComicProject.h"
#include "ui/base/BaseViewModel.h"
#include "ui/screen/import/ImportState.h"

#include <QFutureWatcher>

namespace manga::application {
class IComicImporter;
class IProjectRepository;
class OperationRegistry;
} // namespace manga::application

namespace manga::ui::screen::import_ {

/// Drives the Import page: source list, project form validation and the staged
/// create -> import -> apply -> acknowledge -> save orchestration.
class ImportViewModel : public base::BaseViewModel<ImportViewModel, ImportState> {
    Q_OBJECT
public:
    ImportViewModel(application::IProjectRepository* projectRepository,
                    application::IComicImporter* comicImporter,
                    application::OperationRegistry* operationRegistry,
                    QObject* parent = nullptr);

    void addSourcePaths(const QVector<QString>& paths);
    void removeSource(const QString& path);
    void setProjectName(const QString& name);
    void setSaveLocation(const QString& location);
    void setReadingDirection(domain::ReadingDirection direction);
    void setSourceLanguageCode(const QString& code);
    void setTargetLanguageCode(const QString& code);

    void requestBrowseFiles() {
        Q_EMIT browseFilesRequested();
    }
    void requestBrowseFolder() {
        Q_EMIT browseFolderRequested();
    }
    void requestBrowseLocation() {
        Q_EMIT browseLocationRequested();
    }
    void requestCancel() {
        Q_EMIT cancelRequested();
    }

    void submit();
    void cancelImport();
    void dismissNotice();

Q_SIGNALS:
    void stateChanged(const ImportState& state);
    void browseFilesRequested();
    void browseFolderRequested();
    void browseLocationRequested();
    void cancelRequested();
    void projectImported(const application::ProjectSessionDescriptor& session);

protected:
    void emitStateChanged() override {
        Q_EMIT stateChanged(m_state);
    }

private:
    enum class Stage {
        None,
        Creating,
        Importing,
        Finalizing
    };

    bool validateForm(); // writes nameError/locationError/formErrorCount/languageCaution
    void handleCreateFinished();
    void handleImportFinished();
    void handleImportProgress(int value, int maximum);
    void handleSaveFinished();
    void failToEditingState(const application::ApplicationError& error);
    void releaseCreatedProject(); // CloseProjectUseCase after failed/cancelled import

    application::IProjectRepository* m_projectRepository; // non-owning
    application::IComicImporter* m_comicImporter;         // non-owning
    application::OperationRegistry* m_operationRegistry;  // non-owning

    QFutureWatcher<application::Result<application::ProjectDescriptor>> m_createWatcher;
    QFutureWatcher<application::Result<application::ImportImagesResult>> m_importWatcher;
    QFutureWatcher<application::Result<application::SaveProjectResult>> m_saveWatcher;
    QFutureWatcher<application::Result<void>> m_closeWatcher;

    Stage m_stage{Stage::None};
    quint64 m_requestToken{0};
    application::OperationId m_currentOperation{};
    application::OperationContext m_currentContext;
    domain::ComicProject m_project;
    QString m_projectRoot;
    application::ImportImagesResult m_importResult;
};

} // namespace manga::ui::screen::import_
