# P5-C：Region 编辑、Inspector、Undo/Dirty 与保存 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 P5-B 编辑器骨架上交付可持久化的矩形 Region 创建与编辑、选择驱动的 Inspector、项目级单一 Undo/Redo 历史、可靠 Dirty/保存语义和关闭拦截。

**Architecture:** `EditorViewModel` 持有每个打开项目唯一的可编辑 `ComicProject` 聚合与 `QUndoStack`，State 仍只投影稳定 ID 和值类型。同步编辑一律通过 application UseCase；Undo Command 保存 before/after 快照和稳定身份，不保存 Domain、Scene Item 或 Widget 指针。P5-C 同时提供通用的可逆 Region 快照应用接口，供 P5-D 把 OCR、翻译、清除和排版采用接入同一历史。

**Tech Stack:** C++23、Qt 6.11.1（Widgets/Gui/Test）、FluentQt v1.7.4 公共 C++ API、`QGraphicsView/QGraphicsScene`、`QUndoStack`、CMake presets `local-*`、Qt Test（offscreen）。

**Spec:** [`phase-5-ui-editor.md`](phase-5-ui-editor.md) + [`phase-5-ui-editor-supplement.md`](phase-5-ui-editor-supplement.md) §4–§8、§11–§13 + `docs/ui/pages/editor.md`、`docs/ui/components/manga-canvas.md`、`docs/ui/components/region-overlay.md`。前置执行计划：[`phase-5-b-editor-skeleton-plan.md`](phase-5-b-editor-skeleton-plan.md)。

## Global Constraints

- 先完成 P5-B；本计划引用其 `ProjectSessionDescriptor`、`EditorState`、`EditorViewModel`、`EditorPage`、`MangaCanvas`、页面列表与编辑器路由。
- C++23、UTF-8/LF、4 空格缩进、英文标识符和代码注释；只格式化本计划触及的 C/C++ 文件。
- Qt 元对象语法统一使用 `Q_SIGNALS`、`Q_SLOTS`、`Q_EMIT`；typed `connect()` 的普通接收成员不声明为 slot，禁止 `signals`、`slots`、`emit` 关键字别名。
- UI State 不保存 `ComicProject*`、`ComicPage*`、`TextRegion*`、`QGraphicsItem*`、Widget、`QImage` 或 `QPixmap`。可编辑聚合只在 `EditorViewModel` 的私有会话成员中存在。
- View 只渲染 State 并报告 intent；Domain 修改必须通过 application UseCase。Command 可以调用同步 UseCase，但不得直接修改 Domain。
- 每个打开项目仅一个 `QUndoStack`。文本控件自身 Undo 必须关闭；`Ctrl+Z/Ctrl+Y` 永远操作项目栈。
- `ComicProject::isDirty()` 是关闭和保存的权威；`QUndoStack::isClean()` 只用于辅助显示。
- Scene 权威坐标为 source-image pixels；Region 最小尺寸 8×8 source px，手势启动阈值 4 device px，手柄视觉 8 device px、命中至少 16 device px。
- `QGraphicsItem::paint()` 不执行 Domain 查询、文本布局、I/O、Future 等待或 Widget 创建。
- P5-C 不启动 OCR、翻译、清除或排版后台任务；只预留 P5-D 采用命令所需的快照接口。
- V0.1 只实现矩形 Region；不引入自动检测、多边形 Region、Split Curtain 或全局快捷键配置。
- 下列提交步骤仅在执行者得到明确 Git 授权时执行；否则在验证通过后报告建议提交消息，不创建提交。
- 构建与测试统一使用 `.agents/skills/manga-translator-build/scripts/{build,test}.ps1` 和已签入的 `local-test` preset。

## File Structure

