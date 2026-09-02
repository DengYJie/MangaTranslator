# Koharu-Style Text Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current drag-only Create Region interaction with a Koharu-style Text tool: click creates point text, drag creates a paragraph text box, the new region is selected, and its translation editor receives focus.

**Architecture:** Keep MangaTranslator's existing `TextRegion`, undo stack, Inspector, and axis-aligned `RectGeometry`. Treat point/paragraph as creation intents rather than adding a persisted layer-kind abstraction: both produce a normal `TextRegion`, but point text receives a generated frame and start alignment while paragraph text keeps the dragged frame. Reuse the existing canvas draft rectangle and region move/resize behavior; do not import Koharu's React layer/content architecture.

**Tech Stack:** C++23, Qt 6.11 Widgets/Graphics View, FluentQt public C++ API, Qt Test, CMake presets.

**Spec:** `C:/Users/Luren/Documents/antigravity/koharu/docs/workflow/canvas-basics.md`, with executable behavior taken from `packages/koharu/components/editor/CanvasWorkspace.tsx`, `packages/koharu/lib/geometry.ts`, `crates/koharu-app/src/commands/project.rs`, and `packages/koharu/tests/components/workspace.test.tsx` in the same Koharu checkout.

## Global Constraints

- Preserve MVVM/UDF: `MangaCanvas` emits user intent, `EditorPage` wires it, `EditorViewModel` validates and mutates through undo commands.
- Scene and persisted geometry remain page source pixels; zoom changes presentation and the approximately four-view-pixel click/drag threshold only.
- Rename the existing `CreateRegion` tool to `Text`; do not leave aliases, duplicate tools, compatibility shims, or dead signal names.
- Point text defaults to at most `320 x 120` source pixels, stays inside the page, enables `TextLayout::autoFit`, and starts at left/top alignment.
- Paragraph text uses the normalized dragged frame, stays inside the page, enables `TextLayout::autoFit`, and retains the normal center/middle alignment defaults.
- A successful creation is one undoable command, selects the new region, and focuses the Inspector translation editor.
- Invalid or out-of-page creation intents do not push undo history and do not change selection.
- Editing a fixed font size disables Auto Fit; explicitly enabling Auto Fit leaves `minFontSizePt` and `maxFontSizePt` unchanged.
- Rotation is excluded: `domain::RectGeometry` is axis-aligned and `TextLayout::rotationDeg` is explicitly fixed at `0.0` in V0.1. Do not add polygon geometry in this change.
- Koharu's edge-only resize hit zones are excluded. Existing eight-handle move/resize, multi-move, keyboard nudge, delete, undo, and redo behavior remains authoritative after creation.
- Use FluentQt public controls only, Qt non-keyword macros, and existing project build/test scripts.
- Preserve unrelated worktree changes. Do not commit unless the user separately authorizes Git actions.

---

### Task 1: Cut Over the Canvas Tool Contract

**Files:**
- Modify: `src/ui/manga/RegionInteraction.h:14-22`
- Modify: `src/ui/manga/MangaCanvas.h:93-157, 218-234`
- Modify: `src/ui/manga/MangaCanvas.cpp:350-378, 751-1045`
- Modify: `tests/unit/ui/manga_canvas_test.cpp:267-333, 370-410`

**Interfaces:**
- Consumes: existing source-pixel scene coordinates, `domain::RectGeometry`, and the existing draft `QGraphicsRectItem`.
- Produces: `canvas::Tool::Text`, `MangaCanvas::pointTextCreateCommitted(QPointF)`, and `MangaCanvas::textBoxCreateCommitted(SourceRectGeometry)`.

- [ ] **Step 1: Write failing canvas tests for click, drag, normalization, bounds, and cancellation**

Replace the drag-only creation tests with behavior-focused tests. Compute expected anchors with `mapToScene()` so tests do not depend on fit-to-page transforms:

