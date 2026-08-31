# P6：纵向切片集成 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用真实 Repository、Importer、PageImageLoader、DerivedAssetStore、Clear/Layout/Exporter 与生产 Composition Root 接通并验证 V0.1 端到端主线：补齐唯一对象图、Process/Project session/View/Operation 四级生命周期与确定性关闭协议，以按场景拆分的真实集成测试（P6-E01..E05 + 持久化/会话/恢复矩阵）替换 `tests/integration` 占位测试。

**Architecture:** 沿用 P5 已建立的 `Application`/`ApplicationContext` 骨架并扩展同一对象图；不创建第二个 Context、Service Locator 或全局单例。Project session 的拥有者是 P5-C 交付的 `EditorViewModel`（持有 `OpenedProjectSession`、`QUndoStack`、projectSessionGeneration 与项目任务取消钩子），P6 不新建“会话控制器”对象，只在 app 层补窗口关闭/进程退出到该会话协议的编排。跨层契约缺口（导出快照构建、翻译配置热生效、关闭编排注入）回到 owning layer 做最小新增：`ExportSnapshotBuilder` 放 `ui/screen/editor`（`ui -> application + rendering` 方向合法），翻译配置快照放 `network`，测试注入构造放 `app`。

**Tech Stack:** C++23、Qt 6.11.1（Core/Gui/Widgets/Network/Concurrent/Test）、FluentQt v1.7.4 公共 C++ API、QtWindowKit 外壳、ONNX Runtime（仅 `p4-model` 作业）、CMake preset `local-test`、Qt Test（offscreen）。

**Spec:** [`phase-6-integration.md`](phase-6-integration.md)（绑定规格，§2 生命周期/关闭、§3 场景、§5 恢复矩阵、§6 测试结构、§8 清单）。前置执行计划（只消费其 Produces/Exit Gate，不重做）：[`phase-5-c-region-inspector-plan.md`](phase-5-c-region-inspector-plan.md)、[`phase-5-d-processing-settings-acceptance-plan.md`](phase-5-d-processing-settings-acceptance-plan.md)。已核实符号清单：`.superpowers/sdd/phase-5-a-mvvm-shell-plan/verified-symbols.md`（本计划不重复核实其覆盖的 FluentQt/QWK 符号）。

## Global Constraints

每个任务的执行者都必须满足以下约束（相当于每条 Step 的隐含验收）：

