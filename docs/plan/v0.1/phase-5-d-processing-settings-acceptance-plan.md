# P5-D：处理采用、任务状态、Settings、错误恢复与视觉验收 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 P5-C 可编辑工作台上接通 OCR、翻译、清除和排版的启动与可撤销采用，交付全局任务状态条、真实可执行的 Settings Surface、完整错误恢复入口，并用 full-profile 工程与视觉证据关闭 P5-A～P5-D 的 UI 验收。

**Architecture:** `OperationRegistry` 扩展为 application 层线程安全、QtCore 可观察的任务事实源；处理 UseCase/Workflow 只产生候选结果，不直接覆盖用户数据。采用阶段先生成 immutable prepared change，再由 P5-C 项目 `QUndoStack` 同步应用。Settings 通过 typed UseCase 使用 `IAppSettingsStore`/`ICredentialStore`/最小 OCR 模型校验 Port，UI 不读 QSettings、Secret 或 services。错误统一转换为 `RecoveryPresentation`，全局状态条只展示摘要，关键错误留在所属页面。

**Tech Stack:** C++23、Qt 6.11.1（Core/Gui/Widgets/Concurrent/Test）、FluentQt v1.7.4 公共 C++ API、既有 application ports/usecases、P4 processing adapters、`QAbstractListModel`/delegate、CMake presets `local-*`、Qt Test、FluentQt full-profile visual evidence contract v4。

**Spec:** [`phase-5-ui-editor.md`](phase-5-ui-editor.md) + [`phase-5-ui-editor-supplement.md`](phase-5-ui-editor-supplement.md) §4、§6–§13 + `docs/ui/components/status-task-strip.md`、`docs/ui/components/settings-controls.md`、`docs/ui/pages/settings.md`、`docs/ui/pages/editor.md`。前置执行计划：[`phase-5-c-region-inspector-plan.md`](phase-5-c-region-inspector-plan.md)。

## Global Constraints

- 先完成 P5-C；所有处理采用必须进入其唯一项目 `QUndoStack`，不得建立第二个历史或直接把 Future 结果写入 Widget。
- UI 不依赖 `services`、`data` 或 `network`，不创建 ONNX session、HTTP client、credential store、文件 adapter 或 QSettings。
- Qt 元对象语法统一使用 `Q_SIGNALS`、`Q_SLOTS`、`Q_EMIT`；typed `connect()` 的普通接收成员不声明为 slot，禁止 `signals`、`slots`、`emit` 关键字别名。
- 处理任务绑定 `operationId + projectSessionGeneration + ProjectId + PageId + RegionId/contentRevision`。切页不取消项目任务；关闭/切换项目、用户取消或同目标替换才失效。
- Worker 只传递不可变值与 `QImage`；Widget、`QPixmap`、Scene、ViewModel、UndoStack 和 FluentQt 对象只在 GUI 线程。
- `safeMessage` 可见，`diagnostic` 永不上屏；Cancelled/stale completion 静默，不制造错误 Toast。
- `AppStatusTaskStrip` 是摘要，不替代页面级 `InfoBar`、阻塞 `ContentDialog` 或任务详情。
- Settings 只注册有真实读取、验证、保存和运行时效果的分类。V0.1 本计划交付常规、OCR 模型位置、AI 翻译凭据/基础配置和外观；模型下载/迁移、连接测试、排版默认、性能调度和隐私清理没有现成能力边界，不显示入口。
- API Key 只经 `SecretBytes` 和 Credential UseCase；状态查询只调用 `hasApiKey()`，不得 read-then-discard、回显、复制或记录。
- 视觉验收使用 full profile、contract v4、最终 rebuild 和独立 reviewer；功能测试通过不能替代视觉通过。
- 新增可见字符串必须可翻译，使用稳定词汇：排队中、处理中、正在停止、已完成、失败、已取消、未保存、正在保存、已保存。
- 下列提交步骤仅在执行者得到明确 Git 授权时执行；否则报告建议提交消息。

## Executable Settings Scope

| 分类 | P5-D 交付 | 运行时效果 | 明确不显示 |
|---|---|---|---|
| 常规 | 恢复上次项目、最近项目数量 | 下次启动/最近项目投影 | 自动更新、未实现的自动保存调度 |
| OCR | 模型目录、默认语言、manifest 状态 | 新 OCR 请求 | 下载、暂停、迁移、版本商店 |
| AI 翻译 | Provider ID、Endpoint、Model、超时、重试、凭据状态/替换/移除 | 新翻译请求或标注为需重启的配置 | 连接测试、动态远端模型目录 |
| 外观 | 系统/浅色/深色、界面密度、减少动画、画布背景 | 即时预览；取消离开可回滚 | 应用级文本缩放、未验证 Accent 编辑器 |
| 其余 | 不注册分类 | 无 | 模型管理、排版与字体、性能、隐私 |

新增未列入范围的设置必须同时交付 Application Port、production adapter、typed UseCase、运行时消费方、成功/失败测试与恢复入口，不能只写入 QSettings。