- `src/application/dto/ProjectDto.h`：打开后会话值对象，携带可编辑聚合、项目根和恢复警告。
- `src/application/usecase/region/RegionUseCases.*`：可重放的精确插入、移除和 Region 快照应用。
- `src/domain/model/{ComicPage,ComicProject}.*`：按指定 orderIndex 原子插入既有 Region 的最小 Domain 能力。
- `src/ui/screen/editor/EditorState.h`：Region、选择、工具、Inspector、Dirty/保存的值类型投影。
- `src/ui/screen/editor/EditorViewModel.*`：项目会话、选择、命令栈、同步编辑和保存编排。
- `src/ui/command/`：Region 生命周期、几何、文本、候选和样式命令。
- `src/ui/manga/`：Region/Handle/创建投影和 source↔scene↔device 交互。
- `src/ui/widget/InspectorPanel.*`：Empty/Single/Multi Inspector，可访问的表单投影。
- `tests/unit/{domain,application,ui}/`：按所有权镜像测试。

---

### Task 1: 修复打开项目后的可编辑聚合所有权

**Files:**
- Modify: `src/application/dto/ProjectDto.h`
- Modify: `src/application/mapping/ProjectMapper.h`
- Modify: `src/ui/screen/home/HomeViewModel.h`
- Modify: `src/ui/screen/home/HomeViewModel.cpp`
- Modify: `src/ui/screen/editor/EditorViewModel.h`
- Modify: `src/ui/screen/editor/EditorViewModel.cpp`
- Modify: `src/app/Application.cpp`
- Test: `tests/unit/ui/home_view_model_test.cpp`
- Test: `tests/unit/ui/editor_view_model_test.cpp`

**Interfaces:**
- Consumes: P5-B `ProjectSessionDescriptor` 与 repository `OpenProjectResult`。
- Produces:
  - `application::OpenedProjectSession { ComicProject project; QString projectRootPath; RecoverySource recoverySource; QVector<ProjectWarning> warnings; }`
  - `mapping::toSessionDescriptor(const OpenedProjectSession&)`
  - `HomeViewModel::projectOpened(const OpenedProjectSession&)`
  - `EditorViewModel::openProjectSession(OpenedProjectSession)`；私有 `std::optional<OpenedProjectSession> m_session`。

- [ ] **Step 1: 写失败测试，证明聚合不会在 Home→Editor 交接时丢失**

```cpp
void openedSessionCarriesEditableAggregate() {
    FakeProjectRepository repository;
    repository.openPageCount = 1;
    HomeViewModel viewModel(&m_recents, &repository, &m_registry);
    QSignalSpy spy(&viewModel, &HomeViewModel::projectOpened);

    viewModel.openProjectPath(QStringLiteral("D:/manga/demo"));
    QTRY_COMPARE(spy.size(), 1);
    const auto session = spy.takeFirst().at(0).value<application::OpenedProjectSession>();
    QVERIFY(session.project.id().isValid());
    QCOMPARE(session.project.pages().size(), 1);
    QCOMPARE(session.projectRootPath, QStringLiteral("D:/manga/demo"));
}
```

在 `editor_view_model_test.cpp` 增加 `openSessionRetainsAggregateForEditing()`，通过仅测试可见的 `currentProjectRevision()` 断言初始 revision，并在后续 Task 2 删除该临时探针，改由真实编辑行为覆盖。

- [ ] **Step 2: 运行聚焦测试确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_home_view_model
```

Expected: 编译失败，`OpenedProjectSession` 和新信号不存在。

- [ ] **Step 3: 增加会话 DTO 与映射**

```cpp
struct OpenedProjectSession {
    domain::ComicProject project;
    QString projectRootPath;
    RecoverySource recoverySource{RecoverySource::Current};
    QVector<ProjectWarning> warnings;
};

