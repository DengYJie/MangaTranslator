#include "ui/screen/editor/EditorState.h"

#include <QCoreApplication>

namespace manga::ui::screen::editor {

QString processingUnavailableReason(const EditorState& state, application::OperationStage stage) {
    QString reason;
    QString guidance;
    switch (stage) {
        case application::OperationStage::Detect:
            if (state.isDetectionAvailable) {
                return {};
            }
            reason = state.detectionUnavailableReason;
            guidance = QCoreApplication::translate("EditorState", "文本检测模型不可用，请前往设置配置模型。");
            break;
        case application::OperationStage::Ocr:
            if (state.isOcrAvailable) {
                return {};
            }
            reason = state.ocrUnavailableReason;
            guidance = QCoreApplication::translate("EditorState", "文字识别模型不可用，请前往设置配置模型。");
            break;
        case application::OperationStage::Translate:
            if (state.isTranslationAvailable) {
                return {};
            }
            reason = state.translationUnavailableReason;
            guidance = QCoreApplication::translate("EditorState", "翻译服务不可用，请前往设置检查端点、模型和密钥。");
            break;
        case application::OperationStage::Clear:
            if (state.isInpaintAvailable) {
                return {};
            }
            reason = state.inpaintUnavailableReason;
            guidance = QCoreApplication::translate("EditorState", "文字消除模型不可用，请前往设置配置模型。");
            break;
        default:
            return {};
    }
    return reason.isEmpty() ? guidance : guidance + QLatin1Char('\n') + reason;
}

} // namespace manga::ui::screen::editor
