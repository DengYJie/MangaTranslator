#include "ui/screen/editor/TextEditSession.h"

#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "ui/command/RegionPropertyCommand.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QtTest>

using namespace manga;
using manga::ui::command::RegionPropertyField;
using manga::ui::command::TextEditSessionKey;
using manga::ui::screen::editor::EditorViewModel;
using manga::ui::screen::editor::InspectorField;
using manga::ui::screen::editor::RegionSelectionMode;
using manga::ui::screen::editor::SingleRegionInspectorState;
using manga::ui::screen::editor::TextEditSession;

namespace {

struct ManualClock {
    qint64 nowMs{10'000};
    void advance(qint64 ms) {
        nowMs += ms;
    }
};

/// Creates a session with `pageCount` pages, each page having `regionsPerPage` regions.
/// Region 0 of every page carries an OCR source text and two translation candidates.
application::OpenedProjectSession makeSession(int pageCount, int regionsPerPage = 0, bool lockFirstRegion = false) {
    application::OpenedProjectSession session;
    session.project = domain::ComicProject("Demo Project");
    session.projectRootPath = QStringLiteral("D:/manga/demo");
    for (int i = 0; i < pageCount; ++i) {
        auto pageId = domain::PageId::generate();
        domain::ComicPage page(pageId,
                               QStringLiteral("assets/original/page_%1.png").arg(1000 + i).toStdString(),
                               domain::ImageSize{1200, 1800},
                               i);
        for (int r = 0; r < regionsPerPage; ++r) {
            domain::RectGeometry geom;
            geom.x = 10.0 + r * 100.0;
            geom.y = 10.0 + r * 50.0;
            geom.width = 80.0;
            geom.height = 40.0;
            domain::TextRegion region(domain::RegionId::generate(), geom, r);
            if (r == 0) {
                domain::TextContent source;
                source.setOcrResult("recognized", 0.9, "fake-ocr");
                region.setSource(source);
                domain::TranslationEntry translation;
                translation.setModelResult("model text",
                                           {domain::TranslationCandidate{"c1", "candidate one", 0.8, "provider-a"},
                                            domain::TranslationCandidate{"c2", "candidate two", 0.6, "provider-b"}});
                region.setTranslation(translation);
            }
            if (r == 0 && lockFirstRegion) {
                region.setLocked(true);
            }
            page.addRegion(std::move(region));
        }
        session.project.addPage(std::move(page));
    }
    return session;
}

domain::PageId pageAt(const application::OpenedProjectSession& session, int index) {
    return session.project.pages().at(index).id();
}

domain::RegionId regionAt(const application::OpenedProjectSession& session, int pageIndex, int regionIndex) {
    return session.project.pages().at(pageIndex).regions().at(regionIndex).id();
}

struct Fixture {
    test::FakeThumbnailStore thumbnailStore;
    test::FakePageImageLoader imageLoader;
    application::OperationRegistry registry;
    ManualClock clock;
    explicit Fixture() {
        imageLoader.nextImage = QImage(64, 96, QImage::Format_RGBA8888);
        imageLoader.nextImage.fill(Qt::gray);
    }
};

SingleRegionInspectorState singleInspector(const EditorViewModel& viewModel) {
    return std::get<SingleRegionInspectorState>(viewModel.state().inspector);
}

} // namespace

class TextEditSessionTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // TextEditSession unit behavior.
    void keyForKeepsSessionIdWithinActiveSession() {
        TextEditSession session;
        const auto projectId = domain::ProjectId::generate();
        const auto pageId = domain::PageId::generate();
        const auto regionId = domain::RegionId::generate();

        const auto first = session.keyFor(projectId, pageId, regionId, RegionPropertyField::ManualTranslation);
        const auto second = session.keyFor(projectId, pageId, regionId, RegionPropertyField::ManualTranslation);
        QCOMPARE(first, second);
        QCOMPARE(first.projectId, projectId);
        QCOMPARE(first.pageId, pageId);
        QCOMPARE(first.regionId, regionId);
        QCOMPARE(first.field, RegionPropertyField::ManualTranslation);
        QVERIFY(session.hasActiveSession());
    }