[[nodiscard]] ProjectSessionDescriptor toSessionDescriptor(const OpenedProjectSession& session);
```

`HomeViewModel::handleOpenFinished()` 将 `OpenProjectResult` 移入 `OpenedProjectSession` 后发 Signal。`Application` 的 direct GUI-thread connection 按值调用 `EditorViewModel::openProjectSession`，由 ViewModel 移入 `m_session`。不要同时保留第二份聚合。

- [ ] **Step 4: 让 P5-B 页面/缩略图投影从会话聚合派生**

`EditorViewModel::openProjectSession` 先调用 `mapping::toSessionDescriptor(session)` 生成既有页面投影，再替换 `m_session`。关闭项目顺序固定为：取消 view-owned work → invalidate generation → 清 UndoStack（Task 5 接入）→ 释放 `m_session`。

- [ ] **Step 5: 运行 Home 与 Editor ViewModel 测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_(home|editor)_view_model"
```

Expected: 新旧用例全绿，打开结果的 `warnings` 和 `recoverySource` 仍完整保留供 P5-D 使用。

- [ ] **Step 6: Commit（仅在获授权时）**

```bash
git add src/application/dto/ProjectDto.h src/application/mapping/ProjectMapper.h src/ui/screen/home src/ui/screen/editor src/app/Application.cpp tests/unit/ui
git commit -m "fix(application): retain editable project in editor session"
```

---

### Task 2: 提供可逆的 Region 精确插入、移除与快照应用契约

**Files:**
- Modify: `src/domain/model/ComicPage.h`
- Modify: `src/domain/model/ComicPage.cpp`
- Modify: `src/domain/model/ComicProject.h`
- Modify: `src/domain/model/ComicProject.cpp`
- Modify: `src/application/usecase/region/RegionUseCases.h`
- Modify: `src/application/usecase/region/RegionUseCases.cpp`
- Test: `tests/unit/domain/ComicPageTest.cpp`
- Test: `tests/unit/domain/ComicProjectTest.cpp`
- Test: `tests/unit/application/RegionUseCasesTest.cpp`

**Interfaces:**
- Produces:
  - `ComicPage::insertRegion(TextRegion region, int orderIndex)`
  - `ComicProject::insertRegionOnPage(PageId, TextRegion, int orderIndex)`
  - `InsertRegionUseCase::execute(InsertRegionRequest)`
  - `RemoveRegionUseCase::execute(RemoveRegionRequest) -> RemovedRegion`
  - `ApplyRegionSnapshotsUseCase::execute(ApplyRegionSnapshotsRequest)`

```cpp
struct RemovedRegion {
    domain::TextRegion region;
    int orderIndex{0};
};

struct ApplyRegionSnapshotsRequest {
    domain::PageId pageId;
    std::vector<domain::PageRegionEdit> edits;
};
```

- [ ] **Step 1: 写失败的 Domain 测试**

覆盖：在 0/中间/末尾插入；ID 冲突、越界 order、非法 geometry 时不修改页；插入后 order 连续；项目 revision 只增加一次。

```cpp
void ComicProjectTest::insertRegionAtOriginalOrderIsAtomic() {
    ComicProject project("demo");
    const auto pageId = addPage(project);
    const TextRegion restored(RegionId::generate(), RectGeometry{10, 10, 80, 60}, 0);
    const auto before = project.revision();
    QVERIFY(project.insertRegionOnPage(pageId, restored, 0).has_value());
    QCOMPARE(project.findPage(pageId)->regions().front().id(), restored.id());
    QCOMPARE(project.revision(), before + 1);
}
```

- [ ] **Step 2: 运行 Domain 测试确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex manga_tests_domain
```

- [ ] **Step 3: 实现原子插入**

`ComicPage::insertRegion` 在临时 vector 上验证并插入，再一次性交换；`ComicProject::insertRegionOnPage` 先检查项目级 RegionId 唯一性，成功后调用一次 `markDirty()`。不要用“append 后 reorder”制造半成功状态或两次 revision。

- [ ] **Step 4: 写失败的 Application 测试**

覆盖精确 ID 重插、移除返回完整 `TextRegion`、几何/文本/翻译/排版快照应用、批量校验失败不修改 Domain。

- [ ] **Step 5: 实现同步 UseCase 并运行测试**

`ApplyRegionSnapshotsUseCase` 先校验同页所有 RegionId 和目标值，再一次调用 `applyPageRegionEdits`，确保批量样式与多 Region 采用原子提交。它不接收固定 expected revision；命令首次 push 前由 ViewModel 校验，后续 undo/redo 的可执行性由栈顺序保证。任何失败返回 `internal.undo_invariant_broken`，交给 Task 5 停止继续重放。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex manga_tests_application
```

