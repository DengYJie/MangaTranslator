#include "services/inpaint/SolidColorInpainter.h"

#include "application/operation/AsyncResult.h"

#include <QColor>
#include <QColorSpace>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <utility>

namespace manga::services {
namespace {

application::ApplicationError makeClearError(const QString& code, const QString& message) {
    return application::makeApplicationError(application::ErrorCategory::Input, code, message);
}

[[nodiscard]] int hexDigit(QChar character) {
    const ushort value = character.unicode();
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

/// Parses #RRGGBB (case-insensitive, exactly six digits and no surrounding whitespace).
/// Anything else, including named colors, short forms and alpha prefixes, is rejected:
/// alpha comes exclusively from ClearSettings::opacity.
[[nodiscard]] bool parseColorHex(const QString& text, int& red, int& green, int& blue) {
    if (text.size() != 7 || !text.startsWith(QLatin1Char('#'))) {
        return false;
    }
    int rgb = 0;
    for (qsizetype index = 1; index < text.size(); ++index) {
        const int digit = hexDigit(text.at(index));
        if (digit < 0) {
            return false;
        }
        rgb = (rgb << 4) | digit;
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
    auto submitted = m_executor->submit<application::ClearResponse>(
        application::OperationStage::Clear,
        TaskQueue::Interactive,
        context,
        [request = std::move(request)](
            const application::OperationContext& runContext,
            application::TaskProgress& progress) -> application::Result<application::ClearResponse> {
            progress.setRange(0, 1);
            progress.setValue(0);

            if (runContext.stopToken.stop_requested()) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                      QStringLiteral("cancelled.user_requested"),
                                                      QStringLiteral("The clear task was cancelled")));
            }

            // A non-finite rect would make the pixel expansion below meaningless.
            if (request.pageImage.isNull() || !std::isfinite(request.region.x) || !std::isfinite(request.region.y) ||
                !std::isfinite(request.region.width) || !std::isfinite(request.region.height)) {
                return std::unexpected(makeClearError(QStringLiteral("clear.invalid_region"),
                                                      QStringLiteral("The clear region is not a finite rectangle")));
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
            patch.fill(Qt::transparent);
            patch.setColorSpace(QColorSpace(QColorSpace::SRgb));
            const int alpha = static_cast<int>(std::lround(opacity * 255.0));
            // CompositionMode_Source sets the exact color including alpha, independent of
            // any default fill semantics.
            QPainter painter(&patch);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.setRenderHint(QPainter::Antialiasing, true);

            if (request.polygon.has_value() && request.polygon->isValid()) {
                QPainterPath polyPath;
                const auto& pts = request.polygon->points;
                polyPath.moveTo(pts[0].x - left, pts[0].y - top);
                for (size_t i = 1; i < pts.size(); ++i) {
                    polyPath.lineTo(pts[i].x - left, pts[i].y - top);
                }
                polyPath.closeSubpath();
                painter.fillPath(polyPath, QColor(red, green, blue, alpha));
            } else {
                painter.fillRect(0, 0, width, height, QColor(red, green, blue, alpha));
            }
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
            progress.setValue(1);
            return response;
        });
    if (!submitted) {
        return application::makeFailedFuture<application::ClearResponse>(submitted.error());
    }
    return *submitted;
}

} // namespace manga::services
