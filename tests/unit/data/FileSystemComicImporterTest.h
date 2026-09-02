#pragma once

#include "application/dto/ImportDto.h"
#include "application/dto/ProjectDto.h"

#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>
#include <optional>

namespace manga::data {
class ProjectIoCoordinator;
class FileSystemProjectRepository;
class FileSystemComicImporter;
class FileSystemPageImageLoader;
struct RepositorySession;
} // namespace manga::data

class FileSystemComicImporterTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void webpPluginIsAvailable();
    void importSingleImage();
    void importFolderNaturalOrder();
    void duplicateViaFolderAndDirectRejected();
    void reimportCreatesNewAsset();
    void disguisedExtensionWarns();
    void nonImageContentFails();
    void exifOrientationApplied();
    void specialFormatsDecode();
    void transparentPngKeepsAlpha();
    void oversizeImagesRejected();
    void partialFailureKeepsOrder();
    void cancelledImportWritesNothing();
    void cancelledMidImportRollsBackCommittedAssets();
    void concurrentImportsSerialize();
    void discardRemovesAssets();
    void acknowledgedBatchCannotDeleteAdoptedAsset();
    void projectSessionBindingRejectsMismatchedAndClosedRequests();
    void loaderRoundTripMatchesImport();
    void loaderRejectsMissingAndEscape();
    void loaderRejectsDimensionMismatch();
    void importErrorsDoNotLeakPathsOrUserContent();
    void sourceFileUnchangedAfterImportAndSave();

private:
    [[nodiscard]] QString fixture(const QString& name) const;
    [[nodiscard]] bool materializeFixtures();
    [[nodiscard]] bool createTestProject();
    [[nodiscard]] std::optional<manga::application::ImportImagesResult> import(const QVector<QString>& paths);
    [[nodiscard]] manga::application::ApplicationError importError(const QVector<QString>& paths);
    [[nodiscard]] std::optional<QImage> loadPageImage(const QString& assetPath, manga::domain::ImageSize expectedSize);

    QTemporaryDir m_tempDir;
    std::unique_ptr<manga::data::ProjectIoCoordinator> m_coordinator;
    std::unique_ptr<manga::data::FileSystemProjectRepository> m_repository;
    std::unique_ptr<manga::data::FileSystemComicImporter> m_importer;
    std::unique_ptr<manga::data::FileSystemPageImageLoader> m_loader;
    manga::application::ProjectDescriptor m_projectDescriptor;
    QString m_projectRoot;
    int m_requestCounter{0};
};
