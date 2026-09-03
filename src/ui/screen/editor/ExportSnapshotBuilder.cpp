#include "ui/screen/editor/ExportSnapshotBuilder.h"

#include "application/operation/AsyncResult.h"
#include "application/ports/IDerivedAssetStore.h"
#include "application/ports/IPageImageLoader.h"
#include "application/usecase/import/ImportUseCases.h"
#include "rendering/TextLayoutEngine.h"
#include "rendering/TextRasterizer.h"

#include <QColorSpace>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QPromise>
#include <QSet>

#include <functional>
#include <memory>

namespace manga::ui::screen::editor {

ExportSnapshotBuilder::ExportSnapshotBuilder(domain::ComicProject* project,
                                             application::IPageImageLoader* loader,
                                             application::IDerivedAssetStore* derivedStore)
    : m_project(project), m_loader(loader), m_derivedStore(derivedStore) {}

QFuture<application::Result<ExportSnapshotBuilder::PreviewResult>>
ExportSnapshotBuilder::executePreview(const Request& request, const application::OperationContext& context) {
    return execute(request, context)
        .then([](application::Result<QVector<application::ExportPageSnapshot>> result)
                  -> application::Result<ExportSnapshotBuilder::PreviewResult> {
            if (!result.has_value()) {
                return std::unexpected(result.error());
            }
            if (result.value().isEmpty()) {
                return std::unexpected(application::makeApplicationError(application::ErrorCategory::Input,
                                                                         QStringLiteral("input.invalid_page_selection"),
                                                                         QStringLiteral("Preview page was not found")));
            }

            const auto& page = result.value().first();

            // Build full composite (source + clear + translation + manualOverride). The canvas
            // receives the clear and override layers separately, so the eraser can subtract brush
            // paint without punching a hole through the inpainted result.
            QImage composite(page.sourceImage.size(), QImage::Format_RGBA8888_Premultiplied);
            composite.setColorSpace(QColorSpace(QColorSpace::SRgb));
            composite.fill(Qt::transparent);
            QPainter painter(&composite);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.drawImage(0, 0, page.sourceImage);
            if (!page.clearLayer.isNull()) {
                painter.drawImage(0, 0, page.clearLayer);
            }
            if (!page.translationLayer.isNull()) {
                painter.drawImage(0, 0, page.translationLayer);
            }
            if (!page.manualOverrideLayer.isNull()) {
                painter.drawImage(0, 0, page.manualOverrideLayer);
            }
            painter.end();

            PreviewResult preview;
            preview.compositeImage = composite.convertToFormat(QImage::Format_RGBA8888);
            preview.clearImage = page.clearLayer;
            preview.manualOverrideImage = page.manualOverrideLayer;
            preview.manualOverrideRef = page.manualOverrideLayerRef;
            return preview;
        });
}

QFuture<application::Result<QVector<application::ExportPageSnapshot>>>
ExportSnapshotBuilder::execute(const Request& request, const application::OperationContext& context) {
    if (m_project == nullptr || m_loader == nullptr || m_derivedStore == nullptr) {
        return application::makeFailedFuture<QVector<application::ExportPageSnapshot>>(
            application::makeApplicationError(application::ErrorCategory::Internal,
                                              QStringLiteral("internal.null_dependency"),
                                              QStringLiteral("Export dependency is unavailable")));
    }
    if (context.stopToken.stop_requested()) {
        return application::makeFailedFuture<QVector<application::ExportPageSnapshot>>(
            application::makeApplicationError(application::ErrorCategory::Cancelled,
                                              QStringLiteral("cancelled.before_start"),
                                              QStringLiteral("Export was cancelled")));
    }
    if (request.projectRoot.trimmed().isEmpty()) {
        return application::makeFailedFuture<QVector<application::ExportPageSnapshot>>(
            application::makeApplicationError(application::ErrorCategory::Input,
                                              QStringLiteral("input.invalid_project_root"),
                                              QStringLiteral("Project root is empty")));
    }

    QVector<domain::ComicPage> targetPages;
    if (request.pageIds.isEmpty()) {
        for (const auto& p : m_project->pages()) {
            targetPages.append(p);
        }
    } else {
        QSet<domain::PageId> requestedSet;
        for (const auto& pId : request.pageIds) {
            if (!pId.isValid() || m_project->findPage(pId) == nullptr || requestedSet.contains(pId)) {
                return application::makeFailedFuture<QVector<application::ExportPageSnapshot>>(
                    application::makeApplicationError(application::ErrorCategory::Input,
                                                      QStringLiteral("input.invalid_page_selection"),
                                                      QStringLiteral("Selected page was not found or invalid")));
            }
            requestedSet.insert(pId);
        }
        for (const auto& p : m_project->pages()) {
            if (requestedSet.contains(p.id())) {
                targetPages.append(p);
            }
        }
    }

    if (targetPages.isEmpty()) {
        return application::makeReadyFuture<QVector<application::ExportPageSnapshot>>(
            QVector<application::ExportPageSnapshot>{});
    }

    const domain::ProjectId projectId = m_project->id();
    const QString projectRoot = request.projectRoot.trimmed();
    const double logicalDpi = request.logicalDpi > 0.0 ? request.logicalDpi : 72.0;

    auto promise = std::make_shared<QPromise<application::Result<QVector<application::ExportPageSnapshot>>>>();
    promise->start();
    auto future = promise->future();

    struct BuilderState {
        domain::ProjectId projectId;
        QString projectRoot;
        double logicalDpi{72.0};
        application::OperationContext context;
        application::IPageImageLoader* loader{nullptr};
        application::IDerivedAssetStore* derivedStore{nullptr};
        QVector<domain::ComicPage> pages;
        int currentIndex{0};
        QVector<application::ExportPageSnapshot> snapshots;
        std::shared_ptr<QPromise<application::Result<QVector<application::ExportPageSnapshot>>>> promise;
        std::shared_ptr<std::function<void()>> processNextPage;
    };

    auto state = std::make_shared<BuilderState>(BuilderState{
        .projectId = projectId,
        .projectRoot = projectRoot,
        .logicalDpi = logicalDpi,
        .context = context,
        .loader = m_loader,
        .derivedStore = m_derivedStore,
        .pages = std::move(targetPages),
        .currentIndex = 0,
        .snapshots = {},
        .promise = promise,
    });

    auto processNextPage = std::make_shared<std::function<void()>>();
    state->processNextPage = processNextPage;
    const std::weak_ptr<std::function<void()>> weakProcessNextPage = processNextPage;
    *processNextPage = [state, weakProcessNextPage]() {
        if (state->context.stopToken.stop_requested()) {
            state->promise->addResult(
                std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                  QStringLiteral("cancelled.during_export"),
                                                                  QStringLiteral("Export was cancelled"))));
            state->processNextPage.reset();
            state->promise->finish();
            return;
        }

