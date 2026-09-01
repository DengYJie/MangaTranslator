#pragma once

namespace manga::application {

enum class OperationStage {
    Project,
    Import,
    LoadImage,
    Detect,
    Ocr,
    Translate,
    Clear,
    Layout,
    Export
};

} // namespace manga::application