## File Structure

- `src/application/operation/`：任务 descriptor、snapshot、progress reporter。
- `src/application/workflow/OperationRegistry.*`：可观察任务事实源、取消与有界保留。
- `src/application/workflow/SmartTranslatePageWorkflow.*`：OCR→翻译→清除→排版编排，只返回 proposal。
- `src/application/usecase/apply/`：prepared adoption，不做 UI 或 Undo。
- `src/ui/screen/editor/EditorProcessingController.*`：GUI-thread watcher、目标替换和采用 intent。
- `src/ui/widget/AppStatusTaskStrip.*` 与 `src/ui/model/OperationListModel.*`：全局摘要和任务 Flyout。
- `src/application/usecase/settings/`：typed settings read/save/validate。
- `src/ui/screen/settings/` 与 `src/ui/widget/Settings*.{h,cpp}`：Settings Surface 和有限内容页。
- `src/ui/error/RecoveryPresenter.*`：错误去重与 `RecoveryAction` 映射。
- `.fluentqt/`、`tests/visual/`：full-profile 设计与最终证据。

---

### Task 1: 建立 P5-D full-profile 设计与证据门

**Files:**
- Create: `.fluentqt/architecture.json`
- Create: `tests/visual/p5/design-brief.json`
- Create: `tests/visual/p5/concepts/editor-stable-inspector.svg`
- Create: `tests/visual/p5/concepts/editor-drawer-inspector.svg`
- Create: `tests/visual/p5/concepts/proofreading-split.svg`
- Create: `tests/visual/p5/design-board.svg`
- Modify: `docs/plan/v0.1/phase-5-ui-editor-supplement.md`（只记录最终 human selection 证据路径与偏差；不改冻结交互契约）

**Interfaces:**
- Produces: 当前 bundled FluentQt design brief contract，`profile=full`、`author_id`、三套同内容高保真概念、human decision 与实现比较区域。

- [ ] **Step 1: 初始化 full design brief 和架构 manifest**

```powershell
python .agents/skills/build-fluentqt-gui/scripts/init_project_structure.py --project-root . --application MangaTranslator --language cpp --profile full --source-root src --tests-root tests
python .agents/skills/build-fluentqt-gui/scripts/init_design_brief.py --application MangaTranslator --recipe document-workbench --profile full --author-id p5-implementation --output tests/visual/p5/design-brief.json
```

若脚本实际路径随 skill 安装位置不同，执行者先解析 `.agents/skills/build-fluentqt-gui/`；不复制脚本进仓库。

- [ ] **Step 2: 用同一 representative fixture 完成三套概念**

固定内容：1200×1800 漫画页、12 个 Region、1 个选中、2 个多选、OCR 运行 72%、1 条模型缺失错误、Settings AI 翻译页。每套包含 Light/Dark、normal/narrow，且结构差异对应 supplement §1.2 的三方案。

- [ ] **Step 3: 渲染 board 并验证 concepts-ready**

```powershell
python .agents/skills/build-fluentqt-gui/scripts/render_design_board.py tests/visual/p5/design-brief.json --output tests/visual/p5/design-board.svg
python .agents/skills/build-fluentqt-gui/scripts/validate_design_brief.py --stage concepts tests/visual/p5/design-brief.json
```

Expected: `CONCEPTS READY`。此结果只允许产品负责人评审，不允许实现者自行选择。

- [ ] **Step 4: 暂停并取得 human selection**

向产品负责人展示 board 与三个原始 comp。把选择、理由、拒绝理由和允许适配写回 brief；如果选择仍为“画布优先 + 稳定 Inspector”，记录它与 2026-08-30 决策一致，而不是伪造此前已查看这些 comp。

- [ ] **Step 5: 验证 approved brief 和架构**

```powershell
python .agents/skills/build-fluentqt-gui/scripts/validate_design_brief.py tests/visual/p5/design-brief.json
python .agents/skills/build-fluentqt-gui/scripts/validate_project_structure.py --project-root . --strict
```

Expected: `PASS`。任何失败是进入可见 P5-D Surface 实现的阻塞项。

- [ ] **Step 6: Commit（仅在获授权时）**

```bash
git add .fluentqt tests/visual/p5 docs/plan/v0.1/phase-5-ui-editor-supplement.md
git commit -m "docs(ui): establish p5 visual acceptance baseline"
```

---

### Task 2: 将 OperationRegistry 扩展为可观察、有界的任务事实源

**Files:**
- Create: `src/application/operation/OperationDescriptor.h`
- Create: `src/application/operation/OperationSnapshot.h`
- Create: `src/application/operation/OperationProgressReporter.h`
- Modify: `src/application/operation/OperationContext.h`
- Modify: `src/application/workflow/OperationRegistry.h`
- Create: `src/application/workflow/OperationRegistry.cpp`
- Modify: `src/application/CMakeLists.txt`
- Modify: `src/services/execution/BoundedTaskExecutor.h`
- Test: `tests/unit/application/OperationRegistryTest.cpp`
- Test: `tests/unit/services/BoundedTaskExecutorTest.cpp`

