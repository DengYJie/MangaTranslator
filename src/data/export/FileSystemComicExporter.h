#pragma once

#include "application/ports/IComicExporter.h"

namespace manga::data {

/// Local filesystem comic exporter that composites the page layers back to front, original image
/// then clear, translation and manual override, applies sRGB/color conversions, never overwrites
/// existing target files, and uses atomic staging per page.
class FileSystemComicExporter final : public application::IComicExporter {
public:
    FileSystemComicExporter() = default;
    ~FileSystemComicExporter() override = default;

    FileSystemComicExporter(const FileSystemComicExporter&) = delete;
    FileSystemComicExporter& operator=(const FileSystemComicExporter&) = delete;
    FileSystemComicExporter(FileSystemComicExporter&&) = delete;
    FileSystemComicExporter& operator=(FileSystemComicExporter&&) = delete;

    [[nodiscard]] QFuture<application::Result<application::ExportResponse>>
    exportPages(application::ExportRequest request, application::OperationContext context) override;
};

} // namespace manga::data