```cpp
void textClickEmitsPointIntent() {
    MangaCanvas canvas;
    canvas.resize(800, 600);
    canvas.show();
    QImage image(400, 400, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    canvas.setPageImage(image);
    canvas.setPageBounds(ImageSize{400, 400});
    canvas.setTool(Tool::Text);
    QCoreApplication::processEvents();

    QSignalSpy pointSpy(&canvas, &MangaCanvas::pointTextCreateCommitted);
    QSignalSpy boxSpy(&canvas, &MangaCanvas::textBoxCreateCommitted);
    const QPoint viewPoint = canvas.mapFromScene(QPointF{100.0, 100.0});
    const QPointF expectedAnchor = canvas.mapToScene(viewPoint);

    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, {}, viewPoint);

    QCOMPARE(pointSpy.size(), 1);
    QCOMPARE(boxSpy.size(), 0);
    const QPointF actual = pointSpy.constFirst().constFirst().toPointF();
    QVERIFY(QLineF(actual, expectedAnchor).length() < 0.01);
}

void textDragEmitsNormalizedParagraphFrame() {
    MangaCanvas canvas;
    canvas.resize(800, 600);
    canvas.show();
    QImage image(400, 400, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    canvas.setPageImage(image);
    canvas.setPageBounds(ImageSize{400, 400});
    canvas.setTool(Tool::Text);
    QCoreApplication::processEvents();

    QSignalSpy pointSpy(&canvas, &MangaCanvas::pointTextCreateCommitted);
    QSignalSpy boxSpy(&canvas, &MangaCanvas::textBoxCreateCommitted);
    const QPoint start = canvas.mapFromScene(QPointF{240.0, 220.0});
    const QPoint end = canvas.mapFromScene(QPointF{120.0, 110.0});
    const QPointF sourceStart = canvas.mapToScene(start);
    const QPointF sourceEnd = canvas.mapToScene(end);

    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, start);
    QTest::mouseMove(canvas.viewport(), end);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, end);

    QCOMPARE(pointSpy.size(), 0);
    QCOMPARE(boxSpy.size(), 1);
    const auto frame = boxSpy.constFirst().constFirst().value<RectGeometry>();
    QVERIFY(qAbs(frame.x - qMin(sourceStart.x(), sourceEnd.x())) < 0.01);
    QVERIFY(qAbs(frame.y - qMin(sourceStart.y(), sourceEnd.y())) < 0.01);
    QVERIFY(qAbs(frame.width - qAbs(sourceEnd.x() - sourceStart.x())) < 0.01);
    QVERIFY(qAbs(frame.height - qAbs(sourceEnd.y() - sourceStart.y())) < 0.01);
}
```

Add two more focused cases:

```cpp
void textGestureClampsParagraphToPageBounds();
void escapeCancelsTextGestureWithoutCommit();
void diagonalMovementBelowEachAxisThresholdRemainsPointText();
void horizontalDragCreatesMinimumHeightParagraphFrame();
```

The bounds test starts inside the page and drags into the navigation margin; its emitted frame must satisfy `frame.isWithinBounds(ImageSize{400, 400})`. The Escape test presses and moves, sends `Qt::Key_Escape`, then releases and expects both creation spies to remain empty and `gestureCancelled` to fire once. A `3 x 3` view-pixel movement remains point text because Koharu classifies each axis independently; a horizontal movement of at least four view pixels creates a paragraph frame whose height is `RectGeometry::kMinDimension`.

- [ ] **Step 2: Run the canvas test and verify the new contract fails**

Run:

```powershell
pwsh -File tools/build/build.ps1 -Preset local-test -Target manga_tests_ui_manga_canvas -LogFile build/local-test/text-tool-canvas-build.log
pwsh -File tools/build/test.ps1 -Preset local-test -Regex '^manga_tests_ui_manga_canvas$' -LogFile build/local-test/text-tool-canvas-test.log
```

Expected: compilation fails because `Tool::Text`, `pointTextCreateCommitted`, and `textBoxCreateCommitted` do not exist.

- [ ] **Step 3: Rename the canvas tool and replace the drag-only signal**

Perform a clean cutover in `RegionInteraction.h`:

```cpp
enum class Tool {
    Select,
    Pan,
    Text,
    Remove,
    Brush,
    Eraser
};

inline constexpr double kTextGestureThresholdViewPx = 4.0;
```

In `MangaCanvas.h`, replace `regionCreateCommitted` with two explicit intents:

