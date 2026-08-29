#pragma once

#include "application/ports/IDerivedAssetStore.h"
#include "data/derived/FileSystemDerivedAssetStore.h"
#include "data/project/ProjectIoCoordinator.h"
#include "domain/model/Uuid.h"

#include <QImage>
#include <QObject>
#include <QTemporaryDir>

#include <memory>
#include <optional>

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
    void readRejectsPathOutsideRequestedPage();
    void commitRejectsNullOrWrongFormatLayer();
    void commitRejectsUnboundProjectSession();
    void stopRequestedCommitReturnsCancelled();
    void corruptedExistingFileFailsAndIsNeverOverwritten();

private:
    [[nodiscard]] std::optional<manga::application::CommitDerivedLayerResponse> commit(const QImage& layer);

    QTemporaryDir m_tempDir;
    std::unique_ptr<manga::data::ProjectIoCoordinator> m_coordinator;
    std::unique_ptr<manga::data::FileSystemDerivedAssetStore> m_store;
    manga::domain::ProjectId m_projectId;
    manga::domain::PageId m_pageId;
};
