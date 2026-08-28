#pragma once

#include "application/dto/ClearDto.h"
#include "application/dto/LayoutDto.h"
#include "application/dto/OcrDto.h"
#include "application/dto/TranslationDto.h"
#include "application/operation/AsyncResult.h"
#include "application/ports/IInpainter.h"
#include "application/ports/IOcrEngine.h"
#include "application/ports/ITextLayouter.h"
#include "application/ports/ITranslator.h"
#include "application/workflow/OperationRegistry.h"

#include <QSet>

#include <utility>

namespace manga::application {
namespace detail {

[[nodiscard]] inline ApplicationError invalidProcessingInput(QString code, QString message) {
    return makeApplicationError(ErrorCategory::Input, std::move(code), std::move(message));
}

[[nodiscard]] inline ApplicationError unavailableDependency() {
    return makeApplicationError(
        ErrorCategory::Internal, "internal.unavailable_dependency", "Processing service is unavailable");
}

} // namespace detail

class RecognizeRegionUseCase {
public:
    RecognizeRegionUseCase(IOcrEngine* engine, OperationRegistry* registry) : m_engine(engine), m_registry(registry) {}

    [[nodiscard]] QFuture<Result<OcrResponse>>
    execute(const OcrRequest& request, domain::ProjectId projectId, quint64 generation, OperationId& outOperationId) {
        outOperationId = {};
        if (m_engine == nullptr || m_registry == nullptr) {
            return makeFailedFuture<OcrResponse>(detail::unavailableDependency());
        }
        if (!projectId.isValid() || !request.pageId.isValid() || !request.regionId.isValid() ||
            request.image.isNull() || !request.rect.isValid() || request.language.empty()) {
            return makeFailedFuture<OcrResponse>(
                detail::invalidProcessingInput("input.invalid_ocr_request", "OCR request is invalid"));
        }

        const auto context = m_registry->createOperation(generation);
        outOperationId = context.operationId;
        return m_engine->recognize(request, context);
    }

    void cancel(OperationId id) {
        if (m_registry != nullptr) {
            m_registry->cancelOperation(id);
        }
    }

private:
    IOcrEngine* m_engine;
    OperationRegistry* m_registry;
};

class TranslatePageUseCase {
public:
    TranslatePageUseCase(ITranslator* translator, OperationRegistry* registry)
        : m_translator(translator), m_registry(registry) {}

    [[nodiscard]] QFuture<Result<TranslationResponse>> execute(const TranslationRequest& request,
                                                               domain::ProjectId projectId,
                                                               domain::PageId pageId,
                                                               quint64 generation,
                                                               OperationId& outOperationId) {
        outOperationId = {};
        if (m_translator == nullptr || m_registry == nullptr) {
            return makeFailedFuture<TranslationResponse>(detail::unavailableDependency());
        }
        if (!projectId.isValid() || !pageId.isValid() || request.sourceLanguage.empty() ||
            request.targetLanguage.empty() || request.regions.isEmpty()) {
            return makeFailedFuture<TranslationResponse>(
                detail::invalidProcessingInput("input.invalid_translation_request", "Translation request is invalid"));
        }
        QSet<QString> regionIds;
        for (const auto& region : request.regions) {
            const auto key = QString::fromStdString(region.regionId.toString());
            if (!region.regionId.isValid() || regionIds.contains(key)) {
                return makeFailedFuture<TranslationResponse>(detail::invalidProcessingInput(
                    "input.duplicate_translation_region", "Translation request contains invalid or duplicate regions"));
            }
            regionIds.insert(key);
        }

        const auto context = m_registry->createOperation(generation);
        outOperationId = context.operationId;
        return m_translator->translate(request, context);
    }

    void cancel(OperationId id) {
        if (m_registry != nullptr) {
            m_registry->cancelOperation(id);
        }
    }

private:
    ITranslator* m_translator;
    OperationRegistry* m_registry;
};

class ClearRegionUseCase {
public:
    ClearRegionUseCase(IInpainter* inpainter, OperationRegistry* registry)
        : m_inpainter(inpainter), m_registry(registry) {}

    [[nodiscard]] QFuture<Result<ClearResponse>>
    execute(const ClearRequest& request, quint64 generation, OperationId& outOperationId) {
        outOperationId = {};
        if (m_inpainter == nullptr || m_registry == nullptr) {
            return makeFailedFuture<ClearResponse>(detail::unavailableDependency());
        }
        if (!request.projectId.isValid() || !request.pageId.isValid() || !request.regionId.isValid() ||
            request.pageImage.isNull() || !request.region.isValid() || !request.settings.isValid()) {
            return makeFailedFuture<ClearResponse>(
                detail::invalidProcessingInput("input.invalid_clear_request", "Clear request is invalid"));
        }

        const auto context = m_registry->createOperation(generation);
        outOperationId = context.operationId;
        return m_inpainter->clear(request, context);
    }

    void cancel(OperationId id) {
        if (m_registry != nullptr) {
            m_registry->cancelOperation(id);
        }
    }

private:
    IInpainter* m_inpainter;
    OperationRegistry* m_registry;
};

class LayoutRegionUseCase {
public:
    LayoutRegionUseCase(ITextLayouter* layouter, OperationRegistry* registry)
        : m_layouter(layouter), m_registry(registry) {}

    [[nodiscard]] QFuture<Result<LayoutResponse>> execute(const LayoutRequest& request,
                                                          domain::ProjectId projectId,
                                                          quint64 generation,
                                                          OperationId& outOperationId) {
        outOperationId = {};
        if (m_layouter == nullptr || m_registry == nullptr) {
            return makeFailedFuture<LayoutResponse>(detail::unavailableDependency());
        }
        if (!projectId.isValid() || !request.pageId.isValid() || !request.regionId.isValid() ||
            request.text.isEmpty() || !request.bounds.isValid() || !request.constraints.isValid() ||
            request.logicalDpi <= 0.0) {
            return makeFailedFuture<LayoutResponse>(
                detail::invalidProcessingInput("input.invalid_layout_request", "Layout request is invalid"));
        }

        const auto context = m_registry->createOperation(generation);
        outOperationId = context.operationId;
        return m_layouter->layout(request, context);
    }

    void cancel(OperationId id) {
        if (m_registry != nullptr) {
            m_registry->cancelOperation(id);
        }
    }

private:
    ITextLayouter* m_layouter;
    OperationRegistry* m_registry;
};

} // namespace manga::application