**Interfaces:**

```cpp
enum class OperationState { Queued, Running, Cancelling, Completed, Failed, Cancelled };

struct OperationDescriptor {
    OperationStage stage;
    domain::ProjectId projectId;
    domain::PageId pageId;
    std::optional<domain::RegionId> regionId;
    QString displayTarget;
    bool cancellable{true};
};

struct OperationSnapshot {
    OperationId id;
    OperationDescriptor descriptor;
    OperationState state;
    qint64 completed{0};
    qint64 total{0};
    QString statusText;
    std::optional<ApplicationError> error;
};
```

- Produces `OperationRegistry::createOperation(descriptor, sessionGeneration, viewGeneration)`、`snapshot()`、`cancelOperation()`、`cancelProject()`、`markFinished()` 和 Qt Signal `operationsChanged(QVector<OperationSnapshot>)`。
- 最多保留 32 条 settled 摘要或 10 分钟，以先到者为准；active operation 不受 settled 上限删除。

- [ ] **Step 1: 写线程安全与状态序列失败测试**

覆盖 Queued→Running→Completed、取消 queued/running、失败安全错误、单调进度、同 operation 跨线程发布、32 条保留、项目取消不影响其他项目。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_application
```

- [ ] **Step 3: 实现 reporter 与 GUI-thread publication**

`OperationProgressReporter` 只持有 Registry shared state 的 weak 引用；worker 更新受 mutex 保护，再用 context-bound `QMetaObject::invokeMethod` 合并到 Registry owning thread。每 50 ms 最多发一次中间进度，final/error/cancel 立即发。

- [ ] **Step 4: 接入 BoundedTaskExecutor**

executor 创建的 `TaskProgress` 同时更新 `QPromise` 和 `context.progressReporter`。入队后标记 Queued，实际 run 前标记 Running，所有完成路径只调用一次 `markFinished`。

- [ ] **Step 5: 运行 Application/Services 测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_(application|services)"
```

- [ ] **Step 6: Commit（仅在获授权时）**

```bash
git add src/application/operation src/application/workflow src/application/CMakeLists.txt src/services/execution tests/unit/application tests/unit/services
git commit -m "feat(application): publish observable operation progress"
```

---

### Task 3: 组合处理端口并实现只产出 proposal 的智能翻译 Workflow

**Files:**
- Create: `src/application/workflow/SmartTranslatePageWorkflow.h`
- Create: `src/application/workflow/SmartTranslatePageWorkflow.cpp`
- Modify: `src/application/usecase/processing/ProcessingUseCases.h`
- Modify: `src/application/CMakeLists.txt`
- Modify: `src/app/ApplicationContext.h`
- Modify: `src/app/ApplicationContext.cpp`
- Test: `tests/unit/application/SmartTranslatePageWorkflowTest.cpp`
- Test: `tests/unit/app/application_context_test.cpp`

**Interfaces:**
- ApplicationContext exposes injected/production `IOcrEngine`、`ITranslator`、`IInpainter`、`ITextLayouter`、`IDerivedAssetStore`、`ICredentialStore` and executor ownership without exposing concrete adapters to UI。
- Produces `PageProcessingProposal` containing per-stage result, target identity/revision snapshot and skipped reason; it never mutates `ComicProject`。

```cpp
struct RegionProcessingTarget {
    domain::RegionId regionId;
    domain::RectGeometry geometry;
    QString sourceText;
    QString effectiveTranslation;
    quint64 contentRevision{0};
    bool hasManualTranslation{false};
};

struct PageProcessingProposal {
    domain::ProjectId projectId;
    domain::PageId pageId;
    OperationId rootOperationId;
    QVector<OcrResponse> ocr;
    std::optional<TranslationResponse> translation;
    QVector<ClearResponse> clear;
    QVector<LayoutResponse> layout;
    QVector<ProcessingIssue> issues;
};
```

- [ ] **Step 1: 写 Workflow 失败测试**

覆盖无 Region 不启动、OCR→Translate→Clear→Layout 顺序、阶段失败停止依赖阶段、取消、manual override 默认跳过、部分成功保留、切页不取消、session generation 变化丢弃。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_application
```

- [ ] **Step 3: 扩展 Processing UseCase 的 descriptor 参数**

每个 UseCase 调用 `createOperation(OperationDescriptor, ...)`；descriptor 使用安全页码/Region 序号，不含漫画正文、完整路径或 Secret。

- [ ] **Step 4: 实现 Workflow**

Workflow 串联 focused UseCase/Future，保留已完成 proposal；失败时返回带 `issues` 的成功 proposal，只有根级输入/依赖错误才返回 failed `Result`。用户可从失败 stage 以新的 operationId 重试。

- [ ] **Step 5: 完成 ApplicationContext production/test wiring**

生产构造按 adapters→UseCases→ViewModels 顺序；测试构造接受 fake ports。shutdown 顺序：停止接收 → `cancelProject/all` → executor bounded shutdown → adapter teardown。

- [ ] **Step 6: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_(application|app)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/application/workflow src/application/usecase/processing src/application/CMakeLists.txt src/app tests/unit/application tests/unit/app
git commit -m "feat(application): orchestrate page processing proposals"
```

