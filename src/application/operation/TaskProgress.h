#pragma once

#include <QString>

#include <functional>
#include <optional>

namespace manga::application {

/// Throttled, monotonic progress facade handed to task workers. Publication policy is
/// injected at construction (the P4 scheduler installs the 50 ms throttle); a value equal
/// to the range maximum is always published so the final successful value equals total.
/// Decreasing values are clamped away.
class TaskProgress {
public:
    using RangeFn = std::function<void(qint64, qint64)>;
    using PublishFn = std::function<void(qint64)>;
    using TextFn = std::function<void(QString)>;
    using ThrottleFn = std::function<bool()>;

    TaskProgress() = default;
    TaskProgress(RangeFn rangeFn, PublishFn publishFn, TextFn textFn, ThrottleFn throttleFn)
        : m_range(std::move(rangeFn)), m_publish(std::move(publishFn)), m_text(std::move(textFn)),
          m_throttle(std::move(throttleFn)) {}

    void setRange(qint64 from, qint64 to) {
        if (m_range) {
            m_max = to;
            m_range(from, to);
        }
    }

    void setValue(qint64 value) {
        if (!m_publish) {
            return;
        }
        if (m_lastPublished.has_value() && value < *m_lastPublished) {
            value = *m_lastPublished;
        }
        const bool isFinal = m_max >= 0 && value >= m_max;
        if (!isFinal && m_throttle && !m_throttle()) {
            return;
        }
        m_publish(value);
        m_lastPublished = value;
    }

    void setText(const QString& text) {
        if (m_text) {
            m_text(text);
        }
    }

private:
    RangeFn m_range;
    PublishFn m_publish;
    TextFn m_text;
    ThrottleFn m_throttle;
    std::optional<qint64> m_lastPublished;
    qint64 m_max{-1};
};

} // namespace manga::application
