#pragma once

namespace manga::application {

enum class OperationStage {
    Project,
    Import,
    LoadImage,
    Ocr,
    Translate,
    Clear,
    Layout,
    Export
};

} // namespace manga::application