```cpp
/// Emitted when a Text-tool click requests point text at a source-pixel anchor.
void pointTextCreateCommitted(QPointF anchor);

/// Emitted when a Text-tool drag requests paragraph text in a source-pixel frame.
void textBoxCreateCommitted(SourceRectGeometry geometry);
```

Rename `m_creationBox` to `m_textDraftBox` and update its comments. Keep the same scene-owned item, dashed accent pen, translucent accent fill, and `kZToolCreation` placement.

- [ ] **Step 4: Implement the Text pointer gesture**

In `MangaCanvas.cpp`, add file-local helpers that reject presses outside the page and clamp drag endpoints:

```cpp
[[nodiscard]] bool isInsidePage(const QPointF& point, const domain::ImageSize& bounds) noexcept {
    return bounds.isValid() && point.x() >= 0.0 && point.y() >= 0.0 &&
           point.x() < static_cast<double>(bounds.width) &&
           point.y() < static_cast<double>(bounds.height);
}

[[nodiscard]] QPointF clampToPage(const QPointF& point, const domain::ImageSize& bounds) noexcept {
    return {std::clamp(point.x(), 0.0, static_cast<double>(bounds.width)),
            std::clamp(point.y(), 0.0, static_cast<double>(bounds.height))};
}

[[nodiscard]] domain::RectGeometry textDraftFrame(QPointF start,
                                                  QPointF current,
                                                  const domain::ImageSize& bounds) noexcept {
    current = clampToPage(current, bounds);
    double x = std::min(start.x(), current.x());
    double y = std::min(start.y(), current.y());
    const double width = std::min(static_cast<double>(bounds.width),
                                  std::max(domain::RectGeometry::kMinDimension,
                                           std::abs(current.x() - start.x())));
    const double height = std::min(static_cast<double>(bounds.height),
                                   std::max(domain::RectGeometry::kMinDimension,
                                            std::abs(current.y() - start.y())));
    x = std::min(x, static_cast<double>(bounds.width) - width);
    y = std::min(y, static_cast<double>(bounds.height) - height);
    return {x, y, width, height};
}
```

Implement these state transitions:

```text
mousePress(Text):
    reject if the source point is outside the page
    store the source anchor in m_gesture.targets[0].startGeometry.x/y
    show a zero-size draft rectangle

mouseMove(Text):
    update the draft on every move through textDraftFrame()
    classify as paragraph when either absolute view delta reaches kTextGestureThresholdViewPx
    keep a 2-pixel source minimum on the non-dragged axis

mouseRelease(Text):
    recompute the final delta and frame from the release position
    both view-axis deltas below kTextGestureThresholdViewPx -> emit pointTextCreateCommitted(anchor)
    either view-axis delta reaches kTextGestureThresholdViewPx -> emit textBoxCreateCommitted(frame)
    always hide draft and clear m_gesture
```

Do not use the existing Euclidean `exceedsDragThreshold()` result for Text classification: Koharu checks width and height independently. Continue using the existing helper unchanged for move and resize gestures.

`setTool()`, `cancelGesture()`, focus loss, Space temporary pan, and Escape must all hide the text draft. Do not preserve a `CreateRegion` switch branch.

- [ ] **Step 5: Run the focused canvas test**

Run the two PowerShell commands from Step 2 again.

Expected: `manga_tests_ui_manga_canvas` passes; click emits only point intent, drag emits only paragraph intent, reverse drags normalize, out-of-page motion clamps, and Escape commits nothing.

---

### Task 2: Create Point and Paragraph Text Through Undo History

**Files:**
- Modify: `src/ui/command/RegionCommands.h:34-55`
- Modify: `src/ui/command/RegionCommands.cpp:5-38`
- Modify: `src/ui/screen/editor/EditorState.h:44-52`
- Modify: `src/ui/screen/editor/EditorViewModel.h:100-128`
- Modify: `src/ui/screen/editor/EditorViewModel.cpp:30-90, 1220-1248`
- Modify: `src/ui/screen/editor/InspectorState.h:19-36, 51-109`
- Modify: `tests/unit/ui/editor_view_model_test.cpp:585-713`

**Interfaces:**
- Consumes: `QPointF` point intent and normalized `domain::RectGeometry` paragraph intent from Task 1.
- Produces: `EditorViewModel::createPointText(QPointF)`, `EditorViewModel::createTextBox(const domain::RectGeometry&)`, and `EditorViewModel::textRegionCreated(domain::RegionId)`.