---

### Task 4: 准备并采用 OCR/翻译/清除/排版结果到项目 UndoStack

**Files:**
- Create: `src/application/usecase/apply/PrepareProcessingAdoptionUseCases.h`
- Create: `src/application/usecase/apply/PrepareProcessingAdoptionUseCases.cpp`
- Modify: `src/application/usecase/apply/ApplyUseCases.h`
- Modify: `src/application/CMakeLists.txt`
- Create: `src/ui/command/ProcessingAdoptionCommands.h`
- Create: `src/ui/command/ProcessingAdoptionCommands.cpp`
- Modify: `src/ui/screen/editor/EditorViewModel.h`
- Modify: `src/ui/screen/editor/EditorViewModel.cpp`
- Test: `tests/unit/application/PrepareProcessingAdoptionTest.cpp`
- Test: `tests/unit/ui/processing_adoption_commands_test.cpp`

**Interfaces:**

```cpp
struct PreparedRegionChange {
    domain::PageId pageId;
    domain::RegionId regionId;
    domain::PageRegionEdit before;
    domain::PageRegionEdit after;
};

struct PreparedProcessingAdoption {
    OperationStage stage;
    QVector<PreparedRegionChange> changes;
    QVector<ProcessingIssue> skipped;
    std::optional<PreparedClearChange> clearAssetChange;
};
```

- Prepare UseCases 校验 project/session/target/contentRevision 并计算 before/after，但 OCR/translation/layout 不修改 Domain；Clear 继续先原子写 immutable asset，再返回 prepared change。
- `ProcessingAdoptionCommand` 使用 P5-C `EditorCommandContext`；first redo、undo、redo 都只同步应用已准备值。

- [ ] **Step 1: 写 preparation 失败测试**

覆盖 ID mismatch、stale generation、deleted Region、content revision conflict、manual source/translation override、translation unknown/missing/duplicate IDs、layout warning/overflow、clear prepare I/O failure。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_application
```

- [ ] **Step 3: 实现 prepare UseCases**

批量翻译按 Region 分组成功/skip；成功 changes 作为一条宏采用命令。manual override 默认进入 skipped，只有用户确认的 `ReplaceManualPolicy::Replace` 才生成 after。

- [ ] **Step 4: 写命令失败测试**

覆盖采用一次进栈、Undo 恢复完整 before、Redo 不重新调用网络/I/O、Clear redo 不再写资产、保存后历史保留、跨页采用命令导航目标页。

- [ ] **Step 5: 实现命令与 EditorViewModel 接口**

```cpp
Result<void> adoptProcessingProposal(const PreparedProcessingAdoption& prepared);
void rejectProcessingProposal(OperationId operationId);
```

push 前在同一 GUI event 再做最终 snapshot 校验；push 后任何命令失败设置 `undoHistoryFaulted` 并停止重放，不能静默移动栈游标。

- [ ] **Step 6: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_(application|ui_processing_adoption)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/application/usecase/apply src/application/CMakeLists.txt src/ui/command src/ui/screen/editor tests/unit/application tests/unit/ui
git commit -m "feat(ui): adopt processing results through project undo"
```

---

### Task 5: Editor 处理命令、proposal 状态与页面级反馈

**Files:**
- Create: `src/ui/screen/editor/EditorProcessingController.h`
- Create: `src/ui/screen/editor/EditorProcessingController.cpp`
- Modify: `src/ui/screen/editor/EditorState.h`
- Modify: `src/ui/screen/editor/EditorViewModel.h`
- Modify: `src/ui/screen/editor/EditorViewModel.cpp`
- Modify: `src/ui/widget/EditorCommandBar.cpp`
- Modify: `src/ui/widget/InspectorPanel.cpp`
- Modify: `src/ui/widget/PageThumbnailList.cpp`
- Test: `tests/unit/ui/editor_processing_controller_test.cpp`
- Test: `tests/unit/ui/editor_page_test.cpp`

**Interfaces:**
- Commands: OCR selected、Translate page、Clear selected、Layout selected、Smart Translate page、Cancel operation、Retry failed stage、Adopt、Reject。
- `EditorProcessingState` 只含 operation IDs、target IDs、stage/status、safe issue summaries 和 proposal availability，不保存 raw result images。

- [ ] **Step 1: 写状态机失败测试**