- [ ] **Step 6: Commit（仅在获授权时）**

```bash
git add src/domain src/application/usecase/region tests/unit/domain tests/unit/application
git commit -m "feat(application): add reversible region edit contracts"
```

---

### Task 3: 扩展 Editor State 为 Region、选择、工具与保存投影

**Files:**
- Modify: `src/ui/screen/editor/EditorState.h`
- Modify: `src/ui/screen/editor/EditorViewModel.h`
- Modify: `src/ui/screen/editor/EditorViewModel.cpp`
- Test: `tests/unit/ui/editor_view_model_test.cpp`

**Interfaces:**
- Produces `CanvasTool { Select, Pan, CreateRegion }`、`PreviewMode { Original, Translated, Compare }`。
- Produces `RegionProjection`、`InspectorMode`、`DocumentSaveState`。

```cpp
struct RegionProjection {
    domain::RegionId id;
    domain::RectGeometry geometry;
    int orderIndex{0};
    quint64 contentRevision{0};
    bool isLocked{false};
    bool isCleared{false};
    bool hasManualTranslation{false};
    bool operator==(const RegionProjection&) const = default;
};
```

`EditorState` 增加当前页 `regions`、`selectedRegionIds`、`primaryRegionId`、`activeTool`、`previewMode`、`isDirty`、`isSaving`、`canUndo`、`canRedo`、Undo/Redo 文案和安全保存错误摘要。

- [ ] **Step 1: 写 State 序列失败测试**

覆盖打开页投影、稳定 ID 选择、Ctrl 多选、空白清选、切页清选择、无变化 State 抑制、删除后清除不存在 ID。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_editor_view_model
```

- [ ] **Step 3: 实现集中投影刷新**

增加私有 `rebuildCurrentPageProjection()`，只从 `m_session->project` 读取并生成值类型。所有同步命令完成后调用它；View 的 `render()` 不查询 Domain。

- [ ] **Step 4: 实现选择与工具 intent**

```cpp
void setActiveTool(CanvasTool tool);
void selectRegion(domain::RegionId id, SelectionMode mode);
void clearRegionSelection();
void selectAllRegionsOnCurrentPage();
void setPreviewMode(PreviewMode mode);
```

选择集合按页面 Region order 排序后投影，`primaryRegionId` 永远属于集合；工具切换取消未提交手势但不清既有选择。

- [ ] **Step 5: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex manga_tests_ui_editor_view_model
```

- [ ] **Step 6: Commit（仅在获授权时）**

```bash
git add src/ui/screen/editor tests/unit/ui/editor_view_model_test.cpp
git commit -m "feat(ui): project region selection into editor state"
```

---

### Task 4: Region Overlay、八向手柄和画布工具状态机

**Files:**
- Create: `src/ui/manga/RegionOverlayItem.h`
- Create: `src/ui/manga/RegionOverlayItem.cpp`
- Create: `src/ui/manga/SelectionHandleItem.h`
- Create: `src/ui/manga/SelectionHandleItem.cpp`
- Create: `src/ui/manga/RegionInteraction.h`
- Modify: `src/ui/manga/MangaCanvas.h`
- Modify: `src/ui/manga/MangaCanvas.cpp`
- Modify: `src/ui/CMakeLists.txt`
- Test: `tests/unit/ui/region_interaction_test.cpp`
- Test: `tests/unit/ui/manga_canvas_test.cpp`