- [ ] **Step 1: Write failing ViewModel tests for point defaults and paragraph preservation**

Replace `createRegionPushesCommandAndFlipsCanUndo` with explicit text creation tests:

```cpp
void createPointTextUsesKoharuFrameAndSelectsRegion() {
    Fixture f;
    EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
    viewModel.openProjectSession(makeSession(1, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
    QSignalSpy createdSpy(&viewModel, &EditorViewModel::textRegionCreated);

    viewModel.createPointText(QPointF{100.0, 200.0});

    QCOMPARE(createdSpy.size(), 1);
    QCOMPARE(viewModel.state().regions.size(), 1);
    QCOMPARE(viewModel.state().regions.constFirst().geometry,
             (domain::RectGeometry{100.0, 200.0, 320.0, 120.0}));
    QCOMPARE(viewModel.state().selectedRegionIds,
             QVector<domain::RegionId>{viewModel.state().regions.constFirst().id});
    QCOMPARE(viewModel.state().inspectorMode, InspectorMode::Single);
    const auto& single = std::get<SingleRegionInspectorState>(viewModel.state().inspector);
    QVERIFY(single.autoFit);
    QCOMPARE(single.horizontalAlignment, domain::HorizontalAlignment::Left);
    QCOMPARE(single.verticalAlignment, domain::VerticalAlignment::Top);
    QCOMPARE(viewModel.state().saveState.undoText, QStringLiteral("Create Text"));
}

void createTextBoxPreservesDraggedFrameAndEnablesAutoFit() {
    Fixture f;
    EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
    viewModel.openProjectSession(makeSession(1, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
    const domain::RectGeometry frame{40.0, 60.0, 180.0, 90.0};

    viewModel.createTextBox(frame);

    QCOMPARE(viewModel.state().regions.constFirst().geometry, frame);
    const auto& single = std::get<SingleRegionInspectorState>(viewModel.state().inspector);
    QVERIFY(single.autoFit);
    QCOMPARE(single.horizontalAlignment, domain::HorizontalAlignment::Center);
    QCOMPARE(single.verticalAlignment, domain::VerticalAlignment::Middle);
}
```

Add boundary and failure coverage:

```cpp
void pointTextNearBottomRightStaysInsidePageAndMeetsDomainMinimum();
void pointTextOutsidePageDoesNotChangeHistoryOrSelection();
void invalidTextBoxDoesNotChangeHistoryOrSelection();
void undoRedoPointTextKeepsIdentityLayoutAndSelectionInvariant();
```

For a `1200 x 1800` page and anchor `{1199.5, 1799.5}`, expect a `2 x 2` frame shifted to `{1198.0, 1798.0}` so it satisfies `RectGeometry::kMinDimension` without crossing page bounds.

- [ ] **Step 2: Run the ViewModel test and verify it fails**

Run:

```powershell
pwsh -File tools/build/build.ps1 -Preset local-test -Target manga_tests_ui_editor_view_model -LogFile build/local-test/text-tool-view-model-build.log
pwsh -File tools/build/test.ps1 -Preset local-test -Regex '^manga_tests_ui_editor_view_model$' -LogFile build/local-test/text-tool-view-model-test.log
```

Expected: compilation fails because the new creation methods, signal, and `autoFit` Inspector projection do not exist.

- [ ] **Step 3: Rename the editor tool and command without compatibility aliases**

Change the editor-facing enum to:

```cpp
enum class CanvasTool {
    Select,
    Pan,
    Text,
    Remove,
    Brush,
    Eraser
};
```

Rename `CreateRegionCommand` to `CreateTextRegionCommand` and change its undo label to `QStringLiteral("Create Text")`. Give its constructor an initial layout:

```cpp
CreateTextRegionCommand(EditorCommandContext* context,
                        domain::PageId pageId,
                        domain::RegionId regionId,
                        domain::RectGeometry geometry,
                        domain::TextLayout initialLayout,
                        int orderIndex);
```

Store `domain::TextLayout m_initialLayout`. On every `redo()`, construct the region, call `region.setLayout(m_initialLayout)`, then insert it. Undo continues removing the same generated `RegionId`; redo keeps the same identity and layout.