覆盖当前页无 Region 的画布提示、同目标新任务取消旧 operation、切页后任务继续、关闭项目取消、结果 conflict 显示“有新的 AI 译文可比较”、adopt/reject 清 proposal。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_editor_processing
```

- [ ] **Step 3: 实现 Controller watcher 所有权**

Controller 在 GUI 线程持有 `QFutureWatcher`，key 为 project/page/region/stage；callback 先检查 receiver、session generation、operationId 与目标 snapshot。`QImage` 结果只在 GUI 线程转 `QPixmap`，proposal data 由 controller 有界持有，项目关闭全部释放。

- [ ] **Step 4: 接入 CommandBar 与 Inspector**

Smart Translate 为唯一 Accent action；没有 Region 时禁用并提供明确原因。单阶段命令是次要动作。运行时同位置显示 cancel，不同时保留同权重 start/cancel。

- [ ] **Step 5: 接入页面行/Region 状态**

页面列表和 Region overlay 只显示受影响项的 Queued/Running/NeedsReview/Failed；不在多个区域重复同一错误全文。状态同时有图标/形状/文本可访问描述。

- [ ] **Step 6: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_(editor_processing|editor_page)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/ui/screen/editor src/ui/widget tests/unit/ui
git commit -m "feat(ui): add editor processing and review workflow"
```

---

### Task 6: AppStatusTaskStrip 与任务 Flyout

**Files:**
- Create: `src/ui/model/OperationListModel.h`
- Create: `src/ui/model/OperationListModel.cpp`
- Create: `src/ui/delegate/OperationRowDelegate.h`
- Create: `src/ui/delegate/OperationRowDelegate.cpp`
- Create: `src/ui/widget/AppStatusTaskStrip.h`
- Create: `src/ui/widget/AppStatusTaskStrip.cpp`
- Modify: `src/ui/window/MainWindow.h`
- Modify: `src/ui/window/MainWindow.cpp`
- Modify: `src/app/Application.cpp`
- Test: `tests/unit/ui/operation_list_model_test.cpp`
- Test: `tests/unit/ui/app_status_task_strip_test.cpp`

**Interfaces:**
- Strip consumes `QVector<OperationSnapshot>` 和 `DocumentStatusSnapshot`。
- Signals: `operationActivated(OperationId, ProjectId, PageId)`、`cancelRequested(OperationId)`、`saveFailureDetailsRequested()`。

- [ ] **Step 1: 写优先级与增量模型失败测试**

覆盖 SaveFailed>Dirty>cancellable task>offline/model missing>saved/environment；多个任务摘要；目标行 `dataChanged` 而非 modelReset；完成摘要 1–2 秒后收起；高频进度不重复播报。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_app_status_task_strip
```

- [ ] **Step 3: 实现三段式 Strip**

基准高度 32 px；Document/Active Task/Environment 共享一个 host，不做三张卡。`>=1008` 完整，`720–1007` 环境缩图标，`<720` 只保留最高优先级和菜单按钮；200% 文本允许增长至 40–48 px。

- [ ] **Step 4: 实现有界任务 Flyout**

使用 ListView+model+delegate，不使用 per-row widget。Flyout 按需创建，关闭后 `deleteLater()` 并清 `QPointer`；重复开关 50 次后 live instance 回到 baseline。

- [ ] **Step 5: 接入全局导航**

激活任务时按 ProjectId 校验当前会话，再导航 Editor/PageId；任务不会因导航改变 target。取消调用 Registry；关键错误在所属页面打开 InfoBar。

- [ ] **Step 6: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_(operation_list_model|app_status_task_strip|main_window)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/ui/model src/ui/delegate src/ui/widget/AppStatusTaskStrip.* src/ui/window src/app/Application.cpp tests/unit/ui
git commit -m "feat(ui): add global operation status strip"
```

---

### Task 7: Typed App Settings、OCR 模型校验和凭据组合根

**Files:**
- Create: `src/application/dto/AppSettingsDto.h`
- Create: `src/application/ports/IOcrModelValidator.h`
- Create: `src/application/usecase/settings/AppSettingsUseCases.h`
- Create: `src/application/usecase/settings/AppSettingsUseCases.cpp`
- Create: `src/services/ocr/OcrModelValidator.h`
- Create: `src/services/ocr/OcrModelValidator.cpp`
- Modify: `src/application/CMakeLists.txt`
- Modify: `src/services/CMakeLists.txt`
- Modify: `src/app/ApplicationContext.h`
- Modify: `src/app/ApplicationContext.cpp`
- Test: `tests/unit/application/AppSettingsUseCasesTest.cpp`
- Test: `tests/unit/services/OcrModelValidatorTest.cpp`

**Interfaces:**

```cpp
enum class AppTheme { System, Light, Dark };
enum class InterfaceDensity { Comfortable, Compact };

struct AppSettingsSnapshot {
    bool restoreLastProject{true};
    int recentProjectLimit{8};
    QString ocrModelDirectory;
    QString ocrLanguage{QStringLiteral("ja")};
    QString providerId{QStringLiteral("openai-compatible")};
    QString endpoint;
    QString modelName;
    int timeoutSeconds{60};
    int retryCount{2};
    AppTheme theme{AppTheme::System};
    InterfaceDensity density{InterfaceDensity::Compact};
    bool reduceMotion{false};
    QString canvasBackground{QStringLiteral("#202124")};
};
```