**Interfaces:**
- Canvas signals: `regionCreateCommitted(RectGeometry)`、`regionSelectionRequested(RegionId, SelectionMode)`、`regionGeometryCommitted(RegionId, RectGeometry, EditKind)`、`gestureCancelled()`。
- `EditKind { Move, Resize }`；八向 `HandlePosition`。

- [ ] **Step 1: 写纯几何失败测试**

覆盖 source/scene/view/device 往返、5%/100%/1600% 下 16 device px 命中、八向 resize、页面边界 clamp、8×8 最小尺寸和 4 device px 拖拽阈值。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_region_interaction
```

- [ ] **Step 3: 实现无 Widget 的 Graphics Item**

`RegionOverlayItem` 使用 cosmetic pen，Z 值 `kZRegionNormal`；八个 `SelectionHandleItem` 为子 Item，只有手柄设置 `ItemIgnoresTransformations`，`shape()` 和 `boundingRect()` 都覆盖命中范围。Item 只持有 RegionId 与临时几何，不访问 ViewModel/Domain。

- [ ] **Step 4: 实现 press/move/release 临时投影**

Press 保存起始几何；Move 只修改 Item；Release 仅当最终值不同才发一条 committed intent；Escape 恢复起始几何。Create tool 使用 `kZToolCreation` 临时框，低于最小尺寸时丢弃。

- [ ] **Step 5: 实现键盘与可访问交互**

Tab/Shift+Tab 遍历 Region；方向键移动 1 source px；Shift+方向键调整当前主手柄 1 source px；Delete 只发删除 intent。Canvas 暴露 Region 数量、选择状态和动作的可访问名称。

- [ ] **Step 6: 运行画布测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_(region_interaction|manga_canvas)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/ui/manga src/ui/CMakeLists.txt tests/unit/ui
git commit -m "feat(manga): add rectangle region editing interactions"
```

---

### Task 5: 项目级 Undo Command 与 Region 生命周期/几何命令

**Files:**
- Create: `src/ui/command/EditorCommandContext.h`
- Create: `src/ui/command/EditorCommandContext.cpp`
- Create: `src/ui/command/RegionCommands.h`
- Create: `src/ui/command/RegionCommands.cpp`
- Modify: `src/ui/screen/editor/EditorViewModel.h`
- Modify: `src/ui/screen/editor/EditorViewModel.cpp`
- Modify: `src/ui/CMakeLists.txt`
- Test: `tests/unit/ui/editor_commands_test.cpp`
- Test: `tests/unit/ui/editor_view_model_test.cpp`

**Interfaces:**
- `EditorCommandContext` 持有非 owning `ComicProject*` 和同步 application UseCase；公开 `insertRegion`、`removeRegion`、`applySnapshots`、`notifyProjectionChanged(PageId)`。
- `CreateRegionCommand`、`DeleteRegionsCommand`、`RegionGeometryCommand`。
- Command 都公开 `pageId()`，供跨页 Undo/Redo 导航。

- [ ] **Step 1: 写命令失败测试**

覆盖 create undo redo 保持同一 RegionId、批量删除一条命令、move/resize 一次 release 一条命令、跨页 undo 先修改后导航、内部失败使 `undoHistoryFaulted=true` 并禁止继续重放。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_editor_commands
```

- [ ] **Step 3: 实现 Command Context 和命令**

`CreateRegionCommand` 在构造前由 ViewModel 验证几何并生成稳定 RegionId；first `redo()` 精确插入该 Region。`DeleteRegionsCommand` 保存按 order 排序的完整 `TextRegion` 快照，undo 按原顺序插回。`RegionGeometryCommand` 保存 before/after 并通过 `ApplyRegionSnapshotsUseCase` 应用。

- [ ] **Step 4: 在 EditorViewModel 建立唯一 QUndoStack**

```cpp
std::unique_ptr<QUndoStack> m_undoStack;
std::unique_ptr<command::EditorCommandContext> m_commandContext;
bool m_undoHistoryFaulted{false};
```

打开项目创建；关闭项目先销毁 stack 再释放 aggregate。`undo()`/`redo()` 先 seal 文本批次，再执行；完成后导航至 `command.pageId()`、刷新 Region/Inspector/Dirty 投影。

- [ ] **Step 5: 将 Canvas intent 接入命令**

Create、Delete、Move、Resize 不再直接调用 Domain；一次 release 只 push 一条命令。空操作不进栈。

- [ ] **Step 6: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_(editor_commands|editor_view_model)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/ui/command src/ui/screen/editor src/ui/CMakeLists.txt tests/unit/ui
git commit -m "feat(ui): add project undo history for region edits"
```

