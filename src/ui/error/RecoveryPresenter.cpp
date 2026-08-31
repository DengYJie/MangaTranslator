#include "ui/error/RecoveryPresenter.h"

namespace manga::ui::error {

RecoveryPresenter::RecoveryPresenter(qint64 deduplicationWindowMs) : m_windowMs(deduplicationWindowMs) {}

void RecoveryPresenter::reset() {
    m_history.clear();
}

std::optional<RecoveryPresentation>
RecoveryPresenter::present(const application::ApplicationError& error, const QString& targetKey, qint64 nowMs) {
    if (error.isCancelled() || error.category == application::ErrorCategory::Cancelled ||
        error.code.startsWith(QLatin1String("cancelled"))) {
        return std::nullopt;
    }

    const qint64 timestamp = (nowMs > 0) ? nowMs : QDateTime::currentMSecsSinceEpoch();
    const QString key = error.code + QLatin1Char('|') + targetKey;

    int count = 1;
    auto it = m_history.find(key);
    if (it != m_history.end() && timestamp >= it->lastSeenMs && (timestamp - it->lastSeenMs) < m_windowMs) {
        it->count++;
        it->lastSeenMs = timestamp;
        count = it->count;
    } else {
        m_history.insert(key, DeduplicationEntry{.lastSeenMs = timestamp, .count = 1});
    }

    RecoveryPresentation presentation;
    presentation.code = error.code;
    presentation.safeMessage = localizedSafeMessage(error);
    presentation.actionText = localizedActionText(error.recovery);
    presentation.action = error.recovery;
    presentation.targetKey = targetKey;
    presentation.occurrenceCount = count;
    presentation.blocking = (error.code == QLatin1String("persistence.recovery_failed"));

    return presentation;
}

QString RecoveryPresenter::localizedSafeMessage(const application::ApplicationError& error) {
    const QString& code = error.code;

    if (code == QLatin1String("persistence.not_found")) {
        return QCoreApplication::translate("RecoveryPresenter", "项目文件夹不存在或已移动");
    }
    if (code == QLatin1String("project.not_a_project")) {
        return QCoreApplication::translate("RecoveryPresenter", "所选文件夹不是 MangaTranslator 项目");
    }
    if (code == QLatin1String("persistence.permission_denied")) {
        return QCoreApplication::translate("RecoveryPresenter", "没有读取该项目文件夹的权限");
    }
    if (code == QLatin1String("persistence.read_only")) {
        return QCoreApplication::translate("RecoveryPresenter", "该项目文件夹不可写，无法以编辑模式打开");
    }
    if (code == QLatin1String("persistence.locked")) {
        return QCoreApplication::translate("RecoveryPresenter", "项目已在另一个 MangaTranslator 实例中打开");
    }
    if (code == QLatin1String("persistence.io")) {
        return QCoreApplication::translate("RecoveryPresenter", "暂时无法访问项目文件夹");
    }
    if (code == QLatin1String("persistence.disk_full")) {
        return QCoreApplication::translate("RecoveryPresenter", "磁盘空间不足，无法完成操作");
    }
    if (code == QLatin1String("persistence.target_not_empty")) {
        return QCoreApplication::translate("RecoveryPresenter", "目标文件夹非空");
    }
    if (code == QLatin1String("persistence.recovery_failed")) {
        return QCoreApplication::translate("RecoveryPresenter", "项目文件损坏且自动恢复失败");
    }
    if (code == QLatin1String("project.unsupported_schema_version")) {
        return QCoreApplication::translate("RecoveryPresenter", "不受支持的项目架构版本");
    }
    if (code == QLatin1String("project.unsupported_format_version")) {
        return QCoreApplication::translate("RecoveryPresenter", "不受支持的项目格式版本");
    }
    if (code == QLatin1String("project.invalid_structure")) {
        return QCoreApplication::translate("RecoveryPresenter", "项目结构无效");
    }
    if (code == QLatin1String("project.missing_asset")) {
        return QCoreApplication::translate("RecoveryPresenter", "项目资源文件缺失");
    }
    if (code == QLatin1String("project.image_dimension_mismatch")) {
        return QCoreApplication::translate("RecoveryPresenter", "图像尺寸与元数据不匹配");
    }
    if (code == QLatin1String("project.invalid_semantic")) {
        return QCoreApplication::translate("RecoveryPresenter", "项目数据校验失败");
    }
    if (code == QLatin1String("project.page_not_found")) {
        return QCoreApplication::translate("RecoveryPresenter", "找不到指定的页面");
    }
    if (code == QLatin1String("project.mismatch")) {
        return QCoreApplication::translate("RecoveryPresenter", "项目已发生更改");
    }
    if (code == QLatin1String("model.missing")) {
        return QCoreApplication::translate("RecoveryPresenter", "模型文件缺失");
    }
    if (code == QLatin1String("model.invalid")) {
        return QCoreApplication::translate("RecoveryPresenter", "模型文件损坏或无效");
    }
    if (code == QLatin1String("model.license_blocked")) {
        return QCoreApplication::translate("RecoveryPresenter", "模型许可协议限制");
    }
    if (code == QLatin1String("network.unauthorized")) {
        return QCoreApplication::translate("RecoveryPresenter", "API 认证失败，请检查 API Key");
    }
    if (code == QLatin1String("network.rate_limited")) {
        return QCoreApplication::translate("RecoveryPresenter", "请求过于频繁，请稍后重试");
    }
    if (code == QLatin1String("network.timeout")) {
        return QCoreApplication::translate("RecoveryPresenter", "网络请求超时");
    }
    if (code == QLatin1String("network.unavailable")) {
        return QCoreApplication::translate("RecoveryPresenter", "无法连接到翻译服务");
    }
    if (code == QLatin1String("network.invalid_endpoint")) {
        return QCoreApplication::translate("RecoveryPresenter", "API 端点无效");
    }
    if (code == QLatin1String("credentials.missing")) {
        return QCoreApplication::translate("RecoveryPresenter", "未配置 API Key");
    }
    if (code == QLatin1String("credentials.unavailable")) {
        return QCoreApplication::translate("RecoveryPresenter", "凭据服务不可用");
    }
    if (code == QLatin1String("inference.queue_full")) {
        return QCoreApplication::translate("RecoveryPresenter", "任务队列已满，请稍后重试");
    }
    if (code == QLatin1String("inference.failed")) {
        return QCoreApplication::translate("RecoveryPresenter", "推理执行失败");
    }

    if (!error.safeMessage.isEmpty()) {
        return error.safeMessage;
    }
    return QCoreApplication::translate("RecoveryPresenter", "发生未知错误");
}

QString RecoveryPresenter::localizedActionText(application::RecoveryAction action) {
    switch (action) {
        case application::RecoveryAction::Retry:
            return QCoreApplication::translate("RecoveryPresenter", "重试");
        case application::RecoveryAction::OpenSettings:
            return QCoreApplication::translate("RecoveryPresenter", "打开设置");
        case application::RecoveryAction::Reauthenticate:
            return QCoreApplication::translate("RecoveryPresenter", "配置 API Key");
        case application::RecoveryAction::LocateModel:
            return QCoreApplication::translate("RecoveryPresenter", "选择模型目录");
        case application::RecoveryAction::ChoosePath:
            return QCoreApplication::translate("RecoveryPresenter", "选择其他位置");
        case application::RecoveryAction::FreeDiskSpace:
            return QCoreApplication::translate("RecoveryPresenter", "查看位置");
        case application::RecoveryAction::None:
        default:
            return QString();
    }
}

QString RecoveryPresenter::settingsRouteForAction(application::RecoveryAction action, const QString& errorCode) {
    if (action == application::RecoveryAction::LocateModel) {
        return QStringLiteral("settings/ocr");
    }
    if (action == application::RecoveryAction::Reauthenticate) {
        return QStringLiteral("settings/ai-translation");
    }
    if (action == application::RecoveryAction::OpenSettings) {
        if (errorCode.startsWith(QLatin1String("model.")) || errorCode.startsWith(QLatin1String("ocr"))) {
            return QStringLiteral("settings/ocr");
        }
        if (errorCode.startsWith(QLatin1String("network.")) || errorCode.startsWith(QLatin1String("credentials."))) {
            return QStringLiteral("settings/ai-translation");
        }
        return QStringLiteral("settings/general");
    }
    return QStringLiteral("settings/general");
}

} // namespace manga::ui::error
