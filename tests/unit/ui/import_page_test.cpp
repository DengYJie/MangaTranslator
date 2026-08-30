#include "FakeComicImporter.h"
#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeProjectPorts.h"
#include "ui/screen/import/FileDropZone.h"
#include "ui/screen/import/ImportPage.h"
#include "ui/screen/import/ImportViewModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;

class ImportPageTest : public QObject {
    Q_OBJECT
private slots:
    void submitButtonTracksFormValidation() {
        test::FakeProjectRepository repository;
        application::test::FakeComicImporter importer;
        application::OperationRegistry registry;
        ui::screen::import_::ImportViewModel viewModel(&repository, &importer, &registry);
        ui::screen::import_::ImportPage page(&viewModel);
        page.show();

        QVERIFY(!page.submitButtonForTest()->isEnabled()); // 无来源

        viewModel.addSourcePaths({QStringLiteral("D:/src/a.jpg")});
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit(); // Validate form and set errors

        QVERIFY(!page.submitButtonForTest()->isEnabled()); // 仍缺名称

        viewModel.setProjectName(QStringLiteral("项目"));
        QVERIFY(page.submitButtonForTest()->isEnabled()); // 有名称，并且errorCount=0
        QCOMPARE(page.submitButtonForTest()->text(), QStringLiteral("创建并导入"));
    }

    void dropZoneEmitsDroppedPaths() {
        test::FakeProjectRepository repository;
        application::test::FakeComicImporter importer;
        application::OperationRegistry registry;
        ui::screen::import_::ImportViewModel viewModel(&repository, &importer, &registry);
        ui::screen::import_::ImportPage page(&viewModel);
        page.show();
        QSignalSpy spy(page.dropZoneForTest(), &ui::screen::import_::FileDropZone::pathsDropped);
        auto* mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(QStringLiteral("D:/src/a.jpg")),
                       QUrl::fromLocalFile(QStringLiteral("D:/src/folder"))});

        QDragEnterEvent dragEnter(QPoint(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(page.dropZoneForTest(), &dragEnter);

        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(page.dropZoneForTest(), &drop);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().first().value<QVector<QString>>().size(), 2);
    }

    void importingStateShowsProgressAndCancelButton() {
        test::FakeProjectRepository repository;
        application::test::FakeComicImporter importer;
        importer.deferred = true;
        application::OperationRegistry registry;
        ui::screen::import_::ImportViewModel viewModel(&repository, &importer, &registry);
        ui::screen::import_::ImportPage page(&viewModel);
        page.show();
        viewModel.addSourcePaths({QStringLiteral("D:/src/a.jpg")});
        viewModel.setProjectName(QStringLiteral("进行中"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(importer.callCount == 1, 4000);
        QVERIFY(page.cancelImportButtonForTest()->isVisible());
        QVERIFY(page.progressBarForTest()->isVisible());
        QVERIFY(!page.submitButtonForTest()->isEnabled());
        viewModel.cancelImport();
        importer.complete();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().importInProgress, 4000);
    }
};

QTEST_MAIN(ImportPageTest)
#include "import_page_test.moc"
