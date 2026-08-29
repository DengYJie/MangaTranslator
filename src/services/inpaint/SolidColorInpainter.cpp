#include "services/inpaint/SolidColorInpainter.h"

#include <QColor>
#include <QColorSpace>
#include <QPainter>

#include <cmath>
#include <utility>

namespace manga::services {
namespace {

application::ApplicationError makeClearError(const QString& code, const QString& message) {
    return application::makeApplicationError(application::ErrorCategory::Input, code, message);
}

/// Parses #RRGGBB (case-insensitive). Anything else, including named colors and alpha
/// prefixes, is rejected: alpha comes exclusively from ClearSettings::opacity.
[[nodiscard]] bool parseColorHex(const QString& text, int& red, int& green, int& blue) {
    const QString value = text.trimmed();
    if (value.size() != 7 || !value.startsWith(QLatin1Char('#'))) {
        return false;
    }
    bool ok = false;
    const int rgb = value.mid(1).toInt(&ok, 16);
    if (!ok) {
        return false;
    }
    red = (rgb >> 16) & 0xFF;
    green = (rgb >> 8) & 0xFF;
    blue = rgb & 0xFF;
    return true;
}

} // namespace

SolidColorInpainter::SolidColorInpainter(BoundedTaskExecutor* executor) : m_executor(executor) {}

QFuture<application::Result<application::ClearResponse>>
SolidColorInpainter::clear(application::ClearRequest request, application::OperationContext context) {
    return m_executor->submit<application::ClearResponse>(
        application::OperationStage::Clear,
        TaskQueue::Interactive,
        context,
        [request = std::move(request)](const application::OperationContext& runContext,
                                       application::TaskProgress&) -> application::Result<application::ClearResponse> {
            if (runContext.stopToken.stop_requested()) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                      QStringLiteral("cancelled.user_requested"),
                                                      QStringLiteral("The clear task was cancelled")));
            }

            int red = 0;
            int green = 0;
            int blue = 0;
            const QString colorText = QString::fromStdString(request.settings.colorHex);
            if (!parseColorHex(colorText, red, green, blue)) {
                return std::unexpected(makeClearError(QStringLiteral("clear.invalid_color"),
                                                      QStringLiteral("The clear color must be #RRGGBB")));
            }
            const double opacity = request.settings.opacity;
            if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
                return std::unexpected(makeClearError(QStringLiteral("clear.invalid_opacity"),
                                                      QStringLiteral("The clear opacity must be within [0, 1]")));
            }

            // Region to pixels: expand left/top down, right/bottom up, then clamp to the page.
            const int pageWidth = request.pageImage.width();
            const int pageHeight = request.pageImage.height();
            const int left = std::clamp(static_cast<int>(std::floor(request.region.x)), 0, pageWidth);
            const int top = std::clamp(static_cast<int>(std::floor(request.region.y)), 0, pageHeight);
            const int right =
                std::clamp(static_cast<int>(std::ceil(request.region.x + request.region.width)), 0, pageWidth);
            const int bottom =
                std::clamp(static_cast<int>(std::ceil(request.region.y + request.region.height)), 0, pageHeight);
            const int width = right - left;
            const int height = bottom - top;
            if (width < 2 || height < 2) {
                return std::unexpected(makeClearError(QStringLiteral("clear.invalid_region"),
                                                      QStringLiteral("The clear region does not intersect the page")));
            }

            QImage patch(width, height, QImage::Format_RGBA8888);
            patch.setColorSpace(QColorSpace(QColorSpace::SRgb));
            const int alpha = static_cast<int>(std::lround(opacity * 255.0));
            // CompositionMode_Source sets the exact color including alpha, independent of
            // any default fill semantics.
            QPainter painter(&patch);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(0, 0, width, height, QColor(red, green, blue, alpha));
            painter.end();

            if (runContext.stopToken.stop_requested()) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                      QStringLiteral("cancelled.user_requested"),
                                                      QStringLiteral("The clear task was cancelled")));
            }

            application::ClearResponse response;
            response.regionId = request.regionId;
            response.affectedBounds = domain::RectGeometry{.x = static_cast<double>(left),
                                                           .y = static_cast<double>(top),
                                                           .width = static_cast<double>(width),
                                                           .height = static_cast<double>(height)};
            response.patch = patch;
            return response;
        });
}

} // namespace manga::services