- [ ] **Step 4: Add point-frame construction and the shared creation path**

In `EditorViewModel.cpp`, use one file-local helper:

```cpp
[[nodiscard]] std::optional<domain::RectGeometry>
pointTextFrame(QPointF anchor, const domain::ImageSize& pageSize) noexcept {
    if (!pageSize.isValid() || anchor.x() < 0.0 || anchor.y() < 0.0 ||
        anchor.x() >= pageSize.width || anchor.y() >= pageSize.height ||
        pageSize.width < domain::RectGeometry::kMinDimension ||
        pageSize.height < domain::RectGeometry::kMinDimension) {
        return std::nullopt;
    }

    const double width = std::min(320.0,
                                  std::max(domain::RectGeometry::kMinDimension,
                                           static_cast<double>(pageSize.width) - anchor.x()));
    const double height = std::min(120.0,
                                   std::max(domain::RectGeometry::kMinDimension,
                                            static_cast<double>(pageSize.height) - anchor.y()));
    const double x = std::min(anchor.x(), static_cast<double>(pageSize.width) - width);
    const double y = std::min(anchor.y(), static_cast<double>(pageSize.height) - height);
    return domain::RectGeometry{x, y, width, height};
}
```

Expose:

```cpp
void createPointText(QPointF anchor);
void createTextBox(const domain::RectGeometry& geometry);

Q_SIGNALS:
    void textRegionCreated(domain::RegionId regionId);
```

Both public methods build a layout with `autoFit = true` and call one private helper:

```cpp
void createTextRegion(const domain::RectGeometry& geometry, domain::TextLayout initialLayout);
```

For point text set:

```cpp
layout.autoFit = true;
layout.alignment = {domain::HorizontalAlignment::Left, domain::VerticalAlignment::Top};
```

For paragraph text set only:

```cpp
layout.autoFit = true;
```

The private helper must:

1. Validate session, page identity, finite/valid geometry, page containment, and the domain `2 x 2` minimum.
2. Generate the `RegionId` before constructing the command.
3. Seal the active text edit session.
4. Push exactly one `CreateTextRegionCommand`.
5. Stop without selection or signal changes if the command faults.
6. Set `selectedRegionIds` and `primaryRegionId` to the new ID and `inspectorMode` to `Single`.
7. Rebuild the current page projection and Inspector projection.
8. Refresh undo state.
9. Emit `textRegionCreated(regionId)` after State is coherent.

Delete `EditorViewModel::createRegion`; migrate every local caller and test.

- [ ] **Step 5: Project Auto Fit into Inspector state and support edits**

Extend the value projections:

```cpp
struct SingleRegionInspectorState {
    // existing fields
    bool autoFit{false};
};

struct MultiRegionInspectorState {
    // existing fields
    std::optional<bool> autoFit;
};
```

Add `InspectorField::AutoFit`. Populate the single value directly from `region.layout().autoFit`; compute the multi value with the same common-value rule used by font size and alignment.

In `applyStyleFieldValue`:

```cpp
case InspectorField::FontSizePt:
    layout.fontSizePt = value.toDouble();
    layout.autoFit = false;
    return true;
case InspectorField::AutoFit:
    layout.autoFit = value.toBool();
    return true;
```

This makes fixed-size edits match Koharu's behavior and keeps Auto Fit as an undoable `RegionPropertyCommand` edit.

- [ ] **Step 6: Run focused ViewModel tests**

Run the two PowerShell commands from Step 2 again.

Expected: all editor ViewModel tests pass; point/paragraph creation is undoable, invalid intents are no-ops, the new region is selected, and Inspector state exposes the initial Auto Fit and alignment values.

---

### Task 3: Wire the Text Tool and Inspector Focus

**Files:**
- Modify: `src/ui/screen/editor/EditorPage.cpp:179-222, 405-449, 801-847, 873-924`
- Modify: `src/ui/screen/editor/EditorPage.h:1-180` only if a test observation hook is needed
- Modify: `tests/unit/ui/editor_page_test.cpp:123-223`

**Interfaces:**
- Consumes: Task 1 canvas intents and Task 2 ViewModel creation methods/success signal.
- Produces: one Text tool button named `canvasTextButton`, shortcut `T`, and post-create translation-editor focus.