---

### Task 6: Inspector State 与 Empty/Single/Multi Surface

**Files:**
- Create: `src/ui/screen/editor/InspectorState.h`
- Create: `src/ui/widget/InspectorPanel.h`
- Create: `src/ui/widget/InspectorPanel.cpp`
- Modify: `src/ui/screen/editor/EditorState.h`
- Modify: `src/ui/screen/editor/EditorViewModel.h`
- Modify: `src/ui/screen/editor/EditorViewModel.cpp`
- Modify: `src/ui/screen/editor/EditorPage.h`
- Modify: `src/ui/screen/editor/EditorPage.cpp`
- Test: `tests/unit/ui/inspector_state_test.cpp`
- Test: `tests/unit/ui/inspector_panel_test.cpp`

**Interfaces:**
- `InspectorState = std::variant<PageInspectorState, SingleRegionInspectorState, MultiRegionInspectorState>`。
- Mixed 值使用 `std::optional<T>`：多选中 `nullopt` 表示 mixed，不代表非法或空字符串。
- Panel signals 使用稳定 PageId/RegionId 和字段 enum，不携带 Widget 指针。

- [ ] **Step 1: 写 Inspector 投影失败测试**

覆盖空选页面信息、单选完整文本/候选/`TextLayout`、多选安全交集、mixed 值、删除/切页后的模式变化。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_inspector_state
```

- [ ] **Step 3: 实现 ViewModel 投影**

单选包含 source、manual/effective translation、候选、字体、字重、字距、字号、方向、水平/垂直对齐、行距、四边距、文字色、描边色与描边宽度。多选只允许补充规格列出的安全字段；source/translation/candidates 不在多选编辑范围。

- [ ] **Step 4: 实现 InspectorPanel**

使用 FluentQt `Expander` 承载候选与高级排版；可见 raw Qt 控件必须封装并使用 Fluent 语义 token。Empty/Single/Multi 三个 host 互斥，不堆叠隐藏焦点项。正常宽度为稳定右栏，Overlay 响应式留给 P5-D。

- [ ] **Step 5: 完成表单语义**

每个 label 与输入关联；非法数值保留编辑文本并显示错误，不发送 intent；颜色不仅用 swatch，还显示可读十六进制值；长字体名和错误消息有 wrap/elide 契约。

- [ ] **Step 6: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_inspector_(state|panel)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/ui/screen/editor src/ui/widget/InspectorPanel.* src/ui/CMakeLists.txt tests/unit/ui
git commit -m "feat(ui): add selection-driven region inspector"
```

---

### Task 7: 文本、候选与样式的可合并命令

**Files:**
- Create: `src/ui/command/RegionPropertyCommand.h`
- Create: `src/ui/command/RegionPropertyCommand.cpp`
- Create: `src/ui/screen/editor/TextEditSession.h`
- Create: `src/ui/screen/editor/TextEditSession.cpp`
- Modify: `src/ui/widget/InspectorPanel.cpp`
- Modify: `src/ui/screen/editor/EditorViewModel.h`
- Modify: `src/ui/screen/editor/EditorViewModel.cpp`
- Test: `tests/unit/ui/text_edit_session_test.cpp`
- Test: `tests/unit/ui/editor_commands_test.cpp`

