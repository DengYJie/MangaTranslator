#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "ui/base/DomainMetaTypes.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/screen/editor/InspectorState.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;
using manga::ui::screen::editor::EditorViewModel;
using manga::ui::screen::editor::InspectorCandidateProjection;
using manga::ui::screen::editor::InspectorMode;
using manga::ui::screen::editor::InspectorState;
using manga::ui::screen::editor::RegionSelectionMode;
using manga::ui::screen::editor::SingleRegionInspectorState;

namespace {

domain::RectGeometry makeGeometry(double x, double y) {
    domain::RectGeometry geom;
    geom.x = x;
    geom.y = y;
    geom.width = 80.0;
    geom.height = 40.0;
    return geom;
}

domain::TextLayout makeLayout(const std::string& fontFamily,
                              double fontSizePt,
                              int fontWeight,
                              const std::string& textColorHex,
                              domain::TextOrientation orientation = domain::TextOrientation::Horizontal,
                              domain::HorizontalAlignment hAlign = domain::HorizontalAlignment::Center) {
    domain::TextLayout layout;
    layout.fontFamily = fontFamily;
    layout.fontSizePt = fontSizePt;
    layout.fontWeight = fontWeight;
    layout.textColorHex = textColorHex;
    layout.orientation = orientation;
    layout.alignment.horizontal = hAlign;
    return layout;
}

/// Creates a session with one page (index 0) holding the given regions, plus
/// `extraEmptyPages` trailing empty pages.
application::OpenedProjectSession makeSession(QVector<domain::TextRegion> regions, int extraEmptyPages = 1) {
    application::OpenedProjectSession session;
    session.project = domain::ComicProject("Demo Project");
    session.projectRootPath = QStringLiteral("D:/manga/demo");
    session.project.setReadingDirection(domain::ReadingDirection::LeftToRight);

    domain::ComicPage page(
        domain::PageId::generate(), "assets/original/page_1000.png", domain::ImageSize{1200, 1800}, 0);
    for (auto& region : regions) {
        const bool added = page.addRegion(std::move(region));
        Q_ASSERT(added); // fixture regions must be valid and inside page bounds
    }
    session.project.addPage(std::move(page));
    for (int i = 0; i < extraEmptyPages; ++i) {
        session.project.addPage(
            domain::ComicPage(domain::PageId::generate(),
                              QStringLiteral("assets/original/page_%1.png").arg(1001 + i).toStdString(),
                              domain::ImageSize{800, 1200},
                              i + 1));
    }
    return session;
}

domain::RegionId regionAt(const application::OpenedProjectSession& session, int pageIndex, int regionIndex) {
    return session.project.pages().at(pageIndex).regions().at(regionIndex).id();
}

domain::PageId pageAt(const application::OpenedProjectSession& session, int index) {
    return session.project.pages().at(index).id();
}

struct Fixture {
    test::FakeThumbnailStore thumbnailStore;
    test::FakePageImageLoader imageLoader;
    application::OperationRegistry registry;
};

} // namespace

class InspectorStateTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<domain::RegionId>();
    }

    void emptySelectionYieldsNulloptInspector() {
        domain::TextRegion withText(domain::RegionId::generate(), makeGeometry(10.0, 10.0), 0);
        withText.updateSource([](domain::TextContent& source) { source.setOcrResult("hello", 0.9, "fake-ocr"); });
        withText.updateTranslation([](domain::TranslationEntry& translation) { translation.setModelResult("world"); });
        domain::TextRegion emptyRegion(domain::RegionId::generate(), makeGeometry(10.0, 110.0), 1);

        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.openProjectSession(makeSession({withText, emptyRegion}, 1));

        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::None);
        QVERIFY(!viewModel.state().inspector.has_value());
    }

    void noSessionYieldsNulloptInspector() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::None);
        QVERIFY(!viewModel.state().inspector.has_value());
    }

    void singleSelectionProjectsFullRegion() {
        domain::TextRegion region(domain::RegionId::generate(), makeGeometry(20.0, 30.0), 0);
        region.updateSource([](domain::TextContent& source) { source.setOcrResult("source text", 0.75, "fake-ocr"); });
        region.updateTranslation([](domain::TranslationEntry& translation) {
            translation.setModelResult("model text",
                                       {domain::TranslationCandidate{"c1", "candidate one", 0.8, "provider-a"},
                                        domain::TranslationCandidate{"c2", "candidate two", 0.6, "provider-b"}});
            translation.selectCandidate("c2");
            translation.setManualText("manual override");
        });
        region.updateLayout([](domain::TextLayout& layout) {
            layout.fontFamily = "Comic Sans";
            layout.fontSizePt = 18.0;
            layout.fontWeight = 700;
            layout.letterSpacingEm = 0.05;
            layout.orientation = domain::TextOrientation::Vertical;
            layout.alignment.horizontal = domain::HorizontalAlignment::Left;
            layout.alignment.vertical = domain::VerticalAlignment::Top;
            layout.lineSpacing = 1.5;
            layout.margins = domain::Margins{1.0, 2.0, 3.0, 4.0};
            layout.textColorHex = "#112233";
            layout.strokeColorHex = "#AABBCC";
            layout.strokeWidthPx = 2.5;
        });
        region.setIsCleared(true);
        const auto geometry = region.geometry();

        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession({region}, 0);
        viewModel.openProjectSession(session);
        viewModel.selectRegion(regionAt(session, 0, 0), RegionSelectionMode::Replace);

        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::Single);
        QVERIFY(viewModel.state().inspector.has_value());
        const auto& single = *viewModel.state().inspector;
        QCOMPARE(single.pageId, pageAt(session, 0));
        QCOMPARE(single.regionId, regionAt(session, 0, 0));
        QCOMPARE(single.sourceText, QStringLiteral("source text"));
        QCOMPARE(single.sourceType, domain::TextSourceType::OcrAuto);
        QCOMPARE(single.sourceConfidence, 0.75);
        QVERIFY(single.hasManualTranslation);
        QCOMPARE(single.manualTranslation, QStringLiteral("manual override"));
        QCOMPARE(single.effectiveTranslation, QStringLiteral("manual override"));
        QCOMPARE(single.candidates.size(), 2);
        QCOMPARE(single.candidates.at(0).id, QStringLiteral("c1"));
        QCOMPARE(single.candidates.at(0).text, QStringLiteral("candidate one"));
        QCOMPARE(single.candidates.at(0).provider, QStringLiteral("provider-a"));
        QVERIFY(!single.candidates.at(0).isSelected);
        QCOMPARE(single.candidates.at(1).id, QStringLiteral("c2"));
        QVERIFY(single.candidates.at(1).isSelected);
        QVERIFY(single.isCleared);
        QVERIFY(!single.isLocked);
        QCOMPARE(single.geometry, geometry);
        QCOMPARE(single.orderIndex, 0);
        QCOMPARE(single.fontFamily, QStringLiteral("Comic Sans"));
        QCOMPARE(single.fontSizePt, 18.0);
        QCOMPARE(single.fontWeight, 700);
        QCOMPARE(single.letterSpacingEm, 0.05);
        QCOMPARE(single.orientation, domain::TextOrientation::Vertical);
        QCOMPARE(single.horizontalAlignment, domain::HorizontalAlignment::Left);
        QCOMPARE(single.verticalAlignment, domain::VerticalAlignment::Top);
        QCOMPARE(single.lineSpacing, 1.5);
        QCOMPARE(single.margins, (domain::Margins{1.0, 2.0, 3.0, 4.0}));
        QCOMPARE(single.textColorHex, QStringLiteral("#112233"));
        QCOMPARE(single.strokeColorHex, QStringLiteral("#AABBCC"));
        QCOMPARE(single.strokeWidthPx, 2.5);
    }

    void singleSelectionEffectiveTranslationFallsBackToCandidate() {
        domain::TextRegion region(domain::RegionId::generate(), makeGeometry(10.0, 10.0), 0);
        region.updateTranslation([](domain::TranslationEntry& translation) {
            translation.setModelResult("model text", {domain::TranslationCandidate{"c1", "candidate one", 0.8, "p"}});
            translation.selectCandidate("c1");
        });

        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession({region}, 0);
        viewModel.openProjectSession(session);
        viewModel.selectRegion(regionAt(session, 0, 0), RegionSelectionMode::Replace);

        QVERIFY(viewModel.state().inspector.has_value());
        const auto& single = *viewModel.state().inspector;
        QVERIFY(!single.hasManualTranslation);
        QCOMPARE(single.effectiveTranslation, QStringLiteral("candidate one"));
    }

    void multiSelectionYieldsNulloptInspector() {
        domain::TextRegion first(domain::RegionId::generate(), makeGeometry(10.0, 10.0), 0);
        first.updateLayout([](domain::TextLayout& layout) { layout = makeLayout("Comic Sans", 18.0, 700, "#111111"); });
        first.updateSource([](domain::TextContent& source) { source.setPureManual("first source"); });
        domain::TextRegion second(domain::RegionId::generate(), makeGeometry(10.0, 110.0), 1);
        second.updateLayout(
            [](domain::TextLayout& layout) { layout = makeLayout("Comic Sans", 18.0, 700, "#111111"); });

        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession({first, second}, 0);
        viewModel.openProjectSession(session);
        viewModel.selectRegion(regionAt(session, 0, 0), RegionSelectionMode::Replace);
        viewModel.selectRegion(regionAt(session, 0, 1), RegionSelectionMode::Toggle);

        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::None);
        QVERIFY(!viewModel.state().inspector.has_value());
    }

    void deletingSelectionFallsBackToNullopt() {
        domain::TextRegion region(domain::RegionId::generate(), makeGeometry(10.0, 10.0), 0);
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession({region}, 0);
        viewModel.openProjectSession(session);
        viewModel.selectRegion(regionAt(session, 0, 0), RegionSelectionMode::Replace);
        QVERIFY(viewModel.state().inspector.has_value());

        viewModel.deleteRegions({regionAt(session, 0, 0)});

        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::None);
        QVERIFY(!viewModel.state().inspector.has_value());
    }

    void pageSwitchResetsInspectorToNullopt() {
        domain::TextRegion region(domain::RegionId::generate(), makeGeometry(10.0, 10.0), 0);
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession({region}, 1);
        viewModel.openProjectSession(session);
        viewModel.selectRegion(regionAt(session, 0, 0), RegionSelectionMode::Replace);
        QVERIFY(viewModel.state().inspector.has_value());

        viewModel.selectPage(pageAt(session, 1));

        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::None);
        QVERIFY(!viewModel.state().inspector.has_value());
    }

    void closingSessionResetsInspector() {
        domain::TextRegion region(domain::RegionId::generate(), makeGeometry(10.0, 10.0), 0);
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession({region}, 0);
        viewModel.openProjectSession(session);
        viewModel.selectRegion(regionAt(session, 0, 0), RegionSelectionMode::Replace);
        QVERIFY(viewModel.state().inspector.has_value());

        viewModel.closeProjectSession();

        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::None);
        QVERIFY(!viewModel.state().inspector.has_value());
    }
};

QTEST_MAIN(InspectorStateTest)
#include "inspector_state_test.moc"
