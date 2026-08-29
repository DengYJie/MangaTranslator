#pragma once

#include <QObject>
#include <QTemporaryDir>

#include <memory>
#include <optional>

class ProjectIoCoordinator;
class FileSystemDerivedAssetStore;

namespace manga::application {
struct CommitDerivedLayerResponse;
}

class FileSystemDerivedAssetStoreTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void commitCreatesContentAddressedPng();
    void identicalCommitReusesExistingFile();
    void differentContentCommitsSecondFileWithoutTouchingFirst();
    void readRoundTripReturnsCommittedPixels();
    void readMissingLayerFails();
    void commitRejectsNullOrWrongFormatLayer();
    void stopRequestedCommitReturnsCancelled();

private:
    [[nodiscard]] std::optional<manga::application::CommitDerivedLayerResponse> commit(const QImage& layer);

    QTemporaryDir m_tempDir;
    std::unique_ptr<manga::data::ProjectIoCoordinator> m_coordinator;
    std::unique_ptr<manga::data::FileSystemDerivedAssetStore> m_store;
    QString m_projectRoot;
    manga::domain::PageId m_pageId;
};