- `LoadAppSettingsUseCase` supplies defaults and validates/clamps persisted values。
- `SaveAppSettingsUseCase` validates whole snapshot before writing namespaced keys；partial write failure cannot occur because current store is synchronous/infallible, but invalid input writes nothing。
- `IOcrModelValidator::validate(directory) -> Result<OcrModelStatus>`，services adapter wraps existing manifest validation。

- [ ] **Step 1: 写 typed settings 失败测试**

覆盖 defaults、valid roundtrip、unknown enum、invalid endpoint/timeout/retry/recent limit/color、empty model directory、写入 key 清单不含 API Key。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_application
```

- [ ] **Step 3: 实现 typed UseCases**

UI 永远只看到 `AppSettingsSnapshot`，不拼 key。Provider setting 明确标注“新请求使用；若 adapter 不能热重载则需要重启”，由 ApplicationContext 的实际消费方式决定最终文案。

- [ ] **Step 4: 写并实现 OCR 模型 validator contract test**

覆盖 missing directory、invalid manifest、incompatible runtime、valid model；safeMessage 不含完整敏感路径，diagnostic 可供日志。

- [ ] **Step 5: 扩展 ApplicationContext**

生产注入 `WindowsCredentialStore` 和 `OcrModelValidator`；测试构造接受 fake。UI 只接收 interfaces/usecases，不能向下 cast concrete adapter。

- [ ] **Step 6: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_(application|services|app)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/application/dto src/application/ports/IOcrModelValidator.h src/application/usecase/settings src/services/ocr/OcrModelValidator.* src/app tests/unit
git commit -m "feat(application): add typed executable app settings"
```

---

### Task 8: Settings ViewModel、Surface、内容页与安全凭据编辑

**Files:**
- Create: `src/ui/screen/settings/SettingsState.h`
- Create: `src/ui/screen/settings/SettingsViewModel.h`
- Create: `src/ui/screen/settings/SettingsViewModel.cpp`
- Create: `src/ui/screen/settings/SettingsPage.h`
- Create: `src/ui/screen/settings/SettingsPage.cpp`
- Create: `src/ui/widget/SettingsGroup.h`
- Create: `src/ui/widget/SettingsGroup.cpp`
- Create: `src/ui/widget/SettingsRow.h`
- Create: `src/ui/widget/SettingsRow.cpp`
- Modify: `src/app/Application.cpp`
- Modify: `src/ui/CMakeLists.txt`
- Test: `tests/unit/ui/settings_view_model_test.cpp`
- Test: `tests/unit/ui/settings_page_test.cpp`
- Test: `tests/unit/ui/settings_lifetime_test.cpp`

**Interfaces:**
- Categories: `General`、`Ocr`、`AiTranslation`、`Appearance`；只注册这四项。
- State 保存 persisted snapshot、draft、field errors、Dirty categories、credential status、model status、当前 category 和滚动位置；不保存 Secret。
- Secret 仅存在短生命周期 `SecretBytes` 提交参数，提交后立即清零。

- [ ] **Step 1: 写 ViewModel 失败测试**

覆盖 load、跨分类 draft、valid save、invalid field focus request、credential status 不读取 key、replace cancel 保留旧 key、clear confirmation、theme preview rollback、external settings conflict。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_settings_view_model
```

- [ ] **Step 3: 实现 SettingsViewModel**

`Ctrl+S` 保存所有 Dirty categories。切分类不提示；离开 Surface 发一次 `leaveDecisionRequested`。保存成功更新 persisted snapshot、清 Dirty 并发短暂 `settingsSaved`；失败保留 draft 并导航/聚焦第一个错误字段。

- [ ] **Step 4: 实现 SettingsGroup/SettingsRow**

Group 是单一 surface，行之间 divider，不做卡片套卡片。内容 `>=680` 横向、`<680` 垂直；模式切换不重建输入或丢焦点。Error 行按内容增长，200% 文本不重叠。

- [ ] **Step 5: 实现 SettingsPage 和独立 Navigation Surface**

TitleBar 只保留应用名和 Back；设置分类 pane 不含重复“设置”项/返回项。内容列 max width 1100 px，宽屏 32 px 对称 margin，窄屏 20 px；分类滚动位置按会话恢复。

- [ ] **Step 6: 实现凭据与主题行为**

凭据已配置只显示状态和“更新密钥/移除密钥”。移除使用按需创建、close 后销毁的 `ContentDialog`。主题即时预览通过 `themePreviewRequested` 交给 Application；取消离开恢复 persisted theme，保留焦点。

- [ ] **Step 7: 注册 Settings 路由并运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_settings"
```

- [ ] **Step 8: Commit（仅在获授权时）**

```bash
git add src/ui/screen/settings src/ui/widget/SettingsGroup.* src/ui/widget/SettingsRow.* src/ui/CMakeLists.txt src/app/Application.cpp tests/unit/ui
git commit -m "feat(ui): add executable settings surface"
```

---

### Task 9: RecoveryAction Presenter、去重节流与所有可执行入口

