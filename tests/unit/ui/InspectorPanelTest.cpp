#include "ui/widget/InspectorPanel.h"

#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "ui/base/DomainMetaTypes.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/screen/editor/InspectorState.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/Layout.h>
#include <FluentQt/StatusInfo.h>
#include <FluentQt/TextFields.h>
#include <QButtonGroup>
#include <QInputMethodEvent>
#include <QListWidget>
#include <QSignalSpy>
#include <QTextEdit>
#include <QtTest>

#include <algorithm>

using namespace manga;
using manga::ui::screen::editor::EditorViewModel;
using manga::ui::screen::editor::InspectorCandidateProjection;
using manga::ui::screen::editor::InspectorField;
using manga::ui::screen::editor::InspectorMode;
using manga::ui::screen::editor::InspectorState;
using manga::ui::screen::editor::RegionSelectionMode;
using manga::ui::screen::editor::SingleRegionInspectorState;
using manga::ui::widget::InspectorPanel;

namespace {

SingleRegionInspectorState makeSingleState() {
    SingleRegionInspectorState state;
    state.pageId = domain::PageId::generate();
    state.regionId = domain::RegionId::generate();
    state.sourceText = QStringLiteral("source text");
    state.sourceType = domain::TextSourceType::OcrAuto;
    state.sourceConfidence = 0.75;
    state.manualTranslation = QStringLiteral("manual override");
    state.hasManualTranslation = true;
    state.effectiveTranslation = QStringLiteral("manual override");
    state.candidates = {
        {QStringLiteral("c1"), QStringLiteral("candidate one"), 0.8, QStringLiteral("provider-a"), false},
        {QStringLiteral("c2"), QStringLiteral("candidate two"), 0.6, QStringLiteral("provider-b"), true}};
    state.geometry = domain::RectGeometry{20.0, 30.0, 80.0, 40.0};
    state.orderIndex = 1;
    state.fontFamily = QStringLiteral("Comic Sans");
    state.fontSizePt = 18.0;
    state.fontWeight = 700;
    state.letterSpacingEm = 0.05;
    state.orientation = domain::TextOrientation::Vertical;
    state.horizontalAlignment = domain::HorizontalAlignment::Left;
    state.verticalAlignment = domain::VerticalAlignment::Top;
    state.lineSpacing = 1.5;
    state.margins = domain::Margins{1.0, 2.0, 3.0, 4.0};
    state.textColorHex = QStringLiteral("#112233");
    state.strokeColorHex = QStringLiteral("#AABBCC");
    state.strokeWidthPx = 2.5;
    return state;
}

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

class InspectorPanelTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<domain::RegionId>();
        qRegisterMetaType<QVector<domain::RegionId>>();
        qRegisterMetaType<InspectorField>();
    }

    // State projection

    void emptySelectionYieldsNulloptInspector() {
        domain::TextRegion withText(domain::RegionId::generate(), makeGeometry(10.0, 10.0), 0);
        withText.updateSource([](domain::TextContent& source) { source.setOcrResult("hello", 0.9, "fake-ocr"); });
        withText.updateTranslation([](domain::TranslationEntry& translation) { translation.setModelResult("world"); });
        domain::TextRegion emptyRegion(domain::RegionId::generate(), makeGeometry(10.0, 110.0), 1);

        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.openProjectSession(makeSession({withText, emptyRegion}, 1));

        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::None);
        QVERIFY(std::holds_alternative<std::monostate>(viewModel.state().inspector));
    }

    void noSessionYieldsNulloptInspector() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::None);
        QVERIFY(std::holds_alternative<std::monostate>(viewModel.state().inspector));
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
        QVERIFY(std::holds_alternative<SingleRegionInspectorState>(viewModel.state().inspector));
        const auto& single = std::get<SingleRegionInspectorState>(viewModel.state().inspector);
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

        QVERIFY(std::holds_alternative<SingleRegionInspectorState>(viewModel.state().inspector));
        const auto& single = std::get<SingleRegionInspectorState>(viewModel.state().inspector);
        QVERIFY(!single.hasManualTranslation);
        QCOMPARE(single.effectiveTranslation, QStringLiteral("candidate one"));
    }

    void multiSelectionYieldsMultiInspector() {
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

        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::Multi);
        QVERIFY(std::holds_alternative<ui::screen::editor::MultiRegionInspectorState>(viewModel.state().inspector));
        const auto& multi = std::get<ui::screen::editor::MultiRegionInspectorState>(viewModel.state().inspector);
        QCOMPARE(multi.regionCount, 2);
        QCOMPARE(multi.fontFamily, QStringLiteral("Comic Sans"));
        QCOMPARE(multi.fontSizePt, 18.0);
    }

    void deletingSelectionFallsBackToNullopt() {
        domain::TextRegion region(domain::RegionId::generate(), makeGeometry(10.0, 10.0), 0);
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession({region}, 0);
        viewModel.openProjectSession(session);
        viewModel.selectRegion(regionAt(session, 0, 0), RegionSelectionMode::Replace);
        QVERIFY(std::holds_alternative<SingleRegionInspectorState>(viewModel.state().inspector));

        viewModel.deleteRegions({regionAt(session, 0, 0)});

        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::None);
        QVERIFY(std::holds_alternative<std::monostate>(viewModel.state().inspector));
    }

    void pageSwitchResetsInspectorToNullopt() {
        domain::TextRegion region(domain::RegionId::generate(), makeGeometry(10.0, 10.0), 0);
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession({region}, 1);
        viewModel.openProjectSession(session);
        viewModel.selectRegion(regionAt(session, 0, 0), RegionSelectionMode::Replace);
        QVERIFY(std::holds_alternative<SingleRegionInspectorState>(viewModel.state().inspector));

        viewModel.selectPage(pageAt(session, 1));

        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::None);
        QVERIFY(std::holds_alternative<std::monostate>(viewModel.state().inspector));
    }

    void closingSessionResetsInspector() {
        domain::TextRegion region(domain::RegionId::generate(), makeGeometry(10.0, 10.0), 0);
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession({region}, 0);
        viewModel.openProjectSession(session);
        viewModel.selectRegion(regionAt(session, 0, 0), RegionSelectionMode::Replace);
        QVERIFY(std::holds_alternative<SingleRegionInspectorState>(viewModel.state().inspector));

        viewModel.closeProjectSession();

        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::None);
        QVERIFY(std::holds_alternative<std::monostate>(viewModel.state().inspector));
    }

    // Panel and widget

    void unselectedStateDisablesContentHost() {
        InspectorPanel panel;
        panel.show();

        panel.setInspectorState(std::monostate{});

        QCOMPARE(panel.modeForTest(), InspectorMode::None);
        QVERIFY(panel.contentHostForTest() != nullptr);
        QVERIFY(!panel.contentHostForTest()->isEnabled());

        auto* title = panel.findChild<fluent::textfields::Label*>(QStringLiteral("inspectorHeaderTitle"));
        QVERIFY(title != nullptr);
        QCOMPARE(title->text(), QStringLiteral("属性"));

        auto* subtitle = panel.findChild<fluent::textfields::Label*>(QStringLiteral("inspectorHeaderSubtitle"));
        QVERIFY(subtitle != nullptr);
        QCOMPARE(subtitle->text(), QStringLiteral("未选中文字框"));
    }

    void singleStatePopulatesAllFields() {
        InspectorPanel panel;
        panel.show();
        const auto state = makeSingleState();
        panel.setInspectorState(state);

        QCOMPARE(panel.modeForTest(), InspectorMode::Single);
        QVERIFY(panel.contentHostForTest()->isEnabled());

        auto* title = panel.findChild<fluent::textfields::Label*>(QStringLiteral("inspectorHeaderTitle"));
        QVERIFY(title != nullptr);
        QCOMPARE(title->text(), QStringLiteral("文字框属性"));

        auto* subtitle = panel.findChild<fluent::textfields::Label*>(QStringLiteral("inspectorHeaderSubtitle"));
        QVERIFY(subtitle != nullptr);
        QVERIFY(subtitle->text().contains(QStringLiteral("序号 #2")));

        auto* sourceEdit = panel.findChild<fluent::textfields::TextEdit*>(QStringLiteral("inspectorSourceEdit"));
        QVERIFY(sourceEdit != nullptr);
        QCOMPARE(sourceEdit->toPlainText(), QStringLiteral("source text"));
        auto* translationEdit =
            panel.findChild<fluent::textfields::TextEdit*>(QStringLiteral("inspectorTranslationEdit"));
        QVERIFY(translationEdit != nullptr);
        QCOMPARE(translationEdit->toPlainText(), QStringLiteral("manual override"));

        auto* candidates = panel.findChild<QListWidget*>(QStringLiteral("inspectorCandidatesList"));
        QVERIFY(candidates != nullptr);
        QCOMPARE(candidates->count(), 2);
        QCOMPARE(candidates->item(0)->data(Qt::UserRole).toString(), QStringLiteral("c1"));
        QCOMPARE(candidates->currentRow(), 1);

        auto* fontFamily = panel.findChild<fluent::basicinput::ComboBox*>(QStringLiteral("inspectorFontFamily"));
        QVERIFY(fontFamily != nullptr);
        QCOMPARE(fontFamily->currentText(), QStringLiteral("Comic Sans"));
        auto* fontWeight = panel.findChild<fluent::basicinput::ComboBox*>(QStringLiteral("inspectorFontWeight"));
        QVERIFY(fontWeight != nullptr);
        QCOMPARE(fontWeight->currentText(), QStringLiteral("700"));
        auto* fontSize = panel.findChild<fluent::textfields::NumberBox*>(QStringLiteral("inspectorFontSize"));
        QVERIFY(fontSize != nullptr);
        QCOMPARE(fontSize->value(), 18.0);
        auto* orientation = panel.findChild<fluent::basicinput::ComboBox*>(QStringLiteral("inspectorOrientation"));
        QVERIFY(orientation != nullptr);
        QCOMPARE(orientation->currentIndex(), static_cast<int>(domain::TextOrientation::Vertical));
        QVERIFY(panel.hAlignGroupForTest() != nullptr);
        QCOMPARE(panel.hAlignGroupForTest()->checkedId(), static_cast<int>(domain::HorizontalAlignment::Left));

        auto* hexEdit = panel.findChild<fluent::textfields::LineEdit*>(QStringLiteral("inspectorTextColorHex"));
        QVERIFY(hexEdit != nullptr);
        QCOMPARE(hexEdit->text(), QStringLiteral("#112233"));
    }

    void multiStateShowsTypesettingAndHidesTranslation() {
        InspectorPanel panel;
        panel.show();
        ui::screen::editor::MultiRegionInspectorState state;
        state.pageId = domain::PageId::generate();
        state.regionIds = {domain::RegionId::generate(), domain::RegionId::generate()};
        state.regionCount = 2;
        state.fontSizePt = 24.0;
        state.fontFamily = QStringLiteral("Comic Sans");

        panel.setInspectorState(state);

        QCOMPARE(panel.modeForTest(), InspectorMode::Multi);
        QVERIFY(panel.contentHostForTest()->isEnabled());

        auto* title = panel.findChild<fluent::textfields::Label*>(QStringLiteral("inspectorHeaderTitle"));
        QVERIFY(title != nullptr);
        QCOMPARE(title->text(), QStringLiteral("文字框属性"));

        auto* subtitle = panel.findChild<fluent::textfields::Label*>(QStringLiteral("inspectorHeaderSubtitle"));
        QVERIFY(subtitle != nullptr);
        QCOMPARE(subtitle->text(), QStringLiteral("已选中 2 个文字框"));

        auto* translationExpander =
            panel.findChild<fluent::layout::Expander*>(QStringLiteral("inspectorTranslationExpander"));
        QVERIFY(translationExpander != nullptr);
        QVERIFY(!translationExpander->isVisible());

        auto* typesetExpander =
            panel.findChild<fluent::layout::Expander*>(QStringLiteral("inspectorTypesettingExpander"));
        QVERIFY(typesetExpander != nullptr);
        QVERIFY(typesetExpander->isVisible());

        auto* fontSize = panel.findChild<fluent::textfields::NumberBox*>(QStringLiteral("inspectorFontSize"));
        QVERIFY(fontSize != nullptr);
        QCOMPARE(fontSize->value(), 24.0);
    }

    void switchingFromSingleToUnselectedClearsAndDisables() {
        InspectorPanel panel;
        panel.show();
        panel.setInspectorState(makeSingleState());
        QCOMPARE(panel.modeForTest(), InspectorMode::Single);
        QVERIFY(panel.contentHostForTest()->isEnabled());

        panel.setInspectorState(std::monostate{});
        QCOMPARE(panel.modeForTest(), InspectorMode::None);
        QVERIFY(!panel.contentHostForTest()->isEnabled());

        auto* sourceEdit = panel.findChild<fluent::textfields::TextEdit*>(QStringLiteral("inspectorSourceEdit"));
        QVERIFY(sourceEdit != nullptr);
        QCOMPARE(sourceEdit->toPlainText(), QString());

        auto* translationEdit =
            panel.findChild<fluent::textfields::TextEdit*>(QStringLiteral("inspectorTranslationEdit"));
        QVERIFY(translationEdit != nullptr);
        QCOMPARE(translationEdit->toPlainText(), QString());
    }

    void unavailableTranslationShowsActionableInfoBar() {
        // TODO: translation InfoBar removed from InspectorPanel
    }

    void translationFailureShowsProcessingMessage() {
        // TODO: translation InfoBar removed from InspectorPanel
    }

    void emptyCandidateListStaysCollapsed() {
        InspectorPanel panel;
        panel.show();
        auto state = makeSingleState();
        state.candidates.clear();
        panel.setInspectorState(InspectorState{state});

        auto* candidates = panel.findChild<fluent::layout::Expander*>(QStringLiteral("inspectorCandidatesExpander"));
        QVERIFY(candidates != nullptr);
        QVERIFY(!candidates->isVisible());
    }

    void marginValidationErrorClearsOnRegionSwitch() {
        InspectorPanel panel;
        panel.show();
        panel.setInspectorState(InspectorState{makeSingleState()});

        QSignalSpy spy(&panel, &InspectorPanel::fieldEditCommitted);
        auto* marginBox = panel.findChild<fluent::textfields::NumberBox*>(QStringLiteral("inspectorMarginTop"));
        auto* marginField = panel.findChild<fluent::layout::Field*>(QStringLiteral("inspectorMarginTopField"));
        QVERIFY(marginBox != nullptr);
        QVERIFY(marginField != nullptr);

        marginBox->setText(QStringLiteral("abc"));
        QTest::keyClick(marginBox, Qt::Key_Return);
        QCOMPARE(spy.size(), 0);
        QCOMPARE(marginField->validationState(), fluent::layout::Field::ValidationState::Error);

        // Rendering another region must not leave the stale error behind.
        panel.setInspectorState(InspectorState{makeSingleState()});
        QCOMPARE(marginField->validationState(), fluent::layout::Field::ValidationState::None);
        QVERIFY(marginField->validationMessage().isEmpty());
    }

    void numericEditEmitsFieldIntentWithStableIds() {
        InspectorPanel panel;
        panel.show();
        const auto state = makeSingleState();
        panel.setInspectorState(InspectorState{state});

        QSignalSpy spy(&panel, &InspectorPanel::fieldEditCommitted);
        auto* fontSize = panel.findChild<fluent::textfields::NumberBox*>(QStringLiteral("inspectorFontSize"));
        QVERIFY(fontSize != nullptr);
        fontSize->setValue(20.0);

        QCOMPARE(spy.size(), 1);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).value<domain::PageId>(), state.pageId);
        QCOMPARE(args.at(1).value<QVector<domain::RegionId>>(), QVector<domain::RegionId>({state.regionId}));
        QCOMPARE(args.at(2).value<InspectorField>(), InspectorField::FontSizePt);
        QCOMPARE(args.at(3).toDouble(), 20.0);
    }

    void invalidNumericKeepsTextShowsErrorAndSkipsIntent() {
        InspectorPanel panel;
        panel.show();
        panel.setInspectorState(InspectorState{makeSingleState()});

        QSignalSpy spy(&panel, &InspectorPanel::fieldEditCommitted);
        auto* fontSize = panel.findChild<fluent::textfields::NumberBox*>(QStringLiteral("inspectorFontSize"));
        auto* fontSizeField = panel.findChild<fluent::layout::Field*>(QStringLiteral("inspectorFontSizeField"));
        QVERIFY(fontSize != nullptr);
        QVERIFY(fontSizeField != nullptr);

        fontSize->setText(QStringLiteral("abc"));
        QTest::keyClick(fontSize, Qt::Key_Return);

        QCOMPARE(spy.size(), 0);
        QCOMPARE(fontSize->text(), QStringLiteral("abc"));
        QCOMPARE(fontSizeField->validationState(), fluent::layout::Field::ValidationState::Error);
        QVERIFY(!fontSizeField->validationMessage().isEmpty());

        // A valid commit clears the error and emits again.
        fontSize->setText(QStringLiteral("24"));
        QTest::keyClick(fontSize, Qt::Key_Return);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(fontSizeField->validationState(), fluent::layout::Field::ValidationState::None);
    }

    void invalidHexKeepsTextShowsErrorAndSkipsIntent() {
        InspectorPanel panel;
        panel.show();
        panel.setInspectorState(InspectorState{makeSingleState()});

        QSignalSpy spy(&panel, &InspectorPanel::fieldEditCommitted);
        auto* hexEdit = panel.findChild<fluent::textfields::LineEdit*>(QStringLiteral("inspectorTextColorHex"));
        auto* colorField = panel.findChild<fluent::layout::Field*>(QStringLiteral("inspectorTextColorField"));
        QVERIFY(hexEdit != nullptr);
        QVERIFY(colorField != nullptr);

        hexEdit->setText(QStringLiteral("ZZZZZZ"));
        QTest::keyClick(hexEdit, Qt::Key_Return);

        QCOMPARE(spy.size(), 0);
        QCOMPARE(hexEdit->text(), QStringLiteral("ZZZZZZ"));
        QCOMPARE(colorField->validationState(), fluent::layout::Field::ValidationState::Error);

        hexEdit->setText(QStringLiteral("#a1b2c3"));
        QTest::keyClick(hexEdit, Qt::Key_Return);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().at(2).value<InspectorField>(), InspectorField::TextColorHex);
        QCOMPARE(spy.first().at(3).toString(), QStringLiteral("#A1B2C3"));
        QCOMPARE(colorField->validationState(), fluent::layout::Field::ValidationState::None);
    }

    void candidateAdoptionEmitsStableIds() {
        InspectorPanel panel;
        panel.show();
        const auto state = makeSingleState();
        panel.setInspectorState(InspectorState{state});

        QSignalSpy spy(&panel, &InspectorPanel::candidateAdoptionRequested);
        auto* candidates = panel.findChild<QListWidget*>(QStringLiteral("inspectorCandidatesList"));
        auto* adopt = panel.findChild<fluent::basicinput::Button*>(QStringLiteral("inspectorAdoptCandidate"));
        QVERIFY(candidates != nullptr);
        QVERIFY(adopt != nullptr);

        candidates->setCurrentRow(0);
        QVERIFY(adopt->isEnabled());
        QTest::mouseClick(adopt, Qt::LeftButton);

        QCOMPARE(spy.size(), 1);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).value<domain::PageId>(), state.pageId);
        QCOMPARE(args.at(1).value<domain::RegionId>(), state.regionId);
        QCOMPARE(args.at(2).toString(), QStringLiteral("c1"));
    }

    void lockedSingleRegionDisablesEditingButAllowsViewing() {
        InspectorPanel panel;
        panel.show();
        auto state = makeSingleState();
        state.isLocked = true;
        panel.setInspectorState(InspectorState{state});

        auto* sourceEdit = panel.findChild<fluent::textfields::TextEdit*>(QStringLiteral("inspectorSourceEdit"));
        auto* translationEdit =
            panel.findChild<fluent::textfields::TextEdit*>(QStringLiteral("inspectorTranslationEdit"));
        auto* fontSize = panel.findChild<fluent::textfields::NumberBox*>(QStringLiteral("inspectorFontSize"));
        QVERIFY(sourceEdit != nullptr);
        QVERIFY(translationEdit != nullptr);
        QVERIFY(fontSize != nullptr);
        QVERIFY(sourceEdit->isReadOnly());
        QVERIFY(translationEdit->isReadOnly());
        QVERIFY(!fontSize->isEnabled());
        // Values remain visible.
        QCOMPARE(sourceEdit->toPlainText(), QStringLiteral("source text"));
    }

    void textControlsDisableInternalUndo() {
        InspectorPanel panel;
        panel.show();
        panel.setInspectorState(InspectorState{makeSingleState()});

        for (const auto* name : {"inspectorSourceEdit", "inspectorTranslationEdit"}) {
            auto* edit = panel.findChild<fluent::textfields::TextEdit*>(QString::fromLatin1(name));
            QVERIFY(edit != nullptr);
            auto* inner = edit->findChild<QTextEdit*>();
            QVERIFY(inner != nullptr);
            QVERIFY(!inner->isUndoRedoEnabled());
        }
    }

    void imePreeditDoesNotEmitIntentButCommitEmitsOnce() {
        InspectorPanel panel;
        panel.show();
        const auto state = makeSingleState();
        panel.setInspectorState(InspectorState{state});

        auto* edit = panel.findChild<fluent::textfields::TextEdit*>(QStringLiteral("inspectorTranslationEdit"));
        QVERIFY(edit != nullptr);
        auto* inner = edit->findChild<QTextEdit*>();
        QVERIFY(inner != nullptr);
        inner->setFocus();

        QSignalSpy spy(&panel, &InspectorPanel::fieldEditCommitted);

        // Preedit composition only lives in the control; it must not produce an intent.
        QInputMethodEvent preeditEvent(QStringLiteral("にほ"), {});
        QCoreApplication::sendEvent(inner, &preeditEvent);
        QCOMPARE(spy.size(), 0);

        // One commit string produces exactly one committed intent.
        QInputMethodEvent commitEvent;
        commitEvent.setCommitString(QStringLiteral("日本語"), 0, 0);
        QCoreApplication::sendEvent(inner, &commitEvent);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().at(2).value<InspectorField>(), InspectorField::ManualTranslation);
        QVERIFY(spy.first().at(3).toString().contains(QStringLiteral("日本語")));
    }

    void stateEchoKeepsCursorAndSelection() {
        InspectorPanel panel;
        panel.show();
        auto state = makeSingleState();
        panel.setInspectorState(InspectorState{state});

        auto* edit = panel.findChild<fluent::textfields::TextEdit*>(QStringLiteral("inspectorTranslationEdit"));
        QVERIFY(edit != nullptr);
        auto* inner = edit->findChild<QTextEdit*>();
        QVERIFY(inner != nullptr);

        QTextCursor cursor = inner->textCursor();
        cursor.setPosition(2);
        cursor.setPosition(6, QTextCursor::KeepAnchor);
        inner->setTextCursor(cursor);

        // Replaying the same text (the control's own committed value) must not touch the cursor.
        panel.setInspectorState(InspectorState{state});
        QCOMPARE(inner->textCursor().position(), 6);
        QCOMPARE(inner->textCursor().anchor(), 2);

        // Text that genuinely differs, such as an undo restoring "Undo", rebuilds a clamped valid cursor.
        state.manualTranslation = QStringLiteral("abc");
        panel.setInspectorState(InspectorState{state});
        QCOMPARE(edit->toPlainText(), QStringLiteral("abc"));
        QVERIFY(inner->textCursor().position() <= 3);
    }

    void focusOutRequestsSessionSeal() {
        InspectorPanel panel;
        panel.show();
        panel.setInspectorState(InspectorState{makeSingleState()});

        auto* edit = panel.findChild<fluent::textfields::TextEdit*>(QStringLiteral("inspectorTranslationEdit"));
        QVERIFY(edit != nullptr);
        auto* inner = edit->findChild<QTextEdit*>();
        QVERIFY(inner != nullptr);

        QSignalSpy spy(&panel, &InspectorPanel::textEditingSessionFinished);
        QEvent focusOut(QEvent::FocusOut);
        QCoreApplication::sendEvent(inner, &focusOut);
        QCOMPARE(spy.size(), 1);
    }
};

QTEST_MAIN(InspectorPanelTest)
#include "InspectorPanelTest.moc"