- 先完成 P5-C 与 P5-D；本计划只消费其承诺接口并逐一引用任务号（如 `P5-C Task 8`）。若落地接口与计划文档不一致，以实际头文件为准并在偏差中记录，但不在这里重新设计 P5-C/P5-D 的工作。
- C++23、UTF-8/LF、4 空格缩进、英文标识符与代码注释；只格式化本计划触及的文件。提交钩子（prek）改写后必须 re-add 再提交。
- Qt 元对象语法统一使用 `Q_SIGNALS`、`Q_SLOTS`、`Q_EMIT`；禁止 `signals`、`slots`、`emit`。Qt Test 类用 `private Q_SLOTS:` 声明测试函数。**所有新增测试目标一律 `target_compile_definitions(<t> PRIVATE QT_NO_KEYWORDS ...)`**。
- Composition Root 红线：不创建第二套对象图；`ApplicationContext` 只负责构造、注入与关闭，不得塞入业务判断、路径规则或线程补丁。发现契约缺口回到 owning layer 最小修正。
- 每个场景固定规格 §6.1 的真实/替身边界，禁止“全 Fake 算集成”或“引入不可重复的真实外部依赖”。边界表写在各场景任务的 Interfaces 节。
- 所有异步等待用 signal/event-loop 驱动 + 明确 timeout（统一 `manga::test::kAwaitTimeoutMs`），禁止无条件 sleep；测试不读取用户 QSettings、Credential Manager、模型缓存或文档，一律用临时目录与注入替身。
- 像素与浮点比较使用测试内文档化容差；JPEG 不做逐字节相等断言。
- 固定图片 fixture 必须在 `tests/fixtures/README.md` 登记来源与许可证；禁止用户漫画或来源不明素材进仓库。
- 普通离线套件不得依赖真实 OCR 模型、真实凭据或公共网络；真实 ONNX 推理由既有 CI `p4-model` 作业（`.github/workflows/ci.yml`）单独覆盖，本计划不修改该作业。
- 构建与测试（Git Bash 中调 PowerShell 脚本，细节见 `.agents/skills/manga-translator-build/SKILL.md`）：
  - 配置：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Configure`
  - 构建：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test`（可加 `-Target <target>`）
  - 聚焦测试：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex <regex>`
  - 全量：`test.ps1 -Preset local-test`（不带 `-Regex`）
  - 日志：`build/local-test/build.log` / `ctest.log` / `Testing/Temporary/LastTest.log`。
- 一个测试可执行文件只有一个 `QTEST_MAIN`。新集成测试目标命名 `manga_tests_integration_<scenario>`（沿用 `manga_tests_ui_<name>` 既有先例），统一被 `-R manga_tests_integration` 匹配；`ENVIRONMENT "QT_QPA_PLATFORM=offscreen"`、`LABELS "integration"`、场景目标 `TIMEOUT 60`、其余 `TIMEOUT 30`。含 Q_OBJECT 的目标必须 `set_target_properties(<t> PROPERTIES AUTOMOC ON)`。
- 复用既有测试替身：`tests/unit/application/Fake*.h`（含 `deferred` 可控完成的 `FakeOcrEngine` 等），经 `target_include_directories(<t> PRIVATE "${PROJECT_SOURCE_DIR}/tests/unit/application")` 引用（`manga_tests_ui_import_*` 已有先例）；不得复制出第二份。
- 下列提交步骤仅在执行者得到明确 Git 授权时执行；否则在验证通过后报告建议提交消息，不创建提交。

## File Structure

- `tests/fixtures/README.md`：全部固定素材的来源/许可证清单（P6 新增）。
- `tests/fixtures/pages/`：P6 场景页图（生成脚本 + 3 张确定性生成图，签入仓库）。
- `tests/integration/IntegrationHarness.h`：有界 `awaitFuture`、临时项目根、SHA-256 树、真实 data/services 适配器栈工厂。
- `tests/integration/LoopbackHttpServer.h`：基于 `QTcpServer` 的确定性 loopback HTTP 服务器（E03 专用，模式取自 `tests/unit/network/OpenAiCompatibleTranslatorTest.cpp`）。
- `tests/integration/e01_manual_offline_test.cpp` … `e05_page_export_test.cpp`、`persistence_lifecycle_test.cpp`、`session_lifecycle_test.cpp`、`recovery_matrix_test.cpp`：按场景拆分的真实集成测试，删除 `integration_init_test.cpp`。
- `src/app/ApplicationContext.{h,cpp}`：补齐生产接线（Exporter、Ort::Env、翻译配置快照）与完整关闭序列。
- `src/app/Application.{h,cpp}`：测试注入构造重载 + 窗口关闭/进程退出的会话关闭编排。
- `src/ui/screen/editor/ExportSnapshotBuilder.{h,cpp}`：唯一新增 UI 侧契约，构建 `ExportPageSnapshot`（UI 预览与导出共用 `manga_rendering`）。
- `src/network/translation/OpenAiCompatibleTranslator.{h,cpp}`：`ProviderConfig` 改为按请求读取的共享不可变快照（最小契约新增，见 Task 2）。

---

### Task 1: 集成测试脚手架与 fixture 来源清单

**Files:**
- Create: `tests/fixtures/README.md`
- Create: `tests/fixtures/pages/generate_pages.py`
- Create: `tests/fixtures/pages/page_001.png`、`page_002.png`、`page_003.png`（脚本确定性生成后签入）
- Create: `tests/integration/IntegrationHarness.h`
- Create: `tests/integration/LoopbackHttpServer.h`

**Interfaces:**
- Produces `manga::test::kAwaitTimeoutMs`（10000）、`awaitFuture<T>(QFuture<T>, int timeoutMs) -> bool`。
- Produces `manga::test::RealDataStack`：临时目录上的真实 `ProjectIoCoordinator` + `FileSystemProjectRepository` + `FileSystemComicImporter` + `FileSystemPageImageLoader` + `FileSystemThumbnailStore` + `FileSystemDerivedAssetStore` + `FileSystemComicExporter` + `BoundedTaskExecutor` + `SolidColorInpainter` + `QtTextLayouter` + `OperationRegistry`。
- Produces `manga::test::sha256Tree(root, subdir)`、`makeTempProjectRoot(scenarioName)`。
- Produces `manga::test::LoopbackHttpServer`（队列化响应、记录请求头/body、可注入 401/429/`Retry-After`/超时静默）。

- [ ] **Step 1: 登记 fixture 清单并生成场景页图**

`tests/fixtures/README.md` 用表格登记每一类固定素材：路径、来源（生成脚本 + 签入 revision / 既有目录）、许可证（项目自有，与仓库同许可证）、用途、确定性约束。覆盖既有 `tests/fixtures/ocr/**`、`tests/fixtures/fonts/README.md`（计划态）、`tests/unit/data/fixtures/**`（`generate_fixtures.py` 生成），以及本任务新增的 `tests/fixtures/pages/`。

`generate_pages.py` 用 Pillow 以固定 seed 生成 3 张 800×1200 合成“漫画页”（白底 + 黑色边框矩形框 + 灰色文字占位条 + 少量噪声块），输出 PNG；生成参数写入文件头注释与 README。禁止下载或使用任何真实漫画素材。生成一次后签入 PNG，普通构建/测试不运行该脚本。

- [ ] **Step 2: 实现 IntegrationHarness.h**

```cpp
namespace manga::test {

inline constexpr int kAwaitTimeoutMs = 10000;

/// Event-loop-driven bounded wait. Returns false on timeout; callers must QVERIFY the result
/// before reading future.result(). No unconditional sleeps anywhere in integration tests.
template <typename T>
[[nodiscard]] bool awaitFuture(const QFuture<T>& future, int timeoutMs = kAwaitTimeoutMs) {
    if (future.isFinished()) {
        return true;
    }
    QFutureWatcher<T> watcher;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    timer.start(timeoutMs);
    loop.exec();
    return future.isFinished();
}

/// Real data/services adapters rooted at a temporary directory. The stack owns everything it
/// constructs; tests additionally hold OperationRegistry by value (P5-D Task 2 observable
/// registry is a QObject-free shared-state owner — if P5-D ships it as QObject, parent it here).
struct RealDataStack {
    QTemporaryDir tempRoot;
    std::unique_ptr<data::ProjectIoCoordinator> ioCoordinator;
    std::unique_ptr<data::FileSystemProjectRepository> repository;
    std::unique_ptr<data::FileSystemComicImporter> importer;
    std::unique_ptr<data::FileSystemPageImageLoader> pageImageLoader;
    std::unique_ptr<data::FileSystemThumbnailStore> thumbnailStore;
    std::unique_ptr<data::FileSystemDerivedAssetStore> derivedAssetStore;
    std::unique_ptr<data::FileSystemComicExporter> exporter;
    std::unique_ptr<services::BoundedTaskExecutor> executor;
    std::unique_ptr<services::SolidColorInpainter> inpainter;
    std::unique_ptr<services::QtTextLayouter> layouter;
    application::OperationRegistry operationRegistry;

    RealDataStack();   // wires the real adapters over tempRoot; declaration order = teardown order
    [[nodiscard]] QString projectRoot(const QString& name) const; // tempRoot + "/" + name
};

[[nodiscard]] QHash<QString, QString> sha256Tree(const QString& root, const QString& subdir);

} // namespace manga::test
```

构造顺序固定：`ioCoordinator` 最先声明（最后被销毁），`repository/importer/pageImageLoader/thumbnailStore/derivedAssetStore` 持有它的裸指针，`executor` 之后是依赖它的 `inpainter/layouter`。销毁即逆序，不允许在测试里手动 delete。

- [ ] **Step 3: 实现 LoopbackHttpServer.h**

从 `tests/unit/network/OpenAiCompatibleTranslatorTest.cpp` 的 `LoopbackServer` 提取模式（`QTcpServer` 监听 `127.0.0.1:0`、逐连接解析 HTTP 请求、按队列返回预置响应），扩展：响应队列入队 `{status, headers, body}`；支持“永不回复”（超时路径）；记录每次请求的 method/path/`Authorization` 头/body 供断言。只被 E03/恢复矩阵目标包含，因此只有这些目标链接 `Qt6::Network`。

- [ ] **Step 4: 构建验证脚手架可编译**

脚手架暂无独立测试目标；用 Task 4 的首个场景目标验证编译。此步只运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_integration
```

Expected: 既有占位目标仍编译通过（本任务不改 CMake）。

- [ ] **Step 5: Commit（仅在获授权时）**

```bash
git add tests/fixtures/README.md tests/fixtures/pages tests/integration/IntegrationHarness.h tests/integration/LoopbackHttpServer.h
git commit -m "test(integration): add scenario harness and fixture manifest"
```

---

### Task 2: ApplicationContext 生产组合根补齐与降级启动

**Files:**
- Modify: `src/app/ApplicationContext.h`
- Modify: `src/app/ApplicationContext.cpp`
- Modify: `src/network/translation/OpenAiCompatibleTranslator.h`
- Modify: `src/network/translation/OpenAiCompatibleTranslator.cpp`
- Test: `tests/unit/app/application_context_test.cpp`

**Interfaces:**
- Consumes: P5-D Task 3 Step 5（`ApplicationContext` 暴露注入/生产 `IOcrEngine`、`ITranslator`、`IInpainter`、`ITextLayouter`、`IDerivedAssetStore`、`ICredentialStore` 与 executor 所有权，测试构造接受 fake ports）；P5-D Task 7 Step 5（生产注入 `WindowsCredentialStore` 与 `OcrModelValidator`）；P5-D Task 7（`AppSettingsSnapshot`/`LoadAppSettingsUseCase`）。
- Produces:
  - `ApplicationContext::comicExporter() -> application::IComicExporter*`（生产为 `data::FileSystemComicExporter`，P5-D 未覆盖的唯一 Process 级端口）。
  - `ProductionAdapters` 内新增 `std::unique_ptr<Ort::Env> ocrEnv`（Process 级唯一 `Ort::Env`，`OnnxMangaOcrEngine` 以裸指针使用；声明在 engine 之前、销毁在其后）。
  - `OpenAiCompatibleTranslator` 构造改收 `std::shared_ptr<const network::ProviderConfig>`：每次请求开始时读取当前快照，设置保存后由 `ApplicationContext` 原子替换，与“每次请求读 key”的既有语义一致。若 P5-D 落地时已提供等价的热生效机制，消费之并删除本项。
- 降级契约：OCR 模型目录缺失/未配置 API Key/网络离线不构成启动失败；`OnnxMangaOcrEngine::ensureLoaded()` 惰性加载，请求时返回 `Model` 类错误（既有 `model.load_failed`/`model.incompatible` 映射）；人工编辑、保存、导出保持可用。

- [ ] **Step 1: 写失败测试**

在 `tests/unit/app/application_context_test.cpp` 新增（沿用既有测试夹具；QSettings 隔离沿用 `tests/unit/data/QSettingsStoresTest.cpp` 的 `QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir)` 先例）：

```cpp
void productionContextStartsWithoutModelCredentialOrNetwork();
// 构造生产 ApplicationContext，settings 指向不存在的模型目录、无凭据、无网络：
// 构造成功；ocrEngine()/translator()/comicExporter() 非空；
// 对 ocrEngine 发起一次 recognize 得到 Model 类错误而非崩溃。
void manualMainlinePortsAvailableWhenProcessingDegraded();
// projectRepository()/comicImporter()/pageImageLoader()/comicExporter() 全部非空可用。
void translatorReadsLatestProviderConfigSnapshot();
// 构造后替换 shared_ptr<const ProviderConfig>，下一次 translate 使用新 endpoint（fake transport 断言）。
```

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_app
```

Expected: 编译失败（`comicExporter()`、快照构造重载不存在）或新断言失败。

- [ ] **Step 3: 补齐生产接线**

`ApplicationContext()` 生产构造按规格 §2.2 顺序补齐残差：在既有 `ioCoordinator → repository/stores/importer/loader` 之后追加 `FileSystemComicExporter`（无凭据/无网络依赖）；`ocrEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "MangaTranslator")`；`OnnxMangaOcrEngine(ocrEnv.get(), executor, settings.ocrModelDirectory)`。构造顺序与 `ProductionAdapters` 成员声明顺序一致，销毁天然逆序。OCR 模型缺失只让 `ensureLoaded()` 在请求时失败，不在构造期加载权重。

- [ ] **Step 4: 翻译配置共享快照**

`OpenAiCompatibleTranslator` 增加构造重收 `std::shared_ptr<const ProviderConfig>`（保留旧构造委托给新构造）。`translate()` 在任务开始时解引用一次快照。`ApplicationContext` 持有 `std::shared_ptr<const ProviderConfig> m_providerConfig`，设置保存成功后 `std::atomic_store` 替换；并发请求继续用完旧快照，无半途切换。

- [ ] **Step 5: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_(app|network)"
```

Expected: 新用例与既有 `OpenAiCompatibleTranslatorTest` 全绿。

- [ ] **Step 6: Commit（仅在获授权时）**

```bash
git add src/app src/network/translation tests/unit/app tests/unit/network
git commit -m "feat(app): complete production composition root wiring"
```

---

### Task 3: 确定性关闭协议与进程退出路径

**Files:**
- Modify: `src/app/ApplicationContext.h`
- Modify: `src/app/ApplicationContext.cpp`
- Modify: `src/app/Application.h`
- Modify: `src/app/Application.cpp`
- Test: `tests/unit/app/application_context_test.cpp`

**Interfaces:**
- Consumes: P5-C Task 8（`EditorViewModel::requestCloseProject()`/`confirmDiscardAndClose()`/`cancelClose()`，Signals `closeDecisionRequested(ProjectCloseSummary)`/`projectClosed()`；关闭顺序：停止接受命令 → 取消 view work → 项目任务取消钩子 → 销毁 UndoStack → repository close → 释放会话）；P5-D Task 2（`OperationRegistry::cancelProject()`）与 Task 3 Step 5（registry `cancelAll`、executor bounded shutdown）；`services::BoundedTaskExecutor::shutdown()`；`network::QtHttpTransport::shutdown()`；`data::FileSystemProjectRepository::closeProject`（lease 释放，`ProjectIoCoordinator` 等待读 lease）。
- Produces:
  - `Application(int& argc, char** argv, std::unique_ptr<ApplicationContext> injectedContext)`：测试注入重载；生产 `main.cpp` 仍用无注入构造，不创建第二套对象图（同一 `ApplicationContext` 类，只是来源不同）。
  - `ApplicationContext::shutdown()` 完整序列（幂等，见 Step 3 代码）。
  - `Application` 的窗口关闭路径：拦截 MainWindow close → 委托 P5-C 会话关闭协议 → `projectClosed`（或项目本为 clean）后才进入 `m_context->shutdown()` 与 `QApplication::quit()`；取消关闭回到编辑器，不触碰会话。

- [ ] **Step 1: 写失败测试**

在 `application_context_test.cpp` 新增：

```cpp
void shutdownIsIdempotentAndRunsFullTeardown();
// 注入构造 + 可控 fake port 持有一个 deferred future；调用两次 shutdown()：
// registry 收到 cancelAll、executor shutdown 被调用恰好一次、无回调进入已销毁接收者。
void shutdownCancelsInFlightOperationsBeforeAdapterTeardown();
// fake port 在 stopToken 触发后完成 future；断言 future 以 Cancelled 收尾且发生在
// adapter 销毁之前（用 fake 析构置位 flag + QTRY_VERIFY 观察，不用 sleep）。
void windowCloseCancelKeepsContextAndSessionAlive();
// 注入 Application + 打开临时项目 + Dirty；触发关闭再取消：Application 不退出、
// EditorViewModel 会话仍可编辑保存。
```

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_app
```

Expected: 编译失败（注入构造重载、`cancelAll` 调用点缺失）或断言失败。

- [ ] **Step 3: 实现 ApplicationContext 完整关闭序列**

```cpp
void ApplicationContext::shutdown() {
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;
    // Session teardown (gate, Dirty decision, watcher/UndoStack/lease) has already run in the
    // Application layer before this point. Process-tier teardown:
    // 1. Stop intake and request cancellation of every remaining operation.
    m_operationRegistry->cancelAll();
    // 2. Bounded executor shutdown: queued tasks complete as cancelled, running tasks stop
    //    cooperatively; a single uninterruptible ONNX inference may finish but its result is
    //    discarded by the stale generation check (P7 measures its worst-case exit latency).
    if (m_production && m_production->executor) {
        m_production->executor->shutdown();
    }
    // 3. Stop the network thread; replies are aborted and destroyed on their owning thread.
    if (m_production && m_production->httpTransport) {
        m_production->httpTransport->shutdown();
    }
    // 4. Member destruction after this point is the reverse of declaration order:
    //    adapters/stores first, ProjectIoCoordinator (declared first) last.
}
```

若 P5-D 的 `OperationRegistry` 没有 `cancelAll()`（其 Task 3 Step 5 文案为 “cancelProject/all”），在 `src/application/workflow/OperationRegistry` 做最小新增：对所有 in-flight operation 的 stop source 请求停止；这是 application 层契约，不属于 ApplicationContext 业务逻辑。

- [ ] **Step 4: 实现 Application 注入构造与关闭编排**

注入重载把 `m_context` 设为传入实例（其余启动序列不变）。窗口关闭编排：MainWindow 的关闭请求先路由到 `EditorViewModel::requestCloseProject()`（P5-C）；`closeDecisionRequested` 由 P5-C 的 `ContentDialog` 处理；`projectClosed()` 或无会话时，保存非敏感窗口/Pane 设置（既有 `IAppSettingsStore` 路径），再 `m_context->shutdown()` 并退出。进程退出（`QApplication::aboutToQuit` 之外的显式路径）复用同一函数，不允许绕过会话协议直接析构。注意保持 `Application::~Application()` 既有顺序（window → context.shutdown → context → app），shutdown 幂等使重复调用安全。

- [ ] **Step 5: 运行测试**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_app"
```

Expected: `manga_tests_app` 与 `manga_tests_app_smoke`（`tests/CMakeLists.txt` 既有 offscreen 冒烟）全绿。

- [ ] **Step 6: Commit（仅在获授权时）**

```bash
git add src/app tests/unit/app
git commit -m "feat(app): add deterministic session shutdown protocol"
```

---

### Task 4: P6-E01 纯人工离线主线（最低可靠基线，替换占位测试）

**Files:**
- Create: `src/ui/screen/editor/ExportSnapshotBuilder.h`
- Create: `src/ui/screen/editor/ExportSnapshotBuilder.cpp`
- Create: `tests/integration/e01_manual_offline_test.cpp`
- Delete: `tests/integration/integration_init_test.cpp`
- Modify: `tests/integration/CMakeLists.txt`
- Modify: `src/ui/CMakeLists.txt`
- Test: `tests/unit/ui/export_snapshot_builder_test.cpp`

**Interfaces:**
- Consumes: P5-C Task 1（`OpenedProjectSession`、`EditorViewModel::openProjectSession(OpenedProjectSession)`）、Task 3/5（Region 选择与命令 intents）、Task 7（文本/样式 intents）、Task 8（`saveProject()`、`requestCloseProject()`、`confirmDiscardAndClose()`）；`CreateProjectUseCase`/`ImportImagesUseCase`/`ApplyImportResultUseCase`/`AcknowledgeImportedAssetsUseCase`/`OpenProjectUseCase`（`src/application/usecase/{project,import}`）；`ClearRegionUseCase` + `PrepareClearResultUseCase`/`ApplyPreparedClearChangeUseCase`（两阶段清除）；`ExportPagesUseCase`（`src/application/usecase/export/ExportUseCases.h`）；`domain::ComicProject::operator==`、`revision()`、`isDirty()`、`markCleanIfRevision()`。
- 真实/替身边界（规格 §6.1 E01 行）：真实 = Repository、Importer、PageImageLoader、Rendering、Clear（`SolidColorInpainter`）、Layout（`QtTextLayouter`）、Exporter；替身 = 无 OCR、无 Translator（端口置空，对应命令不可用但人工主线继续）。
- Produces `ui::screen::editor::ExportSnapshotBuilder`（唯一新增契约，归 UI 层以满足 `ui -> application + rendering` 依赖方向；`application` 不得反向依赖 `rendering`）：

```cpp
/// Builds immutable ExportPageSnapshots from the open session: source images via
/// LoadPageImageUseCase, committed derived layers via IDerivedAssetStore, and translation
/// layers rasterized with rendering::TextLayoutEngine + TextRasterizer — the same rendering
/// entry points the canvas preview uses, so preview and export share one result.
class ExportSnapshotBuilder {
public:
    ExportSnapshotBuilder(domain::ComicProject* project,
                          application::IPageImageLoader* loader,
                          application::IDerivedAssetStore* derivedStore);

    struct Request {
        QString projectRoot;
        QVector<domain::PageId> pageIds; // empty means all pages in project order
        double logicalDpi{96.0};
    };

    [[nodiscard]] QFuture<application::Result<QVector<application::ExportPageSnapshot>>>
    execute(const Request& request, const application::OperationContext& context);
};
```

- [ ] **Step 1: 写 ExportSnapshotBuilder 失败单元测试**

`tests/unit/ui/export_snapshot_builder_test.cpp`（新目标 `manga_tests_ui_export_snapshot_builder`，offscreen，TIMEOUT 15，`QT_NO_KEYWORDS`）：全部页面按项目序生成 snapshot；显式 PageId 子集保持项目页序；`outputStem` 由 `ExportPagesUseCase` 规则生成时的输入正确（pageId/尺寸/图层空值语义）；缺失 source image 返回页面级错误而不是空图；无清除/排版内容的页面产生空透明层而非失败；translation layer 与 `rendering::TextRasterizer::renderToImage` 的直接输出逐像素一致（同一引擎，容差 0）。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_export_snapshot_builder
```

Expected: 编译失败（`ExportSnapshotBuilder` 不存在）。

- [ ] **Step 3: 实现 ExportSnapshotBuilder**

逐页：加载原图（`LoadPageImageUseCase`）→ 若 `ComicPage::clearLayerAsset()` 有引用则读 Clear 层 → 对有有效 `TextLayout` 与译文的 Region 用 `TextLayoutEngine::layoutText` + `TextRasterizer::renderToImage` 光栅化到页大小 RGBA 层（sRGB、`Format_RGBA8888`，与 `ExportPageSnapshot` 契约一致）。所有 I/O 经端口 Future，worker 只传 `QImage`；builder 不读文件系统、不碰 Widget/QPixmap。

- [ ] **Step 4: 写 P6-E01 场景失败测试并删除占位**

`tests/integration/e01_manual_offline_test.cpp`（目标 `manga_tests_integration_e01_manual_offline`，链接 `manga_domain manga_application manga_services manga_data manga_rendering manga_ui Qt6::Test Qt6::Widgets`，include `tests/unit/application`，offscreen，`QT_NO_KEYWORDS`，TIMEOUT 60）。同步删除 `integration_init_test.cpp` 并把 `tests/integration/CMakeLists.txt` 的占位目标替换为场景目标（模板见 Global Constraints；fixture 路径经 `MANGA_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"` 编译定义注入，不提交任何本机绝对路径）。

```cpp
class ManualOfflineScenarioTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void manualOfflineMainline();
    void reopenYieldsIdenticalDomainWithEmptyUndoHistory();
    void sourceImagesUnchangedAcrossMainline();
};
```

`manualOfflineMainline()` 驱动序列（全部经真实适配器 + `manga::test::RealDataStack`；`awaitFuture` 有界等待）：

1. `CreateProjectUseCase` 在临时根创建项目 → `ImportImagesUseCase` 导入 `page_001.png` 单图与含 `page_002.png`/`page_003.png` 的文件夹 → `ApplyImportResultUseCase` + `AcknowledgeImportedAssetsUseCase`。
2. 构造 `EditorViewModel`（OCR/Translator 端口为空）并 `openProjectSession`（P5-C 接口）；`selectPage` 快速切页，断言 `pageImageReady` 只投递当前 `viewGeneration` 的图像。
3. 经 ViewModel intent 创建/移动/缩放 Region，输入原文/译文，调整基础排版字段；`ClearRegionUseCase` + prepare/apply 两阶段清除一个 Region。
4. Undo/Redo 各若干次，断言投影与 `ComicProject::revision()`/Dirty 一致。
5. `saveProject()` → 断言 `savedRevision` 使 `markCleanIfRevision` 成功、Dirty 清除、Undo 历史保留。
6. `requestCloseProject()`（clean 直接关闭）→ 用 `OpenProjectUseCase` 重开 → 新会话 `operator==` 对比页面/Region/文字/排版/派生引用一致，Undo 历史为空（V0.1 不持久化命令栈）。
7. `ExportSnapshotBuilder` + `ExportPagesUseCase` 导出 PNG 与 JPEG 到临时输出目录 → 断言文件存在、非空、页序正确。

`sourceImagesUnchangedAcrossMainline()`：主线前后各跑一次 `sha256Tree(projectRoot, "assets/original")`，断言逐文件哈希相等（导入暂存已 acknowledge，派生/导出写在 `assets/derived` 与外部目录）。

- [ ] **Step 5: 运行确认失败再修通**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_integration_e01_manual_offline
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_integration_e01"
```

Expected: 首轮红（Compile 或断言暴露真实集成缺口）；修复只允许落在 owning layer 的最小契约修正，禁止把逻辑塞进 `ApplicationContext` 或测试特判。最终全绿。

- [ ] **Step 6: 运行相邻回归**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_(ui_export_snapshot_builder|data|application)"
```

- [ ] **Step 7: Commit（仅在获授权时）**

```bash
git add src/ui/screen/editor/ExportSnapshotBuilder.* src/ui/CMakeLists.txt tests/integration tests/unit/ui/export_snapshot_builder_test.cpp
git commit -m "test(integration): add P6-E01 manual offline mainline"
```

---

### Task 5: P6-E02 OCR 主线（Fake OCR + operation/generation 路径）

**Files:**
- Create: `tests/integration/e02_ocr_scenario_test.cpp`
- Modify: `tests/integration/CMakeLists.txt`

**Interfaces:**
- Consumes: P5-D Task 4（`PrepareProcessingAdoptionUseCases`、`PreparedProcessingAdoption`、`ProcessingAdoptionCommand`、`EditorViewModel::adoptProcessingProposal(...)`/`rejectProcessingProposal(...)`）、Task 5（`EditorProcessingController` 的 OCR selected / Adopt / Reject intents）；`RecognizeRegionUseCase`（`src/application/usecase/processing/ProcessingUseCases.h`，`createOperation(sessionGeneration, ...)` → `IOcrEngine::recognize`）；`ApplyOcrResultUseCase`/`SelectCandidateUseCase`（`src/application/usecase/text/TextUseCases.h`）；`tests/unit/application/FakeOcrEngine.h`（`deferred` 可控完成）。
- 真实/替身边界（规格 §6.1 E02 行）：真实 = Repository、Apply UseCase、operation/generation 路径；替身 = 普通 CI 用 Fake OCR。真实 `OnnxMangaOcrEngine` 由既有 CI `p4-model` 作业与 `tests/unit/services/BaberuOcrIntegrationTest.cpp`（`MANGA_OCR_MODEL_DIR` 门控）覆盖，本任务不触碰。

- [ ] **Step 1: 写场景失败测试**

目标 `manga_tests_integration_e02_ocr`（TIMEOUT 60）。测试函数：

```cpp
void unadoptedOcrResultDoesNotMutateDomain();
// Fake OCR 完成后不采用：ComicProject revision/contentRevision 不变、无 Undo 命令入栈。
void adoptionPushesSingleUndoableCommand();
// 采用 → 恰好一条命令；Undo 恢复 before，Redo 不重新调用 OCR（FakeOcrEngine::callCount 不变）。
void manualSourceTextSurvivesLateOcrResult();
// deferred fake：请求 OCR 后先人工编辑原文（contentRevision 变化），再完成 fake；
// 采用因 input.stale_revision 被拒，人工原文保留，无错误 Toast 级状态残留。
void staleSessionGenerationResultIsRejected();
// 会话 generation 递增后完成 deferred 结果：cancelled.stale_generation，静默丢弃。
```

- [ ] **Step 2: 运行确认失败再修通**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_integration_e02_ocr
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_integration_e02"
```

Expected: 暴露的缺口只允许在 application 的 prepare/adopt 或 ui 的 adoption 命令层修；若 P5-D 行为已满足则首轮即绿（记录为验证通过而非新修）。

- [ ] **Step 3: Commit（仅在获授权时）**

```bash
git add tests/integration/e02_ocr_scenario_test.cpp tests/integration/CMakeLists.txt
git commit -m "test(integration): add P6-E02 OCR adoption scenario"
```

---

### Task 6: P6-E03 翻译主线（真实 QtHttpTransport + loopback 协议解析）

**Files:**
- Create: `tests/integration/e03_translation_scenario_test.cpp`
- Modify: `tests/integration/CMakeLists.txt`

**Interfaces:**
- Consumes: `network::QtHttpTransport`（专用网络线程 + `shutdown()`）、`network::OpenAiCompatibleTranslator`（Task 2 的共享配置快照 + `EndpointPolicy::AllowLoopbackHttp`）、`network::RetryPolicy`（注入固定 clock/rng）、`network::ChatCompletionsProtocol`；`TranslatePageUseCase`；P5-D Task 4 的翻译采用路径（按 RegionId 对齐、`TranslationResponseItem` 校验）；`tests/unit/application/FakeCredentialStore.h` 或 `tests/unit/network` 的 `StubCredentialStore` 模式。
- 真实/替身边界（规格 §6.1 E03 行）：真实 = `QtHttpTransport`、ChatCompletions 协议、RetryPolicy、Repository；替身 = loopback server（`manga::test::LoopbackHttpServer`）、Fake Credential Store、固定 clock。CI 不访问公共 Provider、不用真实凭据。

- [ ] **Step 1: 写场景失败测试**

目标 `manga_tests_integration_e03_translation`（额外链接 `manga_network Qt6::Network`，TIMEOUT 60）。测试函数：

```cpp
void loopbackChatCompletionsRoundTrip();
// 人工原文 → loopback 返回固定 chat.completions JSON → 按 RegionId 对齐候选 →
// 断言请求体 model/messages/response_format 与 Authorization 头来自 fake 凭据。
void regionIdAlignmentRejectsUnknownOrDuplicatedIds();
// server 返回含未知/重复 RegionId 的候选：对应项被拒，不挂到错误 Region，其余项保留。
void credential401MapsToReauthenticateAndKeepsManualText();
// 401 → ErrorCategory::Network + RecoveryAction::Reauthenticate；人工译文与既有候选不变。
void retryable429HonorsRetryAfterWithFixedClock();
// 第一次 429 + Retry-After → 固定 clock 的 RetryPolicy 有界重试 → 第二次 200 成功；
// 断言重试次数与延迟决策，不用 wall-clock sleep。
void adoptedTranslationSurvivesSaveReopen();
// 采用候选 → save → close → 重开：manualText/采用结果一致（operator==）。
```

- [ ] **Step 2: 运行确认失败再修通**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_integration_e03_translation
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_integration_e03"
```

- [ ] **Step 3: Commit（仅在获授权时）**

```bash
git add tests/integration/e03_translation_scenario_test.cpp tests/integration/CMakeLists.txt
git commit -m "test(integration): add P6-E03 loopback translation scenario"
```

---

### Task 7: P6-E04 完整处理主线（OCR→翻译→清除→排版→保存→导出）

**Files:**
- Create: `tests/integration/e04_processing_scenario_test.cpp`
- Modify: `tests/integration/CMakeLists.txt`

**Interfaces:**
- Consumes: P5-D Task 3（`SmartTranslatePageWorkflow`、`PageProcessingProposal`：只产出 proposal，不改 `ComicProject`）、Task 4（prepare/adopt 两阶段、`PreparedClearChange`）；真实 `SolidColorInpainter`、`QtTextLayouter`、`FileSystemDerivedAssetStore`、`FileSystemProjectRepository`、`FileSystemComicExporter`、`manga_rendering`；Task 4 的 `ExportSnapshotBuilder`。
- 真实/替身边界（规格 §6.1 E04 行）：真实 = Repository、DerivedAssetStore、Inpainter、Rendering、Layouter、Exporter；替身 = OCR/Translator 返回固定 DTO（`FakeOcrEngine`/`FakeTranslator`），真实 Adapter 已由 E02/E03 各自覆盖。

- [ ] **Step 1: 写场景失败测试**

目标 `manga_tests_integration_e04_processing`（TIMEOUT 60）。测试函数：

```cpp
void fullProcessingMainlineProducesConsistentExport();
// 已有 Region → OCR(固定 DTO) → 翻译(固定 DTO) → Prepare Clear（原子提交不可变资产）
// → push Undo Command → 排版 → 保存 → 导出；断言 project.json 引用、derived 资产、
// 导出文件三者一致，源图 SHA-256 不变。
void clearUndoRedoOnlySwitchesReferencesWithoutIo();
// 清除采用后 Undo/Redo：只切 clearLayerAsset 引用与 isCleared；用 derived store 包裹计数
// 断言 Undo/Redo 期间 commitDerivedLayer/readDerivedLayer 调用次数为 0。
void previewRasterizationMatchesExportWithinTolerance();
// 同一 Region 的 TextLayout 经 canvas 预览路径与 ExportSnapshotBuilder 光栅化：
// 同一 rendering 引擎输出逐像素一致；导出 PNG 与内存合成结果在文档化容差内
// （sRGB 转换后每通道 <=2；透明度、文字位置、图层顺序 Original->Clear->Translation
// ->ManualOverride 一致）。
void failedStageKeepsCommittedStagesAndOriginals();
// 排版阶段注入失败：已提交的清除资产与 Domain 引用保留，不写半成品；
// proposal.issues 含安全摘要，可从失败 stage 以新 operationId 重试。
```

- [ ] **Step 2: 运行确认失败再修通**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_integration_e04_processing
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_integration_e04"
```

- [ ] **Step 3: Commit（仅在获授权时）**

```bash
git add tests/integration/e04_processing_scenario_test.cpp tests/integration/CMakeLists.txt
git commit -m "test(integration): add P6-E04 full processing scenario"
```

---

### Task 8: P6-E05 页面范围导出与最小 UI 导出入口

**Files:**
- Create: `tests/integration/e05_page_export_test.cpp`
- Modify: `tests/integration/CMakeLists.txt`
- Modify: `src/ui/screen/editor/EditorViewModel.h`
- Modify: `src/ui/screen/editor/EditorViewModel.cpp`
- Modify: `src/ui/widget/EditorCommandBar.h`
- Modify: `src/ui/widget/EditorCommandBar.cpp`
- Test: `tests/unit/ui/editor_export_test.cpp`

**Interfaces:**
- Consumes: `ExportPagesUseCase`（`ExportScope { CurrentPage, SelectedPages, AllPages }`）；`data::FileSystemComicExporter`（冲突追加序号、不覆盖、staging 提交）；Task 4 `ExportSnapshotBuilder`；P5-D Task 10（导出失败 → `RecoveryAction::ChoosePath` 经 `RecoveryPresenter` 落到“选择其他目录”入口）。
- 真实/替身边界（规格 §6.1 E05 行）：真实 = Repository、Rendering snapshot、`FileSystemComicExporter`；替身 = 仅文件选择器/目标目录由测试注入。
- Produces（P6 最小 UI 契约新增，P5-C/P5-D 未交付导出入口）：
  - `EditorViewModel::exportPages(application::ExportScope scope, const QString& outputDir)`，Signals `exportCompleted(application::ExportResponse)`/`exportFailed(application::ApplicationError)`；导出期间 State 暴露 `isExporting`，失败经 RecoveryPresenter 呈现。
  - `EditorCommandBar` 增加 Export 动作（当前页/选中页/全部页三态选择），目录由 `QFileDialog` 选择；测试中目录直接注入，不弹真实对话框。

- [ ] **Step 1: 写 UI intent 失败测试**

`tests/unit/ui/editor_export_test.cpp`（新目标 `manga_tests_ui_editor_export`）：无项目/无页面禁用导出；三种 scope 生成正确 `ExportPagesRequest`（currentPageId/selectedPageIds/页序）；导出中禁用重复触发；失败保留项目内容并发出 `exportFailed`。

- [ ] **Step 2: 运行确认失败**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_editor_export
```

- [ ] **Step 3: 实现导出入口**

`exportPages` 先 `ExportSnapshotBuilder::execute`（GUI 线程组装请求，I/O 在 worker），再 `ExportPagesUseCase::execute`；OperationRegistry descriptor 目标用页码序号，不含正文或完整路径。完成回 GUI 线程再更新 State/发 Signal。CommandBar 动作沿用 P5-C Task 8 的既有模式（Tooltip + accessible name）。

- [ ] **Step 4: 写 P6-E05 场景失败测试**

目标 `manga_tests_integration_e05_export`（TIMEOUT 60）。测试函数：

```cpp
void exportCurrentPageOnly();
void exportSelectedPagesPreservesProjectOrder();
// 显式选中 {page3, page1}：输出顺序仍按项目页序 page1, page3，outputStem 与页序一致。
void exportAllPagesWritesEveryPage();
void conflictFileNamesAppendSuffixWithoutOverwrite();
// 输出目录预置同名文件：导出追加序号，旧文件字节不变。
void singlePageFailureKeepsOtherOutputsAndProject();
// 注入一页坏 snapshot（损坏 source 引用）：ExportResponse.failures 含该页明确错误，
// 其余页输出保留，项目 revision/Dirty 不变。
```

- [ ] **Step 5: 运行确认失败再修通**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_integration_e05_export
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_(integration_e05|ui_editor_export)"
```

- [ ] **Step 6: Commit（仅在获授权时）**

```bash
git add src/ui/screen/editor src/ui/widget/EditorCommandBar.* tests/integration/e05_page_export_test.cpp tests/integration/CMakeLists.txt tests/unit/ui/editor_export_test.cpp
git commit -m "feat(ui): add page-range export entry"
```

---

### Task 9: 保存竞争、Save As、.bak 恢复与损坏项目拒绝（持久化生命周期）

**Files:**
- Create: `tests/integration/persistence_lifecycle_test.cpp`
- Modify: `tests/integration/CMakeLists.txt`

**Interfaces:**
- Consumes: `data::FileSystemProjectRepository` 测试钩子（`setSaveTransactionHook`、`setDiskSpaceProbe`、`setSaveAsCommitProbe`）；`SaveProjectUseCase`/`SaveProjectAsUseCase`/`RecoverProjectUseCase`/`OpenProjectUseCase`；P5-C Task 8 保存协议（保存前 seal 文本会话、捕获 `savedRequestRevision`、`markCleanIfRevision(savedRevision)` 成功才 `undoStack.setClean()`、保存不清历史）；P5-D Task 9 项目打开错误分类（`project.not_a_project`、`persistence.*`）。
- 真实/替身边界：全部真实 data 栈 + 真实 `EditorViewModel` 保存协议；故障由仓库钩子/临时文件系统状态注入，不用 fake Repository。

- [ ] **Step 1: 写失败测试**

目标 `manga_tests_integration_persistence`（TIMEOUT 60）。测试函数：

```cpp
void saveRaceDuringTransactionKeepsDirty();
// SaveTransactionHook 内（保存事务已开始）再触发一次编辑；repository 返回的
// savedRevision 落后于当前 revision → markCleanIfRevision 失败 → 保持 Dirty、
// Undo clean point 不移动；再次保存后 clean。
void saveAsWritesCopyAndKeepsOriginalUntouched();
// Save As 到新目录：新目录项目完整可开，原目录 project.json 与 assets 哈希不变，
// 会话切换到新根（既有 lease 语义）。
void bakRecoveryRestoresLastValidProject();
// 损坏 current project.json、保留 .bak：openProject 走恢复决策路径，
// RecoverProjectUseCase 从备份恢复，recoverySource == Backup 且带 warning。
void corruptProjectJsonIsRejectedWithStableCode();
// current 与 .bak 均损坏：打开失败，稳定 code（persistence.recovery_failed 族），
// 不产生半初始化会话，目录内容不被改写。
void diskFullSaveFailureKeepsDirtyAndLastValidFile();
// setDiskSpaceProbe 模拟磁盘不足：保存失败 → 项目保持 Dirty、既有 project.json
// 字节不变、FreeDiskSpace/ChoosePath 恢复动作可用；释放空间后再次保存成功。
```

- [ ] **Step 2: 运行确认失败再修通**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_integration_persistence
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_integration_persistence"
```

Expected: 暴露的缺口在 `data`（保存事务/恢复）或 P5-C 保存协议层修正；P3/P5-D 已覆盖的行为首轮即绿并记录为验证。

- [ ] **Step 3: Commit（仅在获授权时）**

```bash
git add tests/integration/persistence_lifecycle_test.cpp tests/integration/CMakeLists.txt
git commit -m "test(integration): cover save race recovery and corrupt project"
```

---

### Task 10: 会话/视图生命周期、项目切换与迟到结果

**Files:**
- Create: `tests/integration/session_lifecycle_test.cpp`
- Modify: `tests/integration/CMakeLists.txt`

**Interfaces:**
- Consumes: P5-B 冻结契约（`PageId + viewGeneration` 迟到校验、页面切换只失效 View 生命周期）；P5-C Task 1/8（会话打开/关闭顺序、UndoStack 先于聚合释放）；P5-D Task 2（`cancelProject`、operation 快照）与 Task 5（切页不取消项目任务、同目标替换）；Task 3 的 `Application` 注入构造与关闭编排。
- 真实/替身边界：真实 data 栈 + 真实 EditorViewModel；后台任务用 deferred fake OCR/Translator 与可控 `QFuture` 注入完成时机。

- [ ] **Step 1: 写失败测试**

目标 `manga_tests_integration_session`（TIMEOUT 60）。测试函数：

```cpp
void fastPageSwitchDiscardsLateViewResults();
// 连续 selectPage A→B→A；用可控 loader 让旧页加载最后完成：旧 PageId/viewGeneration
// 结果静默丢弃，画布只显示最终页，旧页图像不残留。
void projectBoundTaskSurvivesPageSwitch();
// 对页 A 发起 deferred OCR 后切到页 B：任务不取消，完成仍绑定原 PageId，可采用。
void projectSwitchRejectsLateCompletions();
// 项目 P1 的 deferred 任务未完成时关闭 P1 打开 P2：P1 会话 generation 失效，
// 迟到完成以 cancelled.stale_generation 静默丢弃，P2 的 Domain/UndoStack 不受影响。
void closeCancelDoesNotDestroySession();
// Dirty 会话 requestCloseProject → closeDecisionRequested → cancelClose：
// 会话、UndoStack、in-flight 项目任务全部存活，可继续编辑保存。
void confirmedCloseRunsTeardownInOrder();
// 确认关闭：观察到固定顺序——停止接受命令 → 文本会话 seal → 项目任务收到停止 →
// UndoStack 销毁 → repository closeProject（lease 释放）→ 会话释放；
// 之后完成的 deferred future 不触发任何已销毁对象回调（QPointer 置空断言 + 无崩溃）。
void applicationExitRunsSameProtocolAsWindowClose();
// Task 3 注入构造的 Application 打开临时项目后直接退出：与窗口关闭走同一代码路径，
// 进程正常结束（offscreen），无 QWARN 级的销毁期回调泄漏。
```

- [ ] **Step 2: 运行确认失败再修通**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_integration_session
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_integration_session"
```

- [ ] **Step 3: Commit（仅在获授权时）**

```bash
git add tests/integration/session_lifecycle_test.cpp tests/integration/CMakeLists.txt
git commit -m "test(integration): cover session teardown and late results"
```

---

### Task 11: 故障恢复矩阵端到端验证与证据收口

**Files:**
- Create: `tests/integration/recovery_matrix_test.cpp`
- Modify: `tests/integration/CMakeLists.txt`
- Modify: `docs/plan/v0.1/phase-6-integration.md`（只在证据全部真实通过后勾选 §8 清单并填 §7 记录）

**Interfaces:**
- Consumes: P5-D Task 10（`RecoveryPresenter`、`RecoveryPresentation`、各 `RecoveryAction` 的真实 UI 路由：Retry/OpenSettings/Reauthenticate/LocateModel/ChoosePath/FreeDiskSpace/None）；P5-D Task 9 项目打开错误矩阵；Task 6 的 loopback server；Task 9 的仓库故障钩子。
- 真实/替身边界：错误一律由真实适配器边界产生（真实仓库/loader/transport/exporter + 注入故障），presentation 层消费 `ApplicationError`；不伪造 `ApplicationError` 直接喂 UI（那是 P5-D 单元测试已覆盖的层面）。

- [ ] **Step 1: 写恢复矩阵失败测试**

目标 `manga_tests_integration_recovery`（链接 `manga_network Qt6::Network`，TIMEOUT 60）。逐行覆盖规格 §5：

```cpp
void missingImageKeepsProjectAndPageSelection();
// 删除一页 assets/original 文件后切到该页：加载失败 → 项目与上一页选择保留，
// 画布不残留旧页图像（P5-B 契约），恢复入口为重新定位/重试加载。
void ocrModelMissingKeepsManualEditingSaveExport();
// 指向不存在模型目录的真实 OnnxMangaOcrEngine（或不可用空端口）：OCR 请求得到
// Model 类错误 + RecoveryAction::LocateModel；人工原文编辑、保存、导出继续成功。
void credentialMissingOr401KeepsManualTranslationAndCandidates();
// Fake 凭据缺失 / loopback 401：RecoveryAction::Reauthenticate，人工译文与既有候选保留。
void rateLimitTimeoutOfflineKeepAdoptedResults();
// loopback 429/静默超时：按 P4 策略的 Retry 入口；已采用结果与人工修改不变。
void clearLayoutFailureWritesNoPartialDerivedAsset();
// 注入非法清除/排版输入：失败返回明确错误；assets/derived 树哈希不变、Domain 引用不变，
// 修正输入后重试成功。
void saveFailureKeepsDirtyAndLastValidFile();        // 与 Task 9 用例互补，断言恢复动作路由
void exportFailureKeepsProjectAndExistingOutputs();
// 输出目录注入只读/不可写：导出失败 → ChoosePath；项目与既有导出文件不变，
// 换目录重试成功。无法可靠建立只读目录的平台行只允许带明确原因 QSKIP。
void projectSwitchRejectsLateResults();              // 引用 Task 10 用例，矩阵行交叉断言
```

- [ ] **Step 2: 运行确认失败再修通**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_integration_recovery
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_integration_recovery"
```

- [ ] **Step 3: 全量验证**

```powershell
prek run --all-files
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test
```

Expected: 全绿；记录实际通过数量与任何 QSKIP/未运行边界，禁止写“应当通过”。确认 `manga_tests_app_smoke` 仍通过且新增集成目标全部离线确定性（无真实模型/凭据/公共网络依赖）。

- [ ] **Step 4: 填写完成证据**

更新 `docs/plan/v0.1/phase-6-integration.md`：勾选 §8 已达成项；§7 记录实际运行的测试命令与结果、场景↔测试名映射（见本计划“验收对照”节）、fixture 清单位置（`tests/fixtures/README.md`）、失败恢复日志摘要的保存位置（`build/local-test/Testing/Temporary/LastTest.log`，失败时按 §6.2 只保留摘要不上传内容）、仍需 P7 验证的边界。

- [ ] **Step 5: Commit（仅在获授权时）**

```bash
git add tests/integration/recovery_matrix_test.cpp tests/integration/CMakeLists.txt docs/plan/v0.1/phase-6-integration.md
git commit -m "test(integration): verify recovery matrix and record P6 evidence"
```

---

## 验收对照与遗留偏差

对照 [`phase-6-integration.md`](phase-6-integration.md) §8 清单的达成映射：

- 组合根接齐 P3–P5 生产 Adapter/UseCase/ViewModel、单一对象图 → Task 2（残差接线）+ P5-D Task 3/7（既有承诺），`manga_tests_app` 降级启动用例。
- Process/Project session/View/Operation 四级生命周期与可注入测试构造 → Task 1（`RealDataStack`）、Task 2、Task 3（`Application` 注入重载）；Project session 由 P5-C `EditorViewModel` 持有，Process 依赖不持有 Project/View 指针由构造注入方向保证。
- 确定性关闭（停止接单 → Dirty 决策 → 会话取消 → Watcher/UndoStack/lease 销毁 → 逆序销毁，无有界外 sleep）→ Task 3 + Task 10 `confirmedCloseRunsTeardownInOrder`/`applicationExitRunsSameProtocolAsWindowClose`。
- 模型/凭据/网络缺失不阻止人工离线主线 → Task 2 `productionContextStartsWithoutModelCredentialOrNetwork` + Task 4（E01 全程无模型无网络）+ Task 11 矩阵行。
- P6-E01..E05 与 §6.1 真实/替身边界 → Task 4–8，目标 `manga_tests_integration_e01_manual_offline`/`_e02_ocr`/`_e03_translation`/`_e04_processing`/`_e05_export`。
- 保存竞争、Save As、恢复、取消、项目切换、迟到结果、人工修改冲突 → Task 9、Task 10、Task 5 `manualSourceTextSurvivesLateOcrResult`。
- RecoveryAction 接真实 UI 入口、失败不写半成品/假成功 → Task 11（真实适配器产生错误，经 P5-D `RecoveryPresenter`）。
- 源图 SHA-256 不变、保存重开一致、导出不覆盖 → Task 4 `sourceImagesUnchangedAcrossMainline`/`reopenYieldsIdenticalDomainWithEmptyUndoHistory`、Task 8 `conflictFileNamesAppendSuffixWithoutOverwrite`。
- 删除占位测试、按场景拆分 → Task 4 Step 4；普通 CI 离线确定性、`p4-model` 独立 → Task 5 边界声明 + Global Constraints。

场景 ↔ 测试名映射（§7 证据用）：

| 场景 | 测试目标 | 关键测试函数 |
| --- | --- | --- |
| P6-E01 | `manga_tests_integration_e01_manual_offline` | `manualOfflineMainline`、`reopenYieldsIdenticalDomainWithEmptyUndoHistory`、`sourceImagesUnchangedAcrossMainline` |
| P6-E02 | `manga_tests_integration_e02_ocr` | `unadoptedOcrResultDoesNotMutateDomain`、`adoptionPushesSingleUndoableCommand`、`manualSourceTextSurvivesLateOcrResult`、`staleSessionGenerationResultIsRejected` |
| P6-E03 | `manga_tests_integration_e03_translation` | `loopbackChatCompletionsRoundTrip`、`regionIdAlignmentRejectsUnknownOrDuplicatedIds`、`credential401MapsToReauthenticateAndKeepsManualText`、`retryable429HonorsRetryAfterWithFixedClock`、`adoptedTranslationSurvivesSaveReopen` |
| P6-E04 | `manga_tests_integration_e04_processing` | `fullProcessingMainlineProducesConsistentExport`、`clearUndoRedoOnlySwitchesReferencesWithoutIo`、`previewRasterizationMatchesExportWithinTolerance`、`failedStageKeepsCommittedStagesAndOriginals` |
| P6-E05 | `manga_tests_integration_e05_export` | `exportCurrentPageOnly`、`exportSelectedPagesPreservesProjectOrder`、`exportAllPagesWritesEveryPage`、`conflictFileNamesAppendSuffixWithoutOverwrite`、`singlePageFailureKeepsOtherOutputsAndProject` |
| §6.3 持久化 | `manga_tests_integration_persistence` | `saveRaceDuringTransactionKeepsDirty`、`saveAsWritesCopyAndKeepsOriginalUntouched`、`bakRecoveryRestoresLastValidProject`、`corruptProjectJsonIsRejectedWithStableCode`、`diskFullSaveFailureKeepsDirtyAndLastValidFile` |
| §6.3 会话 | `manga_tests_integration_session` | `fastPageSwitchDiscardsLateViewResults`、`projectBoundTaskSurvivesPageSwitch`、`projectSwitchRejectsLateCompletions`、`closeCancelDoesNotDestroySession`、`confirmedCloseRunsTeardownInOrder`、`applicationExitRunsSameProtocolAsWindowClose` |
| §5 恢复矩阵 | `manga_tests_integration_recovery` | 见 Task 11 Step 1 |

P6 明确不交付（规格 §1/§10）：自动文字检测、CBZ/PDF、批处理页面、术语库、复杂 Inpaint、流式翻译、Undo 历史持久化；不新增 V0.2 功能；不用演示脚本、真实付费调用或占位测试代替可重复验收。

移交 P7 的边界（不在 P6 下结论）：wall-clock 性能（含热切页、500 页规模）与 >30 fps；视觉最终批准；发布包部署与真实 Windows 版本兼容；真实 Provider 人工验收；单次不可中断 ONNX 推理的实际最坏退出时延；`p4-model` 真实模型验收（既有 CI 作业，P6 只保证不并入离线套件）。

执行中需要记录的决策/偏差：

- §6.1 的 `manga_tests_integration` 目标落地为 `manga_tests_integration_<scenario>` 目标族（沿用 `manga_tests_ui_<name>` 先例），由 `ctest -R manga_tests_integration` 统一选择；不存在单一天桥目标。
- “项目会话控制器”（规格 §2.1）由 P5-C 的 `EditorViewModel` 承担（持有 `OpenedProjectSession`、`QUndoStack`、会话 generation 与取消钩子），P6 不新建同名对象；app 层只编排窗口关闭/进程退出到该协议的衔接。
- 导出快照构建（P6 前无任何生产调用方）作为最小契约新增放在 `ui/screen/editor/ExportSnapshotBuilder`，因为 `application` 不允许依赖 `rendering`；UI 预览与导出由此共享同一 `manga_rendering` 结果。
- `OpenAiCompatibleTranslator` 的 ProviderConfig 热生效机制 P5-D 留待 P6 决定，本计划采用共享不可变快照（`std::shared_ptr<const ProviderConfig>` 原子替换），与既有“每请求读 key”语义一致。
- `OperationRegistry::cancelAll()` 若 P5-D 未交付（其 Task 3 Step 5 文案为 “cancelProject/all”），在 application 层做最小新增，不进 `ApplicationContext`。
- P5-C/P5-D 计划文档引用的 `configure.ps1` 不存在；实际脚本为 `build.ps1 -Configure`，本计划已统一使用后者。