**Files:**
- Create: `src/ui/error/RecoveryPresenter.h`
- Create: `src/ui/error/RecoveryPresenter.cpp`
- Modify: `src/ui/screen/home/HomeState.h`
- Modify: `src/ui/screen/home/HomeViewModel.cpp`
- Modify: `src/ui/screen/import/ImportViewModel.cpp`
- Modify: `src/ui/screen/editor/EditorViewModel.cpp`
- Modify: `src/ui/screen/settings/SettingsViewModel.cpp`
- Modify: `src/app/Application.cpp`
- Test: `tests/unit/ui/recovery_presenter_test.cpp`
- Test: `tests/integration/ui/recovery_routes_test.cpp`

**Interfaces:**

```cpp
struct RecoveryPresentation {
    QString code;
    QString safeMessage;
    QString actionText;
    application::RecoveryAction action;
    QString targetKey;
    int occurrenceCount{1};
    bool blocking{false};
};
```

- `RecoveryPresenter::present(error, targetKey, now)` 合并 3 秒内相同 `code+targetKey`；Cancelled/stale 返回 `std::nullopt`。

- [ ] **Step 1: 写每个 enum 分支失败测试**

覆盖 Retry、OpenSettings、Reauthenticate、LocateModel、ChoosePath、FreeDiskSpace、None、Cancelled；断言 `diagnostic` 从不进入 presentation。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_recovery_presenter
```

- [ ] **Step 3: 实现映射与去重**

映射严格遵循 supplement §10。Presenter 只决定安全文案/动作，不直接打开 dialog、folder 或 route。

- [ ] **Step 4: 接入 Application shell effects**

`Retry` 回到原 owning ViewModel 创建新 operationId；`OpenSettings/Reauthenticate/LocateModel` 导航具体 Settings category/field；`ChoosePath` 重开 picker 并保留表单；`FreeDiskSpace` 显示安全目录名与所需空间并提供“查看位置”；`None` 无动作。

- [ ] **Step 5: 关闭 P5-A/P5-B 明确遗留**

实现 Home 卡片“在文件夹中显示”、查看全部、打开中 Progress 状态；Import warnings/recoverySource 呈现；导航 Accent/过渡、页面列表 responsive overlay 与 pane width persistence。导入异步来源扫描和项目目录删除仍因缺 Port 不显示/不虚构。

- [ ] **Step 6: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_(recovery|home|import|navigation)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/ui/error src/ui/screen src/app/Application.cpp tests/unit/ui tests/integration/ui
git commit -m "feat(ui): add executable error recovery routes"
```

---

### Task 10: 响应式、无障碍、主题与产品文案收口

**Files:**
- Modify: `src/ui/screen/editor/EditorPage.cpp`
- Modify: `src/ui/screen/import/ImportPage.cpp`
- Modify: `src/ui/screen/home/HomePage.cpp`
- Modify: `src/ui/screen/settings/SettingsPage.cpp`
- Modify: `src/ui/widget/AppStatusTaskStrip.cpp`
- Modify: `src/ui/widget/InspectorPanel.cpp`
- Modify: `src/ui/manga/MangaCanvas.cpp`
- Create: `tests/visual/p5/action-inventory.md`
- Create: `tests/visual/p5/copy-inventory.md`
- Test: `tests/unit/ui/responsive_layout_test.cpp`
- Test: `tests/unit/ui/accessibility_contract_test.cpp`

**Interfaces:**
- Breakpoints: Wide `>=1440`、Standard `1200–1439`、Compact `1008–1199`、Narrow `720–1007`、Minimum `<720`；Flat height `<560`。
- Escape consumption: IME → text session → canvas gesture → Flyout → Overlay → navigation。

- [ ] **Step 1: 写响应式与动作所有权失败测试**

覆盖每个 breakpoint、互斥 Page/Inspector drawer、Overlay 不回写 desktop width、主 Smart Translate/Save/Undo 可达、同一 action 无同时可见重复入口。

- [ ] **Step 2: 实现 responsive composition**

Wide/Standard 三栏；Compact 页面栏 80–96 px；Narrow/Minimum 页面与 Inspector 互斥 drawer，Canvas 保持 viewport center，不自动 fit；Flat height 压缩 CommandBar/Strip 但不裁动作。

- [ ] **Step 3: 完成 keyboard/accessibility**

F6 pane cycle、完整 Tab order、Region virtual children、Tooltip/accessible name、状态 live announcement、200% text、高对比度和 reduce motion。无法自动验证 native IME 的环境必须标记 unverified，不能记 pass。

- [ ] **Step 4: 审计产品文案**

`copy-inventory.md` 记录 audience=`manga translators/editors`、voice=`direct, calm, technical`、zh-CN/CJK strategy、稳定状态词和禁用 AI narration/protocol labels。逐项检查 empty/loading/error/completion/dialog/tooltip。

