#include "ui/error/RecoveryPresenter.h"

#include "application/error/ApplicationError.h"

#include <QtTest>

using namespace manga;
using namespace manga::ui::error;

class RecoveryPresenterTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testCancelledReturnsNullopt() {
        RecoveryPresenter presenter;

        auto error1 = application::makeApplicationError(
            application::ErrorCategory::Cancelled, QStringLiteral("cancelled"), QStringLiteral("Operation cancelled"));
        QCOMPARE(presenter.present(error1), std::nullopt);

        auto error2 = application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                        QStringLiteral("cancelled.stale_generation"),
                                                        QStringLiteral("Request is stale"));
        QCOMPARE(presenter.present(error2), std::nullopt);

        auto error3 = application::makeApplicationError(application::ErrorCategory::Internal,
                                                        QStringLiteral("cancelled.by_user"),
                                                        QStringLiteral("Cancelled by user"));
        QCOMPARE(presenter.present(error3), std::nullopt);
    }

    void testEnumBranchesAndActionText() {
        RecoveryPresenter presenter;

        // Retry
        {
            auto err = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                         QStringLiteral("persistence.locked"),
                                                         QStringLiteral("Project locked"),
                                                         application::RecoveryAction::Retry,
                                                         true);
            const auto res = presenter.present(err, QStringLiteral("proj1"), 1000);
            QVERIFY(res.has_value());
            QCOMPARE(res->code, QStringLiteral("persistence.locked"));
            QCOMPARE(res->action, application::RecoveryAction::Retry);
            QCOMPARE(res->actionText, QStringLiteral("重试"));
            QCOMPARE(res->safeMessage, QStringLiteral("项目已在另一个 MangaTranslator 实例中打开"));
            QCOMPARE(res->blocking, false);
        }

        // OpenSettings
        {
            auto err = application::makeApplicationError(application::ErrorCategory::Network,
                                                         QStringLiteral("network.unavailable"),
                                                         QStringLiteral("Network down"),
                                                         application::RecoveryAction::OpenSettings);
            const auto res = presenter.present(err, QStringLiteral("net"), 2000);
            QVERIFY(res.has_value());
            QCOMPARE(res->code, QStringLiteral("network.unavailable"));
            QCOMPARE(res->action, application::RecoveryAction::OpenSettings);
            QCOMPARE(res->actionText, QStringLiteral("打开设置"));
            QCOMPARE(res->safeMessage, QStringLiteral("无法连接到翻译服务"));
            QCOMPARE(res->blocking, false);
        }

        // Reauthenticate
        {
            auto err = application::makeApplicationError(application::ErrorCategory::Network,
                                                         QStringLiteral("network.unauthorized"),
                                                         QStringLiteral("Unauthorized"),
                                                         application::RecoveryAction::Reauthenticate);
            const auto res = presenter.present(err, QStringLiteral("auth"), 3000);
            QVERIFY(res.has_value());
            QCOMPARE(res->code, QStringLiteral("network.unauthorized"));
            QCOMPARE(res->action, application::RecoveryAction::Reauthenticate);
            QCOMPARE(res->actionText, QStringLiteral("配置 API Key"));
            QCOMPARE(res->safeMessage, QStringLiteral("API 认证失败，请检查 API Key"));
            QCOMPARE(res->blocking, false);
        }

        // LocateModel
        {
            auto err = application::makeApplicationError(application::ErrorCategory::Model,
                                                         QStringLiteral("model.missing"),
                                                         QStringLiteral("Model not found"),
                                                         application::RecoveryAction::LocateModel);
            const auto res = presenter.present(err, QStringLiteral("model"), 4000);
            QVERIFY(res.has_value());
            QCOMPARE(res->code, QStringLiteral("model.missing"));
            QCOMPARE(res->action, application::RecoveryAction::LocateModel);
            QCOMPARE(res->actionText, QStringLiteral("选择模型目录"));
            QCOMPARE(res->safeMessage, QStringLiteral("模型文件缺失"));
            QCOMPARE(res->blocking, false);
        }

        // ChoosePath
        {
            auto err = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                         QStringLiteral("persistence.permission_denied"),
                                                         QStringLiteral("Access denied"),
                                                         application::RecoveryAction::ChoosePath);
            const auto res = presenter.present(err, QStringLiteral("path1"), 5000);
            QVERIFY(res.has_value());
            QCOMPARE(res->code, QStringLiteral("persistence.permission_denied"));
            QCOMPARE(res->action, application::RecoveryAction::ChoosePath);
            QCOMPARE(res->actionText, QStringLiteral("选择其他位置"));
            QCOMPARE(res->safeMessage, QStringLiteral("没有读取该项目文件夹的权限"));
            QCOMPARE(res->blocking, false);
        }

        // FreeDiskSpace
        {
            auto err = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                         QStringLiteral("persistence.disk_full"),
                                                         QStringLiteral("Disk full"),
                                                         application::RecoveryAction::FreeDiskSpace);
            const auto res = presenter.present(err, QStringLiteral("disk"), 6000);
            QVERIFY(res.has_value());
            QCOMPARE(res->code, QStringLiteral("persistence.disk_full"));
            QCOMPARE(res->action, application::RecoveryAction::FreeDiskSpace);
            QCOMPARE(res->actionText, QStringLiteral("查看位置"));
            QCOMPARE(res->safeMessage, QStringLiteral("磁盘空间不足，无法完成操作"));
            QCOMPARE(res->blocking, false);
        }

        // None
        {
            auto err = application::makeApplicationError(application::ErrorCategory::Project,
                                                         QStringLiteral("project.invalid_semantic"),
                                                         QStringLiteral("Invalid structure"),
                                                         application::RecoveryAction::None);
            const auto res = presenter.present(err, QStringLiteral("proj"), 7000);
            QVERIFY(res.has_value());
            QCOMPARE(res->code, QStringLiteral("project.invalid_semantic"));
            QCOMPARE(res->action, application::RecoveryAction::None);
            QCOMPARE(res->actionText, QString());
            QCOMPARE(res->safeMessage, QStringLiteral("项目数据校验失败"));
            QCOMPARE(res->blocking, false);
        }
    }

    void testDeduplicationAndThrottlingWithin3Seconds() {
        RecoveryPresenter presenter(3000);

        auto err = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                     QStringLiteral("persistence.locked"),
                                                     QStringLiteral("Locked"),
                                                     application::RecoveryAction::Retry);

        // First occurrence at t = 1000 ms
        auto res1 = presenter.present(err, QStringLiteral("C:/manga/demo"), 1000);
        QVERIFY(res1.has_value());
        QCOMPARE(res1->occurrenceCount, 1);

        // Second occurrence at t = 2500 ms (delta = 1500 < 3000 ms)
        auto res2 = presenter.present(err, QStringLiteral("C:/manga/demo"), 2500);
        QVERIFY(res2.has_value());
        QCOMPARE(res2->occurrenceCount, 2);

        // Third occurrence at t = 3500 ms (delta = 1000 < 3000 ms from t=2500)
        auto res3 = presenter.present(err, QStringLiteral("C:/manga/demo"), 3500);
        QVERIFY(res3.has_value());
        QCOMPARE(res3->occurrenceCount, 3);

        // Fourth occurrence at t = 7000 ms (delta = 3500 >= 3000 ms from t=3500) -> resets count
        auto res4 = presenter.present(err, QStringLiteral("C:/manga/demo"), 7000);
        QVERIFY(res4.has_value());
        QCOMPARE(res4->occurrenceCount, 1);

        // Different targetKey at t = 7500 ms -> independent entry
        auto resDiffTarget = presenter.present(err, QStringLiteral("C:/manga/other"), 7500);
        QVERIFY(resDiffTarget.has_value());
        QCOMPARE(resDiffTarget->occurrenceCount, 1);

        // Reset clears all history
        presenter.reset();
        auto resAfterReset = presenter.present(err, QStringLiteral("C:/manga/demo"), 7600);
        QVERIFY(resAfterReset.has_value());
        QCOMPARE(resAfterReset->occurrenceCount, 1);
    }

    void testTask9ProjectDirectoryMatrix() {
        RecoveryPresenter presenter;

        struct MatrixTestCase {
            QString code;
            application::RecoveryAction action;
            QString expectedActionText;
            QString expectedSafeMessage;
            bool expectedBlocking;
        };

        const QVector<MatrixTestCase> testCases = {
            {QStringLiteral("persistence.not_found"),
             application::RecoveryAction::ChoosePath,
             QStringLiteral("选择其他位置"),
             QStringLiteral("项目文件夹不存在或已移动"),
             false},
            {QStringLiteral("project.not_a_project"),
             application::RecoveryAction::ChoosePath,
             QStringLiteral("选择其他位置"),
             QStringLiteral("所选文件夹不是 MangaTranslator 项目"),
             false},
            {QStringLiteral("persistence.permission_denied"),
             application::RecoveryAction::ChoosePath,
             QStringLiteral("选择其他位置"),
             QStringLiteral("没有读取该项目文件夹的权限"),
             false},
            {QStringLiteral("persistence.read_only"),
             application::RecoveryAction::ChoosePath,
             QStringLiteral("选择其他位置"),
             QStringLiteral("该项目文件夹不可写，无法以编辑模式打开"),
             false},
            {QStringLiteral("persistence.locked"),
             application::RecoveryAction::Retry,
             QStringLiteral("重试"),
             QStringLiteral("项目已在另一个 MangaTranslator 实例中打开"),
             false},
            {QStringLiteral("persistence.io"),
             application::RecoveryAction::Retry,
             QStringLiteral("重试"),
             QStringLiteral("暂时无法访问项目文件夹"),
             false},
            {QStringLiteral("persistence.recovery_failed"),
             application::RecoveryAction::None,
             QString(),
             QStringLiteral("项目文件损坏且自动恢复失败"),
             true},
        };

        for (const auto& tc : testCases) {
            auto err = application::makeApplicationError(
                application::ErrorCategory::Persistence, tc.code, QStringLiteral("fallback"), tc.action);
            const auto res = presenter.present(err, tc.code);
            QVERIFY2(res.has_value(), qPrintable(QStringLiteral("Failed for code: %1").arg(tc.code)));
            QCOMPARE(res->code, tc.code);
            QCOMPARE(res->action, tc.action);
            QCOMPARE(res->actionText, tc.expectedActionText);
            QCOMPARE(res->safeMessage, tc.expectedSafeMessage);
            QCOMPARE(res->blocking, tc.expectedBlocking);
        }
    }

    void testSecurityDiagnosticsAndPathsNeverLeak() {
        RecoveryPresenter presenter;

        application::ApplicationError sensitiveError;
        sensitiveError.category = application::ErrorCategory::Persistence;
        sensitiveError.code = QStringLiteral("persistence.locked");
        sensitiveError.safeMessage = QStringLiteral("Safe fallback");
        sensitiveError.diagnostic =
            QStringLiteral("AccessDenied at F:\\Users\\Administrator\\.mangatranslator.lock (PID 8820, Token 0xdead)");
        sensitiveError.recovery = application::RecoveryAction::Retry;

        const auto res = presenter.present(sensitiveError, QStringLiteral("target"));
        QVERIFY(res.has_value());

        // Safe message must be localized and contain NO sensitive paths or diagnostics
        QVERIFY(!res->safeMessage.contains(QStringLiteral("Administrator")));
        QVERIFY(!res->safeMessage.contains(QStringLiteral("mangatranslator.lock")));
        QVERIFY(!res->safeMessage.contains(QStringLiteral("PID")));
        QVERIFY(!res->safeMessage.contains(QStringLiteral("0xdead")));
        QVERIFY(!res->safeMessage.contains(QLatin1Char('\\')));

        QCOMPARE(res->safeMessage, QStringLiteral("项目已在另一个 MangaTranslator 实例中打开"));
    }

    void testFallbackForUnknownCode() {
        RecoveryPresenter presenter;

        auto err = application::makeApplicationError(application::ErrorCategory::Project,
                                                     QStringLiteral("custom.unknown_code"),
                                                     QStringLiteral("Custom safe error message"),
                                                     application::RecoveryAction::Retry);

        const auto res = presenter.present(err, QStringLiteral("custom"));
        QVERIFY(res.has_value());
        QCOMPARE(res->code, QStringLiteral("custom.unknown_code"));
        QCOMPARE(res->safeMessage, QStringLiteral("Custom safe error message"));
        QCOMPARE(res->action, application::RecoveryAction::Retry);
        QCOMPARE(res->actionText, QStringLiteral("重试"));
    }

    void testSettingsRouteForAction() {
        QCOMPARE(RecoveryPresenter::settingsRouteForAction(application::RecoveryAction::LocateModel),
                 QStringLiteral("settings/models"));
        QCOMPARE(RecoveryPresenter::settingsRouteForAction(application::RecoveryAction::Reauthenticate),
                 QStringLiteral("settings/ai-translation"));
        QCOMPARE(RecoveryPresenter::settingsRouteForAction(application::RecoveryAction::OpenSettings,
                                                           QStringLiteral("network.rate_limited")),
                 QStringLiteral("settings/ai-translation"));
        QCOMPARE(RecoveryPresenter::settingsRouteForAction(application::RecoveryAction::OpenSettings,
                                                           QStringLiteral("credentials.missing")),
                 QStringLiteral("settings/ai-translation"));
        QCOMPARE(RecoveryPresenter::settingsRouteForAction(application::RecoveryAction::OpenSettings,
                                                           QStringLiteral("model.invalid")),
                 QStringLiteral("settings/models"));
        QCOMPARE(RecoveryPresenter::settingsRouteForAction(application::RecoveryAction::OpenSettings,
                                                           QStringLiteral("general.unknown")),
                 QStringLiteral("settings/models"));
    }
};

QTEST_MAIN(RecoveryPresenterTest)
#include "RecoveryPresenterTest.moc"