- [ ] **Step 1: Write failing EditorPage integration tests**

Add tests that assert the clean tool cutover and focus flow:

```cpp
void textToolButtonAndShortcutSelectTextTool() {
    test::FakeThumbnailStore thumbnails;
    test::FakePageImageLoader images;
    application::OperationRegistry registry;
    EditorViewModel viewModel(&thumbnails, &images, &registry);
    EditorPage page(&viewModel);
    page.show();

    auto* textButton = page.findChild<fluent::basicinput::Button*>(QStringLiteral("canvasTextButton"));
    QVERIFY(textButton != nullptr);
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("canvasCreateRegionButton")) == nullptr);

    QTest::mouseClick(textButton, Qt::LeftButton);
    QCOMPARE(viewModel.state().activeTool, CanvasTool::Text);
    QCOMPARE(page.canvasForTest()->tool(), ui::manga::CanvasTool::Text);

    viewModel.setActiveTool(CanvasTool::Select);
    page.setFocus();
    QTest::keyClick(&page, Qt::Key_T);
    QCOMPARE(viewModel.state().activeTool, CanvasTool::Text);
}
```

Add `textCreationSelectsRegionAndFocusesTranslationEditor()`: open one page, click the Text tool, send a canvas click, wait for one region, and verify `QApplication::focusWidget()` is the Inspector translation editor or its child. Keep the wait queued because `EditorPage` intentionally focuses after State render.

- [ ] **Step 2: Run the EditorPage test and verify it fails**

Run:

```powershell
pwsh -File tools/build/build.ps1 -Preset local-test -Target manga_tests_ui_editor_page -LogFile build/local-test/text-tool-editor-page-build.log
pwsh -File tools/build/test.ps1 -Preset local-test -Regex '^manga_tests_ui_editor_page$' -LogFile build/local-test/text-tool-editor-page-test.log
```

Expected: compilation or assertions fail because the old object name/tool mapping and creation signal are still present.

- [ ] **Step 3: Wire canvas intents to the ViewModel**

Replace the old `regionCreateCommitted` connection:

```cpp
connect(m_canvas,
        &ui::manga::MangaCanvas::pointTextCreateCommitted,
        m_viewModel,
        &EditorViewModel::createPointText);
connect(m_canvas,
        &ui::manga::MangaCanvas::textBoxCreateCommitted,
        m_viewModel,
        &EditorViewModel::createTextBox);
```

Connect successful creation to focus only after the ViewModel has rebuilt selection and Inspector state:

```cpp
connect(m_viewModel, &EditorViewModel::textRegionCreated, this, [this](const domain::RegionId&) {
    QMetaObject::invokeMethod(
        m_inspector, [this]() { m_inspector->focusTranslationEditor(); }, Qt::QueuedConnection);
});
```

Keep the existing double-click-to-focus behavior for already-created regions.

- [ ] **Step 4: Rename the tool button and state mapping**

Use the existing scan-text glyph, but rename the object and copy:

```cpp
addCanvasTool(QStringLiteral("canvasTextButton"),
              Typography::Icons::glyph(QStringLiteral("ic_fluent_scan_text_20_regular")),
              tr("文本 (T)"),
              CanvasTool::Text);
```

Update the `T` shortcut, `render()` switch, and checked-button lookup to `CanvasTool::Text` and `ui::manga::CanvasTool::Text`. Remove every `CreateRegion` branch and `canvasCreateRegionButton` lookup.

Do not switch automatically back to Select after creation; Koharu keeps the Text tool active so repeated clicks/drags create multiple text layers.

- [ ] **Step 5: Run the focused EditorPage test**

Run the two PowerShell commands from Step 2 again.

Expected: `manga_tests_ui_editor_page` passes; button and keyboard both choose Text, a canvas click creates and selects one region, and the translation editor receives focus without changing the active Text tool.

---

### Task 4: Expose Auto Fit in the Inspector

**Files:**
- Modify: `src/ui/widget/InspectorPanel.h:10-22, 138-166`
- Modify: `src/ui/widget/InspectorPanel.cpp:405-458, 665-690, 719-754`
- Modify: `tests/unit/ui/inspector_panel_test.cpp:56-474`
- Modify: `tests/unit/ui/inspector_state_test.cpp`