**Interfaces:**
- `RegionPropertyField` 枚举覆盖 SourceText、ManualTranslation、Candidate、FontFamily、FontWeight、LetterSpacing、FontSize、Orientation、Alignment、LineSpacing、Margins、TextColor、StrokeColor、StrokeWidth。
- `TextEditSessionKey { ProjectId, PageId, RegionId, RegionPropertyField, quint64 sessionId }`。
- `RegionPropertyCommand::mergeWith` 仅在 key 相同且单调时钟间隔 `<=500ms` 时合并。

- [ ] **Step 1: 写文本合并与 IME 失败测试**

覆盖 499/501 ms 边界、FocusOut/选择变化/切页/Undo/保存封口、preedit 不写 Domain、commit string 只产生一条命令、State 回放恢复光标/选择。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_text_edit_session
```

- [ ] **Step 3: 实现统一属性命令**

命令保存受影响 Region 的 before/after `PageRegionEdit` 值；批量样式是一条命令且先全量验证，任一目标失败则不 push。采用候选是独立命令，不与手工译文合并。

- [ ] **Step 4: 实现 TextEditSession**

使用可注入 monotonic clock 测试，不读取 wall clock。Inspector 的文本控件关闭内部 undo/redo；IME preedit 只停留在控件，commit 后发送完整新值与 session key。

- [ ] **Step 5: 接入 Inspector intents**

人工译文更新 `TranslationEntry::manualText`；source 编辑保持 `TextContent` provenance 规则。样式改动只失效目标 Region 的渲染缓存接口，实际文字层缓存可在 P5-D 处理采用/预览任务中完成。

- [ ] **Step 6: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_(text_edit_session|editor_commands|inspector)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/ui/command src/ui/screen/editor src/ui/widget/InspectorPanel.cpp tests/unit/ui
git commit -m "feat(ui): add undoable text and layout editing"
```

---

### Task 8: CommandBar、Dirty/保存竞争与关闭拦截

**Files:**
- Create: `src/ui/widget/EditorCommandBar.h`
- Create: `src/ui/widget/EditorCommandBar.cpp`
- Modify: `src/ui/screen/editor/EditorViewModel.h`
- Modify: `src/ui/screen/editor/EditorViewModel.cpp`
- Modify: `src/ui/screen/editor/EditorPage.h`
- Modify: `src/ui/screen/editor/EditorPage.cpp`
- Modify: `src/ui/window/MainWindow.h`
- Modify: `src/ui/window/MainWindow.cpp`
- Modify: `src/app/Application.cpp`
- Test: `tests/unit/ui/editor_save_test.cpp`
- Test: `tests/unit/ui/editor_page_test.cpp`
- Test: `tests/unit/ui/main_window_test.cpp`

**Interfaces:**
- `EditorViewModel::saveProject()`、`requestCloseProject()`、`confirmDiscardAndClose()`、`cancelClose()`。
- Signals: `closeDecisionRequested(ProjectCloseSummary)`、`saveCompleted(QString)`、`saveFailed(ApplicationError)`、`projectClosed()`。
- `ProjectCloseSummary` 只含项目名、Dirty 与安全文案，不含路径或正文。

- [ ] **Step 1: 写保存竞争失败测试**

覆盖 clean save、保存期间继续编辑、repository 返回旧 revision、保存失败保持 Dirty、保存不清 Undo、Ctrl+S 先 seal 文本、关闭 Dirty 项目必须决策。

```cpp
void saveCompletionDoesNotCleanNewerRevision() {
    viewModel.saveProject();
    viewModel.editTranslation(regionId, QStringLiteral("newer"));
    repository.completeSaveWith(capturedRevision);
    QTRY_VERIFY(!viewModel.state().isSaving);
    QVERIFY(viewModel.state().isDirty);
    QVERIFY(!viewModel.undoStackIsCleanForTest());
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_editor_save
```

- [ ] **Step 3: 实现保存协议**

保存前 seal 已完成文本会话，捕获 `savedRequestRevision`，传 aggregate snapshot 给 `SaveProjectUseCase`。Future 成功后只在 `project.markCleanIfRevision(result.savedRevision)` 为 true 时调用 `undoStack.setClean()`；否则保持 Dirty。保存不清历史。