    void sealStartsNewSessionId() {
        TextEditSession session;
        const auto projectId = domain::ProjectId::generate();
        const auto pageId = domain::PageId::generate();
        const auto regionId = domain::RegionId::generate();

        const auto before = session.keyFor(projectId, pageId, regionId, RegionPropertyField::ManualTranslation);
        session.seal();
        QVERIFY(!session.hasActiveSession());
        const auto after = session.keyFor(projectId, pageId, regionId, RegionPropertyField::ManualTranslation);
        QVERIFY(before != after);
        QVERIFY(after.sessionId != before.sessionId);
    }

    void identityChangeStartsNewSession() {
        TextEditSession session;
        const auto projectId = domain::ProjectId::generate();
        const auto pageId = domain::PageId::generate();
        const auto regionId = domain::RegionId::generate();

        const auto base = session.keyFor(projectId, pageId, regionId, RegionPropertyField::ManualTranslation);
        const auto otherField = session.keyFor(projectId, pageId, regionId, RegionPropertyField::SourceText);
        QVERIFY(otherField.sessionId != base.sessionId);
        const auto otherRegion =
            session.keyFor(projectId, pageId, domain::RegionId::generate(), RegionPropertyField::SourceText);
        QVERIFY(otherRegion.sessionId != otherField.sessionId);
        const auto otherPage =
            session.keyFor(projectId, domain::PageId::generate(), regionId, RegionPropertyField::SourceText);
        QVERIFY(otherPage.sessionId != otherRegion.sessionId);
        const auto otherProject =
            session.keyFor(domain::ProjectId::generate(), pageId, regionId, RegionPropertyField::SourceText);
        QVERIFY(otherProject.sessionId != otherPage.sessionId);
    }

    void nowMsUsesInjectedClock() {
        ManualClock clock;
        TextEditSession session([&clock]() { return clock.nowMs; });
        QCOMPARE(session.nowMs(), clock.nowMs);
        clock.advance(250);
        QCOMPARE(session.nowMs(), clock.nowMs);
    }

    void resetClearsActiveSession() {
        TextEditSession session;
        (void)session.keyFor(domain::ProjectId::generate(),
                             domain::PageId::generate(),
                             domain::RegionId::generate(),
                             RegionPropertyField::ManualTranslation);
        QVERIFY(session.hasActiveSession());
        session.reset();
        QVERIFY(!session.hasActiveSession());
    }