**Interfaces:**
- Consumes: Task 2 `InspectorField::AutoFit`, `SingleRegionInspectorState::autoFit`, and `MultiRegionInspectorState::autoFit`.
- Produces: FluentQt `ToggleSwitch` named `inspectorAutoFit` that emits the existing `fieldEditCommitted` intent.

- [ ] **Step 1: Write failing Inspector tests**

Add a focused single-selection test:

```cpp
void autoFitSwitchRendersAndEmitsFieldEdit() {
    InspectorPanel panel;
    SingleRegionInspectorState state;
    state.pageId = domain::PageId::generate();
    state.regionId = domain::RegionId::generate();
    state.autoFit = true;
    panel.setInspectorState(state);

    auto* toggle = panel.findChild<fluent::basicinput::ToggleSwitch*>(QStringLiteral("inspectorAutoFit"));
    QVERIFY(toggle != nullptr);
    QVERIFY(toggle->isChecked());

    QSignalSpy editSpy(&panel, &InspectorPanel::fieldEditCommitted);
    QTest::mouseClick(toggle, Qt::LeftButton);
    QCOMPARE(editSpy.size(), 1);
    QCOMPARE(editSpy.constFirst().at(2).value<InspectorField>(), InspectorField::AutoFit);
    QCOMPARE(editSpy.constFirst().at(3).toBool(), false);
}
```

Add state coverage that single selection carries a concrete bool and multi selection carries either a common bool or `std::nullopt` when values differ.

- [ ] **Step 2: Run Inspector tests and verify they fail**

Run:

```powershell
pwsh -File tools/build/build.ps1 -Preset local-test -Target manga_tests_ui_inspector_panel -LogFile build/local-test/text-tool-inspector-build.log
pwsh -File tools/build/build.ps1 -Preset local-test -Target manga_tests_ui_inspector_state -LogFile build/local-test/text-tool-inspector-state-build.log
pwsh -File tools/build/test.ps1 -Preset local-test -Regex '^(manga_tests_ui_inspector_panel|manga_tests_ui_inspector_state)$' -LogFile build/local-test/text-tool-inspector-test.log
```

Expected: compilation fails because the Auto Fit projection and control do not yet exist.

- [ ] **Step 3: Add the FluentQt Auto Fit field**

Create the control directly after the font-size field:

```cpp
auto* autoFitField = new fluent::layout::Field();
autoFitField->setLabelText(tr("自动适配"));
m_autoFitSwitch = new fluent::basicinput::ToggleSwitch();
m_autoFitSwitch->setObjectName(QStringLiteral("inspectorAutoFit"));
m_autoFitSwitch->setAccessibleName(tr("自动适配文本字号"));
autoFitField->setEditor(m_autoFitSwitch, fluent::WidgetOwnership::Owned);
typesetLayout->addWidget(autoFitField);
connect(m_autoFitSwitch, &fluent::basicinput::ToggleSwitch::toggled, this, [this](bool checked) {
    if (!m_updating) {
        emitFieldEdit(InspectorField::AutoFit, checked);
    }
});
```

Render rules:

```text
no selection/reset -> unchecked
single selection -> exact bool
multi selection/common value -> exact bool
multi selection/mixed value -> unchecked with tooltip "所选文本的自动适配设置不一致"; the next user toggle applies one bool to all selected regions
```

Do not disable the font-size control. Changing it remains valid and Task 2 turns Auto Fit off through the ViewModel, after which State rerender updates the switch.

- [ ] **Step 4: Run focused Inspector tests**

Run the three PowerShell commands from Step 2 again.

Expected: both Inspector tests pass; State drives the switch, user toggles emit `InspectorField::AutoFit`, and mixed multi-selection is represented without inventing a third persisted value.

---

### Task 5: Update the UI Contract and Verify the End-to-End Surface

**Files:**
- Modify: `docs/ui/components/manga-canvas.md`
- Verify: all files changed in Tasks 1-4

**Interfaces:**
- Consumes: completed Text tool behavior from Tasks 1-4.
- Produces: current textual UI specification and runtime evidence for click/drag/focus/edit/undo behavior.