        if (state->currentIndex >= state->pages.size()) {
            state->promise->addResult(state->snapshots);
            state->processNextPage.reset();
            state->promise->finish();
            return;
        }

        const auto& page = state->pages.at(state->currentIndex);
        if (state->context.progressReporter) {
            state->context.progressReporter->setStatusText(
                QObject::tr("正在准备页面 (%1/%2)").arg(state->currentIndex + 1).arg(state->pages.size()));
        }

        // 1. Load source image
        application::LoadPageImageRequest loadReq{
            .projectId = state->projectId,
            .projectRoot = state->projectRoot,
            .pageId = page.id(),
            .assetPath = QString::fromStdString(page.originalAssetPath()),
            .expectedSize = page.imageSize(),
        };

        application::LoadPageImageUseCase loadUseCase(state->loader);
        auto loadFuture = loadUseCase.execute(loadReq, state->context);

        loadFuture.then([state, weakProcessNextPage, page](application::Result<QImage> loadRes) {
            if (!loadRes.has_value()) {
                state->promise->addResult(std::unexpected(loadRes.error()));
                state->processNextPage.reset();
                state->promise->finish();
                return;
            }

            QImage sourceImage = loadRes.value();
            if (sourceImage.format() != QImage::Format_RGBA8888) {
                sourceImage = sourceImage.convertToFormat(QImage::Format_RGBA8888);
            }
            sourceImage.setColorSpace(QColorSpace(QColorSpace::SRgb));

            // 2. Read clear layer if present
            auto onClearLoaded = [state, weakProcessNextPage, page, sourceImage](QImage clearLayer) mutable {
                // 3. Read manual override layer if present
                auto onManualLoaded = [state, weakProcessNextPage, page, sourceImage, clearLayer](
                                          QImage manualOverrideLayer) mutable {
                    // 4. Build translation layer
                    QImage translationLayer;
                    bool hasTextToRender = false;
                    for (const auto& region : page.regions()) {
                        if (region.isEnabled() && region.isTranslationVisible() &&
                            !region.translation().effectiveText().empty()) {
                            hasTextToRender = true;
                            break;
                        }
                    }

                    if (hasTextToRender) {
                        const int width = page.imageSize().width > 0 ? page.imageSize().width : sourceImage.width();
                        const int height = page.imageSize().height > 0 ? page.imageSize().height : sourceImage.height();
                        QImage translationImg(width, height, QImage::Format_ARGB32_Premultiplied);
                        translationImg.fill(Qt::transparent);

                        const double dpm = state->logicalDpi * 1000.0 / 25.4;
                        translationImg.setDotsPerMeterX(qRound(dpm));
                        translationImg.setDotsPerMeterY(qRound(dpm));

                        QPainter painter(&translationImg);
                        for (const auto& region : page.regions()) {
                            if (!region.isEnabled() || !region.isTranslationVisible()) {
                                continue;
                            }
                            const QString text = QString::fromStdString(region.translation().effectiveText());
                            if (text.isEmpty()) {
                                continue;
                            }
                            const auto layout = rendering::TextLayoutEngine::layoutText(
                                text, region.geometry(), region.layout(), state->logicalDpi);
                            rendering::TextRasterizer::draw(painter, layout, state->logicalDpi);
                        }
                        painter.end();

                        translationLayer = translationImg.convertToFormat(QImage::Format_RGBA8888);
                        translationLayer.setColorSpace(QColorSpace(QColorSpace::SRgb));
                    }

                    application::ExportPageSnapshot snapshot;
                    snapshot.pageId = page.id();
                    snapshot.outputStem = QString();
                    snapshot.sourceImage = sourceImage;
                    snapshot.clearLayer = clearLayer;
                    snapshot.translationLayer = translationLayer;
                    snapshot.manualOverrideLayer = manualOverrideLayer;
                    snapshot.manualOverrideLayerRef = page.manualOverrideLayerAsset().has_value()
                                                          ? QString::fromStdString(*page.manualOverrideLayerAsset())
                                                          : QString();

                    state->snapshots.append(snapshot);
                    state->currentIndex++;
                    if (const auto nextPage = weakProcessNextPage.lock()) {
                        (*nextPage)();
                    }
                };

                if (page.manualOverrideLayerAsset().has_value()) {
                    application::ReadDerivedLayerRequest manualReq{
                        .projectId = state->projectId,
                        .projectRoot = state->projectRoot,
                        .pageId = page.id(),
                        .layerKind = application::DerivedLayerKind::ManualOverride,
                        .relativePath = QString::fromStdString(*page.manualOverrideLayerAsset()),
                    };
                    auto manualFuture = state->derivedStore->readDerivedLayer(manualReq, state->context);
                    manualFuture.then([state, onManualLoaded](application::Result<QImage> manualRes) mutable {
                        if (!manualRes.has_value()) {
                            state->promise->addResult(std::unexpected(manualRes.error()));
                            state->processNextPage.reset();
                            state->promise->finish();
                            return;
                        }
                        QImage manualImg = manualRes.value();
                        if (manualImg.format() != QImage::Format_RGBA8888) {
                            manualImg = manualImg.convertToFormat(QImage::Format_RGBA8888);
                        }
                        manualImg.setColorSpace(QColorSpace(QColorSpace::SRgb));
                        onManualLoaded(manualImg);
                    });
                } else {
                    onManualLoaded(QImage());
                }
            };

            if (page.clearLayerAsset().has_value()) {
                application::ReadDerivedLayerRequest clearReq{
                    .projectId = state->projectId,
                    .projectRoot = state->projectRoot,
                    .pageId = page.id(),
                    .layerKind = application::DerivedLayerKind::Clear,
                    .relativePath = QString::fromStdString(*page.clearLayerAsset()),
                };
                auto clearFuture = state->derivedStore->readDerivedLayer(clearReq, state->context);
                clearFuture.then([state, onClearLoaded](application::Result<QImage> clearRes) mutable {
                    if (!clearRes.has_value()) {
                        state->promise->addResult(std::unexpected(clearRes.error()));
                        state->processNextPage.reset();
                        state->promise->finish();
                        return;
                    }
                    QImage clearImg = clearRes.value();
                    if (clearImg.format() != QImage::Format_RGBA8888) {
                        clearImg = clearImg.convertToFormat(QImage::Format_RGBA8888);
                    }
                    clearImg.setColorSpace(QColorSpace(QColorSpace::SRgb));
                    onClearLoaded(clearImg);
                });
            } else {
                onClearLoaded(QImage());
            }
        });
    };

    (*processNextPage)();
    return future;
}

} // namespace manga::ui::screen::editor