- [ ] **Step 5: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_(responsive|accessibility)"
```

- [ ] **Step 6: Commit（仅在获授权时）**

```bash
git add src/ui tests/unit/ui tests/visual/p5/action-inventory.md tests/visual/p5/copy-inventory.md
git commit -m "fix(ui): finish responsive and accessible p5 surfaces"
```

---

### Task 11: 工程验收、视觉 inspect-fix-recapture 与独立评审

**Files:**
- Create: `tests/visual/p5/visual-evidence.json`
- Create: `tests/visual/p5/visual-review.html`
- Create: `tests/visual/p5/acceptance.md`
- Modify: `docs/plan/v0.1/phase-5-ui-editor.md`（只有证据全部通过后更新 P5 状态/checkbox）

**Interfaces:**
- Produces contract v4 evidence, final executable identity, Gallery/VisualCheck references, Inspector quality report or compatibility boundary, independent reviewer verdict。

- [ ] **Step 1: 格式化、配置、构建和全测**

```powershell
prek run --all-files
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/configure.ps1 -Preset local-test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test
```

记录实际通过数量和任何未运行边界；不得写“应当通过”。

- [ ] **Step 2: 运行确定性性能/生命周期门**

覆盖 500 页惰性缩略图、300 Region paint hot path、任务 queue/cancel/retention、50 次 Flyout/Dialog close baseline、settings theme/width cache invalidation。固定发布机另记录 >30 fps 与热切页 <300 ms。

- [ ] **Step 3: 初始化 visual evidence v4**

```powershell
python .agents/skills/build-fluentqt-gui/scripts/init_visual_evidence.py --brief tests/visual/p5/design-brief.json --reviewed-build build/local-test/src/app/MangaTranslator.exe --platform "Windows, native scale, Qt 6.11.1, Debug" --output tests/visual/p5/visual-evidence.json
```

Reviewed build 路径以 preset 实际产物为准并写入绝对运行时路径；提交 artifact 不保留开发者绝对路径，仓库版本改用相对 evidence reference 或生成时替换。

- [ ] **Step 4: 驱动真实 final build 并采集 mandatory states/regions**

状态至少：normal-light/dark、narrow、minimum、empty、0/1/2/8+/300 Region density、long-localized、input single/max、scroll-end、async-settled、selected/focus/disabled、drawer/flyout/dialog/toast、IME preedit。区域至少：title bar、navigation/page list、canvas、CommandBar/Inspector headers、StatusStrip、scroll terminals、overlays。

- [ ] **Step 5: 执行 inspect-fix-rebuild-recapture loop**

与相同 platform/theme/scale 的 FluentQt Gallery/VisualCheck 并排；测量 panel inset、row height、indicator/icon/text slots、baseline/optical centers、footer height、last readable pixel。任何 clipping、overlap、stale palette/overlay、wrong density 或 open major finding 必须修复并重新 capture 同一状态。

- [ ] **Step 6: 生成 review board 并交独立 reviewer**

```powershell
python .agents/skills/build-fluentqt-gui/scripts/render_visual_review.py tests/visual/p5/visual-evidence.json --output tests/visual/p5/visual-review.html
```

reviewer 必须不同于 `author_id`，只接收 raw brief、board 和 final build，不接收实现者期望 verdict。九项评分均 >=4/5 且无 open blocker/major 才可 pass。

- [ ] **Step 7: 验证 current contract 和最终结构**

```powershell
python .agents/skills/build-fluentqt-gui/scripts/validate_visual_evidence.py --require-current tests/visual/p5/visual-evidence.json
python .agents/skills/build-fluentqt-gui/scripts/validate_project_structure.py --project-root . --strict
```

若 FluentQt 提供 diagnostics Inspector，运行 final settled surface report；否则在 `acceptance.md` 记录锁定 revision 不提供该 API，不复制私有规则。

- [ ] **Step 8: 更新 Phase 5 状态并 Commit（仅在获授权时）**

只有 engineering 与 visual 两门都通过才勾选 Phase 5 项目和退出条件。

```bash
git add tests/visual/p5 docs/plan/v0.1/phase-5-ui-editor.md
git commit -m "test(ui): record p5 engineering and visual acceptance"
```

## P5-D Exit Gate

- OCR、翻译、清除、排版和智能翻译可运行、取消、重试、跨页继续并按稳定目标合并。
- 处理结果在用户采用前不覆盖 Domain；采用、Undo、Redo 不重新执行网络、模型或派生资产 I/O。
- `AppStatusTaskStrip` 同时可信地表达 Dirty/保存、活动任务和必要环境状态；多任务可发现并可导航回目标。
- Settings 只显示四个可执行分类；API Key 不回显、不持久化到 QSettings、不进入日志或可访问 Value。
- 每个 `RecoveryAction` 有测试过的 UI 路径；diagnostic 不可见，同类错误去重节流。
- Light/Dark、五档宽度、flat height、高 DPI、高对比度、reduce motion、IME 和重要错误/空/溢出状态有最终 build 证据。
- full-profile visual evidence contract v4 与 project structure validator 通过，独立 reviewer 九项评分均至少 4/5，无 open blocker/major。