- [ ] **Step 1: Update the canvas specification**

Replace drag-only Create Region wording with the exact observable contract:

```markdown
### Text tool

- `T` activates Text and remains active after creation.
- Clicking inside the page creates point text with a source-space frame of at most `320 × 120` pixels.
- Dragging at least four view pixels creates a paragraph text box from the normalized, page-clamped frame.
- A dashed accent draft frame previews paragraph creation.
- Creation selects the new text region and focuses the Inspector translation editor.
- New text enables Auto Fit. Point text starts left/top aligned; paragraph text uses the normal center/middle defaults.
- Escape, focus loss, tool change, and temporary Space pan cancel an unfinished draft without modifying undo history.
```

Record the deliberate V0.1 deviations: axis-aligned geometry, existing eight resize handles, and no rotation.

- [ ] **Step 2: Format only touched C++ files**

Run the repository's configured formatter once over the C++ files changed by Tasks 1-4. Do not format unrelated files or the whole repository.

Expected: only touched C++ files change.

- [ ] **Step 3: Build the UI and all focused test executables**

Run:

```powershell
pwsh -File tools/build/build.ps1 -Preset local-test -Target manga_ui -LogFile build/local-test/text-tool-ui-build.log
pwsh -File tools/build/build.ps1 -Preset local-test -Target manga_tests_ui_manga_canvas -LogFile build/local-test/text-tool-canvas-build.log
pwsh -File tools/build/build.ps1 -Preset local-test -Target manga_tests_ui_editor_view_model -LogFile build/local-test/text-tool-view-model-build.log
pwsh -File tools/build/build.ps1 -Preset local-test -Target manga_tests_ui_editor_page -LogFile build/local-test/text-tool-editor-page-build.log
pwsh -File tools/build/build.ps1 -Preset local-test -Target manga_tests_ui_inspector_panel -LogFile build/local-test/text-tool-inspector-build.log
pwsh -File tools/build/build.ps1 -Preset local-test -Target manga_tests_ui_inspector_state -LogFile build/local-test/text-tool-inspector-state-build.log
```

Expected: every command exits `0`.

- [ ] **Step 4: Run the complete focused regression set**

Run:

```powershell
pwsh -File tools/build/test.ps1 -Preset local-test -Regex '^(manga_tests_ui_manga_canvas|manga_tests_ui_editor_view_model|manga_tests_ui_editor_page|manga_tests_ui_inspector_panel|manga_tests_ui_inspector_state|manga_tests_ui_editor_save)$' -LogFile build/local-test/text-tool-focused-ctest.log
```

Expected: six tests pass, zero fail, and `--no-tests=error` ensures a misspelled regex cannot report false success.

- [ ] **Step 5: Exercise the actual editor surface**

Build and launch the real application:

```powershell
pwsh -File tools/build/build.ps1 -Preset local-test -Target MangaTranslatorApp -LogFile build/local-test/text-tool-app-build.log
.\build\local-test\bin\MangaTranslatorApp.exe
```

Verify on an imported page:

1. Press `T`; the Text button becomes checked.
2. Click near the page center; one text region appears, is selected, and the translation editor receives focus.
3. Switch to Translated preview and enter translation text; the preview updates through the existing preview pipeline.
4. Click the canvas again without changing tools, then drag from bottom-right to top-left; Text remains active, the dashed draft normalizes, and a paragraph frame is created.
5. Drag beyond a page edge; the committed frame remains within page bounds.
6. Start a drag and press Escape; no region is created and undo history is unchanged.
7. Toggle Auto Fit, then change fixed font size; Auto Fit turns off after State rerender.
8. Move and resize both newly created regions; undo and redo creation, text edit, move, and resize.
9. Repeat visual checks in Light and Dark themes and at a narrow editor width. Confirm the tool rail and Inspector do not clip.

Expected: the changed runtime path matches the listed behavior with no canvas drift, stale draft, duplicate command, or focus loop.

- [ ] **Step 6: Report verification boundaries**

Report the exact build/test commands that ran, their exit status, and whether Light, Dark, normal-width, and narrow-width runtime checks were observed. Do not claim rotation or Koharu edge-only handle parity; both are explicitly outside this plan.
