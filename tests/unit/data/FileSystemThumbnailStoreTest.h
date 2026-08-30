#pragma once

#include "data/derived/FileSystemThumbnailStore.h"
#include "data/project/ProjectIoCoordinator.h"

#include <QObject>
#include <QTemporaryDir>

#include <memory>

class FileSystemThumbnailStoreTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void roundTripUsesFixedAtomicPngPath();
    void missingThumbnailReturnsStableError();
    void unboundProjectIsRejected();
    void cancelledReplacementPreservesExistingThumbnail();

private:
    QTemporaryDir m_tempDir;
    std::unique_ptr<manga::data::ProjectIoCoordinator> m_coordinator;
    std::unique_ptr<manga::data::FileSystemThumbnailStore> m_store;
};