    // EditorViewModel integration: merge window, sealing, provenance, candidates, batch style.
    void textEditsWithinWindowMergeIntoOneCommand() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.setTextEditClockForTest([&f]() { return f.clock.nowMs; });
        const auto session = makeSession(1, 1);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);

        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("A"));
        f.clock.advance(499);
        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("AB"));

        QCOMPARE(viewModel.undoStackCountForTest(), 1);
        QCOMPARE(singleInspector(viewModel).manualTranslation, QStringLiteral("AB"));

        viewModel.undo();
        QCOMPARE(singleInspector(viewModel).manualTranslation, QString());
        viewModel.redo();
        QCOMPARE(singleInspector(viewModel).manualTranslation, QStringLiteral("AB"));
    }

    void textEditsBeyondWindowCreateSeparateCommands() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.setTextEditClockForTest([&f]() { return f.clock.nowMs; });
        const auto session = makeSession(1, 1);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);

        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("A"));
        f.clock.advance(501);
        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("AB"));

        QCOMPARE(viewModel.undoStackCountForTest(), 2);
        viewModel.undo();
        QCOMPARE(singleInspector(viewModel).manualTranslation, QStringLiteral("A"));
    }

    void focusOutSealsBatch() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.setTextEditClockForTest([&f]() { return f.clock.nowMs; });
        const auto session = makeSession(1, 1);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);

        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("A"));
        viewModel.sealTextEditSession();
        f.clock.advance(50);
        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("AB"));

        QCOMPARE(viewModel.undoStackCountForTest(), 2);
    }

    void saveSealsBatch() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.setTextEditClockForTest([&f]() { return f.clock.nowMs; });
        const auto session = makeSession(1, 1);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);

        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("A"));
        viewModel.prepareForSave(); // Simulates save action sealing the batch
        f.clock.advance(50);
        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("AB"));

        QCOMPARE(viewModel.undoStackCountForTest(), 2);
    }

    void selectionChangeSealsBatch() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.setTextEditClockForTest([&f]() { return f.clock.nowMs; });
        const auto session = makeSession(1, 2);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto firstRegion = regionAt(session, 0, 0);
        const auto secondRegion = regionAt(session, 0, 1);
        viewModel.selectRegion(firstRegion, RegionSelectionMode::Replace);

        viewModel.commitInspectorFieldEdit(
            pageId, {firstRegion}, InspectorField::ManualTranslation, QStringLiteral("A"));
        viewModel.selectRegion(secondRegion, RegionSelectionMode::Replace);
        f.clock.advance(50);
        viewModel.commitInspectorFieldEdit(
            pageId, {secondRegion}, InspectorField::ManualTranslation, QStringLiteral("B"));

        QCOMPARE(viewModel.undoStackCountForTest(), 2);
    }

    void pageSwitchSealsBatch() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.setTextEditClockForTest([&f]() { return f.clock.nowMs; });
        const auto session = makeSession(2, 1);
        viewModel.openProjectSession(session);
        const auto firstPage = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);

        viewModel.commitInspectorFieldEdit(
            firstPage, {regionId}, InspectorField::ManualTranslation, QStringLiteral("A"));

        // Round trip through another page, then reselect the same region and keep typing
        // within the merge window. The page switch must have sealed the previous batch.
        viewModel.selectPage(pageAt(session, 1));
        viewModel.selectPage(firstPage);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);
        f.clock.advance(50);
        viewModel.commitInspectorFieldEdit(
            firstPage, {regionId}, InspectorField::ManualTranslation, QStringLiteral("AB"));

        QCOMPARE(viewModel.undoStackCountForTest(), 2);
    }

    void undoSealsBatch() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.setTextEditClockForTest([&f]() { return f.clock.nowMs; });
        const auto session = makeSession(1, 1);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);

        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("A"));
        f.clock.advance(100);
        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("AB"));
        QCOMPARE(viewModel.undoStackCountForTest(), 1);

        viewModel.undo();
        QCOMPARE(singleInspector(viewModel).manualTranslation, QString());

        // Typing again must not merge into the undone command.
        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("C"));
        QCOMPARE(viewModel.undoStackCountForTest(), 1);
        QCOMPARE(singleInspector(viewModel).manualTranslation, QStringLiteral("C"));
        QVERIFY(!viewModel.state().canRedo);
    }

    void sourceEditKeepsOcrProvenance() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.setTextEditClockForTest([&f]() { return f.clock.nowMs; });
        const auto session = makeSession(1, 1);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);
        QCOMPARE(singleInspector(viewModel).sourceType, domain::TextSourceType::OcrAuto);

        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::SourceText, QStringLiteral("corrected"));

        const auto edited = singleInspector(viewModel);
        QCOMPARE(edited.sourceText, QStringLiteral("corrected"));
        QCOMPARE(edited.sourceType, domain::TextSourceType::OcrManualModified);
        QCOMPARE(edited.sourceConfidence, 0.9);

        viewModel.undo();
        const auto restored = singleInspector(viewModel);
        QCOMPARE(restored.sourceText, QStringLiteral("recognized"));
        QCOMPARE(restored.sourceType, domain::TextSourceType::OcrAuto);
    }

    void candidateAdoptionIsItsOwnCommand() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.setTextEditClockForTest([&f]() { return f.clock.nowMs; });
        const auto session = makeSession(1, 1);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);

        // Even within the merge window, adoption must not merge into the text batch.
        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("A"));
        f.clock.advance(50);
        viewModel.adoptCandidate(pageId, regionId, QStringLiteral("c1"));

        QCOMPARE(viewModel.undoStackCountForTest(), 2);
        const auto adopted = singleInspector(viewModel);
        QVERIFY(!adopted.candidates.isEmpty());
        QVERIFY(adopted.candidates.at(0).isSelected);
        QCOMPARE(adopted.effectiveTranslation, QStringLiteral("A")); // manual text still wins

        viewModel.undo();
        const auto reverted = singleInspector(viewModel);
        QVERIFY(!reverted.candidates.at(0).isSelected);
        QCOMPARE(reverted.manualTranslation, QStringLiteral("A"));
    }

    void candidateAdoptionRejectsUnknownCandidate() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(1, 1);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);

        viewModel.adoptCandidate(pageId, regionId, QStringLiteral("missing"));
        QCOMPARE(viewModel.undoStackCountForTest(), 0);
        QVERIFY(!singleInspector(viewModel).candidates.at(0).isSelected);
    }

    void batchStyleEditIsOneMacroCommand() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(1, 3);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const QVector<domain::RegionId> regionIds{
            regionAt(session, 0, 0), regionAt(session, 0, 1), regionAt(session, 0, 2)};
        for (const auto& id : regionIds) {
            viewModel.selectRegion(id, RegionSelectionMode::Toggle);
        }

        viewModel.commitInspectorFieldEdit(pageId, regionIds, InspectorField::FontSizePt, 20.0);

        QCOMPARE(viewModel.undoStackCountForTest(), 1);
        viewModel.selectRegion(regionIds.first(), RegionSelectionMode::Replace);
        QCOMPARE(singleInspector(viewModel).fontSizePt, 20.0);

        viewModel.undo();
        QCOMPARE(singleInspector(viewModel).fontSizePt, 14.0);
    }

    void batchStyleEditWithInvalidTargetIsNotCommitted() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(1, 2);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const QVector<domain::RegionId> regionIds{regionAt(session, 0, 0), domain::RegionId::generate()};

        viewModel.commitInspectorFieldEdit(pageId, regionIds, InspectorField::FontSizePt, 20.0);

        // One invalid target vetoes the whole batch: no command, no mutation.
        QCOMPARE(viewModel.undoStackCountForTest(), 0);
        const auto* region = session.project.findPage(pageId)->findRegion(regionIds.first());
        QCOMPARE(region->layout().fontSizePt, 14.0);
    }

    void styleEditsDoNotMergeWithEachOther() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.setTextEditClockForTest([&f]() { return f.clock.nowMs; });
        const auto session = makeSession(1, 1);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);

        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::FontSizePt, 20.0);
        f.clock.advance(50);
        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::FontSizePt, 22.0);

        QCOMPARE(viewModel.undoStackCountForTest(), 2);
    }

    void lockedRegionRejectsEdits() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(1, 1, true);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);

        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("A"));
        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::FontSizePt, 20.0);
        viewModel.adoptCandidate(pageId, regionId, QStringLiteral("c1"));

        QCOMPARE(viewModel.undoStackCountForTest(), 0);
        QCOMPARE(singleInspector(viewModel).manualTranslation, QString());
    }

    void textEditRefreshesUndoStateAndDirtyFlag() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(1, 1);
        viewModel.openProjectSession(session);
        const auto pageId = pageAt(session, 0);
        const auto regionId = regionAt(session, 0, 0);
        viewModel.selectRegion(regionId, RegionSelectionMode::Replace);
        QVERIFY(!viewModel.state().canUndo);

        viewModel.commitInspectorFieldEdit(pageId, {regionId}, InspectorField::ManualTranslation, QStringLiteral("A"));

        QVERIFY(viewModel.state().canUndo);
        QVERIFY(viewModel.state().saveState.isDirty);
    }
};

QTEST_GUILESS_MAIN(TextEditSessionTest)
#include "TextEditSessionTest.moc"