- [ ] **Step 4: 实现关闭顺序**

窗口 close/project switch 先查询 ViewModel；Dirty 时创建一次性 `ContentDialog`（保存并关闭/不保存/取消）。保存成功后关闭；失败留在编辑器。关闭执行：停止接受命令 → 取消 view work → P5-D 项目任务取消钩子（当前为空实现）→ 销毁 UndoStack → repository close → 释放会话。

- [ ] **Step 5: 实现 EditorCommandBar**

CommandBar 包含 Undo、Redo、Select/Pan/Create Region、Preview Selector、保存；智能翻译入口由 P5-D 添加。`V/R/H/Space/+/-/0/1/Delete/Enter/Ctrl+S/Ctrl+Z/Ctrl+Y` 尊重文本焦点和 IME。每个 icon-only action 有 Tooltip 和 accessible name。

- [ ] **Step 6: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_(editor_save|editor_page|main_window)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/ui/widget/EditorCommandBar.* src/ui/screen/editor src/ui/window src/app/Application.cpp tests/unit/ui
git commit -m "feat(ui): add editor save and close workflow"
```

---

### Task 9: P5-C 集成、性能不变量与验收

**Files:**
- Modify: `src/ui/CMakeLists.txt`
- Modify: `tests/unit/ui/CMakeLists.txt`
- Create: `tests/integration/ui/editor_region_workflow_test.cpp`
- Create: `tests/visual/editor/README.md`

**Interfaces:**
- Produces: P5-D 可消费的 `EditorCommandContext::applySnapshots`、项目级 `QUndoStack`、`EditorViewModel::pushPreparedCommand(...)` 和项目任务取消钩子。

- [ ] **Step 1: 写完整纵向工作流测试**

打开项目 → 创建 Region → 移动/缩放 → 编辑译文/样式 → Undo/Redo 跨页 → 保存 → 新编辑 → 关闭拦截。断言 stable IDs、revision/Dirty、clean point 和页面导航。

- [ ] **Step 2: 增加确定性密集测试**

构造 300 Region 页面；断言 scene item 数量线性且无 QWidget-per-region，pan/zoom 时 Domain 查询计数、布局调用计数、I/O 调用计数均为 0。wall-clock fps 只记录，不作为普通 CI 断言。

- [ ] **Step 3: 配置并构建**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/configure.ps1 -Preset local-test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test
```

- [ ] **Step 4: 运行聚焦与全量测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_(domain|application|ui)"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test
```

- [ ] **Step 5: 执行 P5-C 人工检查并记录边界**

检查 Light/Dark、5%/100%/1600%、单选/多选、八向手柄、键盘、IME、保存失败和窄宽 Inspector 可达性。P5-D 才负责正式 full-profile 视觉证据；P5-C 不以单张截图宣称视觉通过。

- [ ] **Step 6: Commit（仅在获授权时）**

```bash
git add src/ui/CMakeLists.txt tests/unit/ui/CMakeLists.txt tests/integration/ui tests/visual/editor/README.md
git commit -m "test(ui): cover region editing and project history workflow"
```

## P5-C Exit Gate

- Home/Import 打开的完整 `ComicProject` 聚合只由当前 Editor 会话持有，关闭顺序确定。
- 用户可通过指针和键盘创建、选择、移动、八向缩放、批量选择及删除矩形 Region。
- Empty/Single/Multi Inspector 与选区一致；文本、候选和完整 V0.1 `TextLayout` 都可编辑。
- 所有用户编辑进入唯一项目 `QUndoStack`；文本合并、IME、跨页 Undo 与批量原子性有测试。
- Dirty/保存竞争/clean point/关闭拦截以 Domain revision 为权威。
- P5-D 可把处理结果封装成 prepared snapshot 后 push 到同一命令历史，无需绕过 application UseCase。
