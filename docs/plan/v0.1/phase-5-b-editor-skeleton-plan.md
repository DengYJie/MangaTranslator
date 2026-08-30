# P5-B：Editor 骨架（页面列表 + 缩略图管线、画布原图加载/平移/缩放）与 Import 页 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让编辑器成为可到达的目的地：从 Home 打开项目或从 Import 页创建导入后，编辑器展示真实页面列表（惰性缩略图管线）和当前页原图画布（平移/缩放），并交付符合 `import.md` 的图片/文件夹导入流程。

**Architecture:** 沿用 P5-A 交付的 MVVM/UDF 基础设施（`BaseViewModel`/`BasePage`）、QtWindowKit 外壳与 `INavigationRegistrar` 路由注册。新增 `screen/editor` 与 `screen/import` 两个屏幕、`ui/manga` 画布（`MangaCanvas`）、`ui/widget` 页面缩略图列表组件。编辑器会话通过新增的 `application::ProjectSessionDescriptor`（纯值 DTO，含稳定 `PageId`、尺寸与资产相对路径）从 Home/Import 传入，UI 层不触碰 Domain 指针。缩略图与原图加载复用既有端口 `IThumbnailStore`/`IPageImageLoader` 与用例 `LoadOrGenerateThumbnailUseCase`/`LoadPageImageUseCase`；导入复用 `CreateProjectUseCase`/`ImportImagesUseCase`/`ApplyImportResultUseCase`/`AcknowledgeImportedAssetsUseCase`/`SaveProjectUseCase`/`CloseProjectUseCase`。

**Tech Stack:** C++23、Qt 6.11.1（Widgets/Test）、FluentQt v1.7.4（pin `5602f5271507972af8292c469972995401dc2e67`，公共 API）、QtWindowKit（既有外壳）、CMake presets `local-*`、Qt Test（offscreen）。

**Spec:** [`phase-5-ui-editor.md`](phase-5-ui-editor.md) + [`phase-5-ui-editor-supplement.md`](phase-5-ui-editor-supplement.md)（冻结契约：两种 generation、缓存预算、GUI 线程边界、缩略图管线、画布 Z-Order）+ `docs/ui/pages/editor.md`、`docs/ui/pages/import.md`、`docs/ui/components/manga-canvas.md`、`docs/ui/components/page-thumbnail-list.md`。已核实符号清单：`.superpowers/sdd/phase-5-a-mvvm-shell-plan/verified-symbols.md`（本计划不重复核实其覆盖的 FluentQt/QWK 符号）。

## Global Constraints

每个任务的执行者都必须满足以下约束（相当于每条 Step 的隐含验收）：

- C++23、UTF-8/LF、4 空格缩进、英文标识符与注释；格式由 prek 钩子把关，钩子改写文件后必须 re-add 再提交。
- 命名空间：UI 层 `manga::ui::{base,navigation,window,widget,manga,screen::<name>}`，application 层 `manga::application`；头文件包含根是 `src`。例外：画布 Z-Order 常量命名空间为冻结契约规定的 `manga::ui::canvas`（见补充规格 §5，文件仍放 `src/ui/manga/`）。
- 只允许 FluentQt 公共头：`#include <FluentQt/FluentQt.h>` 或 `<FluentQt/<Category>.h>` 伞头；禁止私有头、全局 QSS、`QProxyStyle`。本计划新用到的已核实公共符号：`fluent::status_info::ShimmerPainter`（静态方法 `paint(QPainter*, QVector<Element>, Palette, qreal progress, bool animated)`、`imageCardElements(QRectF, qreal)`、`paletteFromTheme(const fluent::FluentElement::Colors&, bool)`）、`fluent::collections::ListView`（`public QListView`）、`fluent::textfields::LineEdit`（`LineEdit(QWidget*)`）、`fluent::basicinput::{RadioButton(QString, QWidget*), ComboBox(QWidget*)}`、`fluent::status_info::{InfoBar::setIsOpen/setSeverity/setTitle/setMessage/setActionWidget, ProgressBar::setRange/setValue(double)}`。
- State 是值类型并提供 `operator==`；只保存稳定 `ProjectId`/`PageId`/枚举/字符串/数值，禁止 `QPixmap`、`QImage`、Widget、Domain 指针进 State。图像经一次性 Signal 投递（`pageImageReady`/`thumbnailReady`），像素缓存归 View 侧专用 cache owner。
- 冻结契约红线：缩略图最多 4 个并发、`QCache` 预算 64 MiB（cost = 像素字节数向上取 KiB）、迟到结果按 `PageId + viewGeneration` 校验静默丢弃；缩略图不进 `project.json`、不推动 Dirty；`paint()`/`paintEvent`/`drawForeground` 不做 I/O、不排版、不查 Domain；缩放范围 5%–1600%。
- 所有 Widget/QPixmap/ViewModel 只在 GUI 线程；工作线程只产生隐式共享 `QImage`；`QFutureWatcher` 回调先核对请求令牌与 generation。
- View 不直接访问 Repository/Store/文件系统/QSettings；ViewModel 依赖一律构造注入接口指针。
- 构建与测试（Git Bash 中调 PowerShell 脚本，细节见 `.agents/skills/manga-translator-build/SKILL.md`）：
  - 配置：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Configure`
  - 构建：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test`（可加 `-Target <target>`）
  - 聚焦测试：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex manga_tests_ui`
  - 全量：`test.ps1 -Preset local-test`（不带 `-Regex`）
  - 日志：`build/local-test/build.log` / `ctest.log`。
- 一个测试可执行文件只有一个 `QTEST_MAIN`；新 UI 测试目标一律 `manga_tests_ui_<name>`，offscreen，TIMEOUT 15。含 Q_OBJECT 的目标必须 `set_target_properties(<t> PROPERTIES AUTOMOC ON)`。
- 测试用假实现：不碰真实凭据/网络/用户文件。UI 层测试的假端口放 `tests/unit/ui/fakes/`；`FakeComicImporter` 直接复用 `tests/unit/application/FakeComicImporter.h`（通过 include 目录，不复制）。
- 提交遵循 Conventional Commits（`<type>(<scope>): <summary>`，英文祈使句小写、≤72 字符）。

## P5 里程碑划分（本 plan 只覆盖 P5-B）

- **P5-A（已完成）**：MVVM 基础设施 + 组合根 + 外壳导航 + Home 项目中心。
- **P5-B（本计划）**：Editor 骨架（页面列表 + 缩略图管线、画布原图加载/平移/缩放）、Import 页、Home→Editor 会话接管。
- **P5-C**：Region 编辑 + Inspector + Undo/Dirty/保存。
- **P5-D**：处理采用、`AppStatusTaskStrip`、Settings Surface、错误恢复矩阵、视觉验收。

P5-B 明确不交付（对照 spec 的显式偏差，P5-C/P5-D 关闭）：

- CommandBar、工具状态机（Select/Create Region）、Region Overlay/Handle、Inspector、Undo/Dirty/保存快捷键 → P5-C。P5-B 画布左键拖动即平移（`ScrollHandDrag` 语义），P5-C 引入工具机后收回。
- 页面行状态机（Queued/Running/Completed/…）、右键菜单、搜索/筛选、重排、多选批量集合 → P5-C/P5-D；P5-B 行只显示缩略图、页码、文件名，单击切换唯一当前页。
- 页面列表紧凑/Overlay 响应式档与可拖动分隔条宽度持久化 → P5-D；P5-B 用固定 232 px `QSplitter` 档位。
- Import 页的来源级异步扫描（每行页数/大小的渐进显示）：**Application 层不存在扫描 Port**（`IComicImporter` 只有 import/discard/acknowledge），P5-B 来源行只显示名称、类型与格式校验结果，页数在导入完成后才可知。这是 spec 缺口，需要扫描 Port 时另立契约任务。
- Import 页的 OCR 引擎 / 翻译 Provider 选择：`CreateProjectRequest` 不携带引擎字段，凭据/Provider 状态属 P5-D；P5-B 不渲染这两个 ComboBox（不展示禁用占位，符合 P5 范围锁定原则）。
- 缩略图 Shimmer 动效的 Reduce Motion 开关、切页 <300 ms 等 wall-clock 门槛 → P5-D 视觉/性能验收。

---

### Task 1: 会话描述契约 `ProjectSessionDescriptor` + Home `projectOpened` 信号修订（P5-A 接口修订）

**背景：** P5-A 遗留声明「`projectOpened` 目前只携带路径与名称；编辑器会话接管 `OpenProjectResult` 的重构 → P5-B」。编辑器骨架需要完整页面清单（稳定 `PageId`、尺寸、资产相对路径），但 UI 不得持有 Domain 指针。因此在 application 层新增纯值 DTO 与映射函数，并修订 Home 信号。

**Files:**
- Modify: `src/application/dto/ProjectDto.h`（新增 `PageDescriptor`、`ProjectSessionDescriptor`）
- Modify: `src/application/mapping/ProjectMapper.h`（新增 `toSessionDescriptor`）
- Modify: `src/ui/screen/home/HomeViewModel.h`、`src/ui/screen/home/HomeViewModel.cpp`（信号改携带 DTO）
- Modify: `tests/unit/ui/fakes/FakeProjectPorts.h`（`FakeProjectRepository` 支持构造带页面的项目、`createProject`/`saveProject` 可成功）
- Test: `tests/unit/ui/home_view_model_test.cpp`（修订既有用例 + 新增 descriptor 断言；映射函数经此端到端覆盖，不另建 application 测试）

**Interfaces:**
- Consumes: `domain::ComicProject`（`id()/name()/pages()`）、`domain::ComicPage`（`id()/orderIndex()/imageSize()/originalAssetPath()/regions()`）。
- Produces:
  - `application::PageDescriptor { domain::PageId id; int orderIndex; domain::ImageSize imageSize; QString originalAssetPath; int regionCount; }`
  - `application::ProjectSessionDescriptor { domain::ProjectId projectId; QString projectRootPath; QString projectName; QVector<PageDescriptor> pages; QVector<ProjectWarning> warnings; }`（`Q_DECLARE_METATYPE`）
  - `application::mapping::ProjectMapper::toSessionDescriptor(const domain::ComicProject&, const QString& projectRootPath)`
  - `HomeViewModel::projectOpened(const application::ProjectSessionDescriptor& session)`（**breaking**：替代 `projectOpened(QString, QString)`）

- [ ] **Step 1: 写失败测试**

`tests/unit/ui/home_view_model_test.cpp` 修订要点（完整文件由执行者基于现状修改）：

1. 文件头部 `#include "application/mapping/ProjectMapper.h"`，并在每个涉及 `projectOpened` 的用例前 `qRegisterMetaType<manga::application::ProjectSessionDescriptor>();`。
2. `openSuccessAddsRecentAndEmitsProjectOpened` 的 `QSignalSpy` 改为监听新签名；断言 `spy.first().first().value<application::ProjectSessionDescriptor>().projectName == "Demo Project"`。
3. 新增用例：

```cpp
void projectOpenedCarriesFullSessionDescriptor() {
    qRegisterMetaType<manga::application::ProjectSessionDescriptor>();
    FakeRecentProjectsStore recents;
    FakeProjectRepository repository;
    repository.openPageCount = 3; // 见 FakeProjectPorts.h 修订
    manga::application::OperationRegistry registry;
    manga::ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
    QSignalSpy spy(&viewModel, &manga::ui::screen::home::HomeViewModel::projectOpened);
    viewModel.openProjectPath(QStringLiteral("D:/manga/demo"));
    QTRY_VERIFY_WITH_TIMEOUT(spy.size() == 1, 2000);
    const auto session = spy.first().first().value<manga::application::ProjectSessionDescriptor>();
    QVERIFY(session.projectId.isValid());
    QCOMPARE(session.projectRootPath, QStringLiteral("D:/manga/demo"));
    QCOMPARE(session.projectName, QStringLiteral("Demo Project"));
    QCOMPARE(session.pages.size(), 3);
    for (int i = 0; i < 3; ++i) {
        QVERIFY(session.pages.at(i).id.isValid());
        QCOMPARE(session.pages.at(i).orderIndex, i);
        QCOMPARE(session.pages.at(i).imageSize, (manga::domain::ImageSize{1200, 1800}));
        QVERIFY(session.pages.at(i).originalAssetPath.startsWith(QStringLiteral("assets/original/")));
    }
}
```

- [ ] **Step 2: 运行确认失败**

先同步修订 `tests/unit/ui/fakes/FakeProjectPorts.h`（否则编译错误指向假端口而非新契约）：

- `FakeProjectRepository` 新增成员 `int openPageCount{0};`、`bool failCreate{false};`、`QString lastCreatedPath;`、`int saveCallCount{0};`、`int closeCallCount{0};`。
- `createProject` 改为：`failCreate` 时保持原失败行为；否则记录 `lastCreatedPath`，返回 `ProjectDescriptor{domain::ProjectId::generate(), request.name, request.path}` 的就绪 Future。
- `openProject` 成功路径：`domain::ComicProject project("Demo Project");` 后按 `openPageCount` 循环 `project.addPage(domain::ComicPage(domain::PageId::generate(), "assets/original/page_" + std::to_string(1000 + i) + ".png", domain::ImageSize{1200, 1800}, i));`，再放进 `OpenProjectResult`。
- `saveProject` 改为：`++saveCallCount;` 返回 `SaveProjectResult{1}` 的就绪 Future。
- `closeProject` 改为：`++closeCallCount;` 后返回就绪 Future。

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_home_view_model
```

Expected: 编译失败（`ProjectSessionDescriptor` / `ProjectMapper::toSessionDescriptor` 不存在，新信号签名不匹配）。

- [ ] **Step 3: 实现**

`src/application/dto/ProjectDto.h` 在 `ProjectDescriptor` 之后追加：

```cpp
/// Flat, view-facing descriptor of one page in an open project session.
struct PageDescriptor {
    domain::PageId id;
    int orderIndex{0};
    domain::ImageSize imageSize;
    QString originalAssetPath; // Project-relative, e.g. assets/original/page_0001.png
    int regionCount{0};
};

/// Everything the editor skeleton needs to present an opened or freshly imported project
/// without holding domain pointers. Carries open/import warnings for later surfacing.
struct ProjectSessionDescriptor {
    domain::ProjectId projectId;
    QString projectRootPath;
    QString projectName;
    QVector<PageDescriptor> pages;
    QVector<ProjectWarning> warnings;
};
```

文件末尾（命名空间外、`#include <QMetaType>` 后）追加：

```cpp
Q_DECLARE_METATYPE(manga::application::PageDescriptor)
Q_DECLARE_METATYPE(manga::application::ProjectSessionDescriptor)
```

`src/application/mapping/ProjectMapper.h` 追加静态方法：

```cpp
static ProjectSessionDescriptor toSessionDescriptor(const domain::ComicProject& project,
                                                    const QString& projectRootPath) {
    ProjectSessionDescriptor descriptor;
    descriptor.projectId = project.id();
    descriptor.projectRootPath = projectRootPath;
    descriptor.projectName = QString::fromStdString(project.name());
    descriptor.pages.reserve(static_cast<qsizetype>(project.pages().size()));
    for (const auto& page : project.pages()) {
        PageDescriptor entry;
        entry.id = page.id();
        entry.orderIndex = page.orderIndex();
        entry.imageSize = page.imageSize();
        entry.originalAssetPath = QString::fromStdString(page.originalAssetPath());
        entry.regionCount = static_cast<int>(page.regions().size());
        descriptor.pages.append(entry);
    }
    return descriptor;
}
```

`src/ui/screen/home/HomeViewModel.h`：信号改为

```cpp
void projectOpened(const application::ProjectSessionDescriptor& session);
```

`src/ui/screen/home/HomeViewModel.cpp` 的 `handleOpenFinished()` 成功分支改为：

```cpp
Q_EMIT toastRequested(tr("已打开项目「%1」").arg(name), false);
auto session = application::mapping::ProjectMapper::toSessionDescriptor(result.value().project,
                                                                      m_pendingOpenPath);
session.warnings = result.value().warnings;
Q_EMIT projectOpened(session);
```

（`#include "application/mapping/ProjectMapper.h"`；`warnings` 的 UI 呈现归 P5-D，这里只透传。）

- [ ] **Step 4: CMake**

无结构变更（只改既有文件）。若执行者给 `ProjectMapper.h` 增加了 include，确认 `manga_ui` 已 PUBLIC 链接 `manga_application`（现状已满足）。

- [ ] **Step 5: 运行确认通过**

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_home_view_model
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_home|manga_tests_ui_home_page|manga_tests_app"
```

Expected: 全绿。注意 `home_page_test.cpp` 若因信号签名变化编译失败，同步修订（它不监听 `projectOpened` 则无需改）。

- [ ] **Step 6: Commit**

```bash
git add src/application/dto/ProjectDto.h src/application/mapping/ProjectMapper.h src/ui/screen/home/ tests/unit/ui/
git commit -m "feat(application): add project session descriptor for editor handoff"
```

---

### Task 2: `ApplicationContext` 扩展缩略图 / 图像加载 / 导入端口

**Files:**
- Modify: `src/app/ApplicationContext.h`、`src/app/ApplicationContext.cpp`
- Test: `tests/unit/app/application_context_test.cpp`（扩展）

**Interfaces:**
- Consumes: `data::FileSystemThumbnailStore(ProjectIoCoordinator*)`、`data::FileSystemPageImageLoader(ProjectIoCoordinator*)`、`data::FileSystemComicImporter(ProjectIoCoordinator*)`（均已存在于 `manga_data`，无需新增适配器）。
- Produces: `ApplicationContext::thumbnailStore()` / `pageImageLoader()` / `comicImporter()` 访问器；注入构造扩展为 6 参数（原 3 参数构造保留并委托，便于既有测试少改动）。

- [ ] **Step 1: 写失败测试**

`tests/unit/app/application_context_test.cpp` 追加（复用既有 Stub，新增三个端口的最小 Stub 实现——直接返回 `makeFailedFuture` 即可）：

```cpp
void injectedImagePortsAreExposed() {
    auto repository = std::make_unique<StubProjectRepository>();
    auto recents = std::make_unique<StubRecentProjectsStore>();
    auto settings = std::make_unique<StubAppSettingsStore>();
    auto thumbnails = std::make_unique<StubThumbnailStore>();
    auto images = std::make_unique<StubPageImageLoader>();
    auto importer = std::make_unique<StubComicImporter>();
    auto* thumbnailsPtr = thumbnails.get();
    auto* imagesPtr = images.get();
    auto* importerPtr = importer.get();

    manga::app::ApplicationContext context(std::move(repository), std::move(recents), std::move(settings),
                                           std::move(thumbnails), std::move(images), std::move(importer));
    QCOMPARE(context.thumbnailStore(), thumbnailsPtr);
    QCOMPARE(context.pageImageLoader(), imagesPtr);
    QCOMPARE(context.comicImporter(), importerPtr);
}

void legacyThreeArgInjectionLeavesImagePortsNull() {
    manga::app::ApplicationContext context(std::make_unique<StubProjectRepository>(),
                                           std::make_unique<StubRecentProjectsStore>(),
                                           std::make_unique<StubAppSettingsStore>());
    QVERIFY(context.thumbnailStore() == nullptr); // 编辑器必须容忍空端口（测试注入场景）
}
```

Stub 示例（`StubThumbnailStore`；其余两个同形）：

```cpp
class StubThumbnailStore final : public manga::application::IThumbnailStore {
public:
    QFuture<manga::application::Result<QImage>> readThumbnail(manga::application::ReadThumbnailRequest,
                                                              manga::application::OperationContext) override {
        return manga::application::makeFailedFuture<QImage>({});
    }
    QFuture<manga::application::Result<void>> writeThumbnail(manga::application::WriteThumbnailRequest,
                                                             manga::application::OperationContext) override {
        return manga::application::makeFailedFuture<void>({});
    }
};
```

注意：`makeFailedFuture<T>({})` 依赖 `ApplicationError` 可默认构造（已满足，见 P5-A Task 3 注记）。`StubComicImporter` 需实现 `importImages`/`discardImportedAssets`/`acknowledgeImportedAssets` 三个纯虚函数。

- [ ] **Step 2: 运行确认失败**（`thumbnailStore()` 等访问器不存在，编译失败）

- [ ] **Step 3: 实现**

`src/app/ApplicationContext.h`：前向声明新增 `class IThumbnailStore; class IPageImageLoader; class IComicImporter;`；新增 6 参数注入构造（3 参数构造声明保留）；访问器：

```cpp
[[nodiscard]] application::IThumbnailStore* thumbnailStore() const { return m_thumbnailStore.get(); }
[[nodiscard]] application::IPageImageLoader* pageImageLoader() const { return m_pageImageLoader.get(); }
[[nodiscard]] application::IComicImporter* comicImporter() const { return m_comicImporter.get(); }
```

成员：`std::unique_ptr<application::IThumbnailStore> m_thumbnailStore;` 等三个。

`src/app/ApplicationContext.cpp`：

```cpp
#include "data/derived/FileSystemThumbnailStore.h"
#include "data/import/FileSystemComicImporter.h"
#include "data/import/FileSystemPageImageLoader.h"
```

生产构造在既有适配器之后追加：

```cpp
m_thumbnailStore = std::make_unique<data::FileSystemThumbnailStore>(m_production->ioCoordinator.get());
m_pageImageLoader = std::make_unique<data::FileSystemPageImageLoader>(m_production->ioCoordinator.get());
m_comicImporter = std::make_unique<data::FileSystemComicImporter>(m_production->ioCoordinator.get());
```

6 参数注入构造逐一 `std::move`；3 参数构造委托：

```cpp
ApplicationContext::ApplicationContext(std::unique_ptr<application::IProjectRepository> projectRepository,
                                       std::unique_ptr<application::IRecentProjectsStore> recentProjectsStore,
                                       std::unique_ptr<application::IAppSettingsStore> appSettingsStore)
    : ApplicationContext(std::move(projectRepository), std::move(recentProjectsStore), std::move(appSettingsStore),
                         nullptr, nullptr, nullptr) {}
```

析构顺序约定：成员声明顺序保持 `m_production`（持有 `ProjectIoCoordinator`）最先声明、最后销毁——三个新适配器都引用 coordinator，必须先于它销毁。在 `ProductionAdapters` 声明上方的既有注释处补一句该不变量说明（非显而易见的销毁顺序约束）。

- [ ] **Step 4: CMake**——无变更（`manga_app` 已链接 `manga_data`）。

- [ ] **Step 5: 运行确认通过**

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_app
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex manga_tests_app
```

Expected: 4 个用例全绿（含既有 2 个）。

- [ ] **Step 6: Commit**

```bash
git add src/app/ApplicationContext.h src/app/ApplicationContext.cpp tests/unit/app/
git commit -m "feat(app): expose thumbnail, image loader and importer ports from context"
```

---

### Task 3: 画布 Z-Order 常量与 `MangaCanvas`（原图层 + 平移/缩放 + 占位状态机）

**Files:**
- Create: `src/ui/manga/CanvasZOrder.h`、`src/ui/manga/MangaCanvas.h`、`src/ui/manga/MangaCanvas.cpp`
- Delete: `src/ui/manga/manga.cpp`（占位编译单元，注释本就要求被真实实现替换）
- Modify: `src/ui/manga/CMakeLists.txt`、`tests/unit/ui/CMakeLists.txt`
- Test: `tests/unit/ui/manga_canvas_test.cpp`（新目标 `manga_tests_ui_manga_canvas`）

**Interfaces:**
- Consumes: `QGraphicsView/QGraphicsScene/QGraphicsPixmapItem`、FluentQt `fluent::FluentElement`（`onThemeUpdated()`/`themeColorsRef()`）、冻结 Z-Order（补充规格 §5）。
- Produces:
  - `manga::ui::canvas`：`kZBackground/kZPageOriginal/kZCleaningLayer/kZTextLayer/kZRegionNormal/kZSelectionHandles/kZToolCreation`（冻结值，P5-C 直接复用）
  - `manga::ui::manga::MangaCanvas`：`ContentState { NoProject, NoPage, Loading, Error, Image }`、`setContentState(state, message, actionText)`、`setPageImage(QImage)`、`clearPageImage()`、`zoomFactor()`、`setZoomFactor(double)`（Clamp 5%–1600%）、`zoomIn()/zoomOut()`（离散档位）、`fitToPage()`、`actualSize()`、`viewCenterOnPage()`、`centerOn(QPointF)`；信号 `zoomChanged(double)`、`viewTransformChanged(double, QPointF)`、`placeholderActionActivated()`。

**设计要点（实现前必读）：**

- Scene 坐标 = 源图像像素坐标；`QGraphicsPixmapItem` 放在 `(0,0)`、`setZValue(canvas::kZPageOriginal)`。SceneRect 设为页面 rect 外扩 24 px 安全空间（`manga-canvas.md` §4）。
- 缩放用 `setTransform(QTransform::fromScale(f, f))`；`wheelEvent` 仅在 `Ctrl` 修饰下缩放并以 `AnchorUnderMouse` 锚定，否则交给基类滚动（`Shift+滚轮` 水平滚动由基类处理）。`setTransformationAnchor(QGraphicsView::AnchorUnderMouse)` 仅在 Ctrl+滚轮路径生效；`fitToPage`/档位缩放前切回 `AnchorViewCenter`。
- P5-B 没有 Region 交互：`setDragMode(QGraphicsView::ScrollHandDrag)`，左键与中键拖动都平移。拖动期间 `viewTransformChanged` 可以连发，ViewModel 只写内存哈希，不进 State。
- 离散档位（`+`/`-` 键与 `zoomIn/zoomOut` 共用）：`{0.05, 0.10, 0.25, 0.50, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0}`；当前值在两档之间时 `+` 取更高档、`-` 取更低档（`manga-canvas.md` §5.2）。`0` 键 → `fitToPage()`，`1` 键 → `actualSize()`。
- 占位状态在 `drawForeground` 绘制（居中消息文本 + 次级提示），动作按钮是一个 `fluent::basicinput::Button` 子 Widget（非 viewport 子控件），只在 `NoProject/NoPage/Error` 状态显示并居中，`clicked` → `placeholderActionActivated()`。`drawForeground` 只读缓存的 state/message 成员，不做任何 I/O。
- `Loading` 状态必须 `clearPageImage()` 后再显示占位（`page-thumbnail-list.md` §6：不让旧页假装为新页）；这条由 `setContentState(Loading, …)` 内部保证。
- 主题：继承 `fluent::FluentElement`，`onThemeUpdated()` 中从 `themeColorsRef()` 取背景/文字色缓存到成员并 `viewport()->update()`；不为 Item 混入 `FluentElement`（补充规格 §5）。
- `setPageImage`：GUI 线程 `QPixmap::fromImage` 一次性转换并缓存为成员，后续缩放只走 view transform，不重采样原图（`src/ui/manga/AGENTS.md` 渲染节）。

- [ ] **Step 1: 写失败测试** `tests/unit/ui/manga_canvas_test.cpp`

```cpp
#include "ui/manga/CanvasZOrder.h"
#include "ui/manga/MangaCanvas.h"

#include <QtTest>

using manga::ui::manga::MangaCanvas;

class MangaCanvasTest : public QObject {
    Q_OBJECT
private slots:
    void zOrderConstantsAreStrictlyOrdered() {
        static_assert(manga::ui::canvas::kZBackground < manga::ui::canvas::kZPageOriginal);
        static_assert(manga::ui::canvas::kZPageOriginal < manga::ui::canvas::kZCleaningLayer);
        static_assert(manga::ui::canvas::kZCleaningLayer < manga::ui::canvas::kZTextLayer);
        static_assert(manga::ui::canvas::kZTextLayer < manga::ui::canvas::kZRegionNormal);
        static_assert(manga::ui::canvas::kZRegionNormal < manga::ui::canvas::kZSelectionHandles);
        static_assert(manga::ui::canvas::kZSelectionHandles < manga::ui::canvas::kZToolCreation);
        QVERIFY(true);
    }

    void initialStateIsNoProject() {
        MangaCanvas canvas;
        QCOMPARE(canvas.contentState(), MangaCanvas::ContentState::NoProject);
    }

    void setPageImageSwitchesToImageState() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QImage image(1200, 1800, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        QCOMPARE(canvas.contentState(), MangaCanvas::ContentState::Image);
    }

    void zoomIsClampedToContractRange() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        QImage image(1200, 1800, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setZoomFactor(0.001);
        QCOMPARE(canvas.zoomFactor(), 0.05);
        canvas.setZoomFactor(100.0);
        QCOMPARE(canvas.zoomFactor(), 16.0);
    }

    void zoomStepsRoundToNearestDiscreteLevel() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        QImage image(1200, 1800, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setZoomFactor(1.0);
        canvas.zoomIn();
        QCOMPARE(canvas.zoomFactor(), 1.5);
        canvas.setZoomFactor(1.2); // 两档之间
        canvas.zoomIn();
        QCOMPARE(canvas.zoomFactor(), 1.5);
        canvas.setZoomFactor(1.2);
        canvas.zoomOut();
        QCOMPARE(canvas.zoomFactor(), 1.0);
    }

    void placeholderActionIsEmittedFromErrorState() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        canvas.setContentState(MangaCanvas::ContentState::Error, QStringLiteral("无法解码页面图像"),
                               QStringLiteral("重试加载"));
        QSignalSpy spy(&canvas, &MangaCanvas::placeholderActionActivated);
        QTest::mouseClick(canvas.placeholderActionButtonForTest(), Qt::LeftButton);
        QCOMPARE(spy.size(), 1);
    }

    void loadingStateClearsPreviousPageImage() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QImage image(1200, 1800, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setContentState(MangaCanvas::ContentState::Loading, QStringLiteral("page_0002.png"));
        QCOMPARE(canvas.contentState(), MangaCanvas::ContentState::Loading);
        QVERIFY(!canvas.hasPageImageForTest());
    }

    void fitToPageNeverFallsBelowMinimumZoom() {
        MangaCanvas canvas;
        canvas.resize(120, 90); // 极小视口
        canvas.show();
        QImage image(12000, 18000, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.fitToPage();
        QVERIFY(canvas.zoomFactor() >= 0.05);
    }

    void viewTransformRoundTripsThroughZoomAndCenter() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QImage image(1200, 1800, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setZoomFactor(2.0);
        canvas.centerOnPage(QPointF(600.0, 900.0));
        const QPointF center = canvas.viewCenterOnPage();
        QVERIFY(qAbs(center.x() - 600.0) < 1.0);
        QVERIFY(qAbs(center.y() - 900.0) < 1.0);
    }
};

QTEST_MAIN(MangaCanvasTest)
#include "manga_canvas_test.moc"
```

注：`placeholderActionButtonForTest()`/`hasPageImageForTest()` 是测试观察钩子（参见 `HomePage::isEmptyStateVisibleForTest` 先例）。若 offscreen 平台 `QTest::mouseClick` 对子按钮投递不稳定，改为直接 `canvas.placeholderActionButtonForTest()->click()`。

- [ ] **Step 2: 运行确认失败**

先把测试目标加进 `tests/unit/ui/CMakeLists.txt`（见 Step 4），然后构建该目标。Expected: 编译失败（`ui/manga/MangaCanvas.h` 不存在）。

- [ ] **Step 3: 实现**

`src/ui/manga/CanvasZOrder.h`：

```cpp
#pragma once

#include <QtGlobal>

/// Frozen z-order for the manga canvas scene (phase-5-ui-editor-supplement §5).
/// V0.1 has no Split Curtain and reserves no curtain slot.
namespace manga::ui::canvas {

inline constexpr qreal kZBackground = 0.0;
inline constexpr qreal kZPageOriginal = 10.0;
inline constexpr qreal kZCleaningLayer = 20.0;
inline constexpr qreal kZTextLayer = 30.0;
inline constexpr qreal kZRegionNormal = 100.0;
inline constexpr qreal kZSelectionHandles = 110.0;
inline constexpr qreal kZToolCreation = 200.0;

} // namespace manga::ui::canvas
```

`src/ui/manga/MangaCanvas.h`：

```cpp
#pragma once

#include "ui/manga/CanvasZOrder.h"

#include <FluentQt/Foundation.h>

#include <QGraphicsView>
#include <QImage>
#include <QPixmap>

class QGraphicsPixmapItem;
class QResizeEvent;
class QWheelEvent;
class QKeyEvent;

namespace fluent::basicinput {
class Button;
}

namespace manga::ui::manga {

/// Manga page viewport: original image layer, pan/zoom and placeholder states.
/// P5-B scope: no region overlay, no tools beyond pan. Scene coordinates are
/// source image pixels; the page pixmap lives at (0,0) with kZPageOriginal.
class MangaCanvas : public QGraphicsView, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
public:
    enum class ContentState { NoProject, NoPage, Loading, Error, Image };

    static constexpr double kMinZoom = 0.05;
    static constexpr double kMaxZoom = 16.0;

    explicit MangaCanvas(QWidget* parent = nullptr);
    ~MangaCanvas() override;

    void onThemeUpdated() override;

    /// Replaces the page image and switches to the Image state. GUI thread only.
    void setPageImage(const QImage& image);
    void clearPageImage();

    /// Loading clears any previous page image so a stale page never poses as the new one.
    void setContentState(ContentState state, const QString& message = {}, const QString& actionText = {});
    [[nodiscard]] ContentState contentState() const { return m_contentState; }

    [[nodiscard]] double zoomFactor() const { return m_zoomFactor; }
    void setZoomFactor(double factor); // clamped to [kMinZoom, kMaxZoom]
    void zoomIn();                     // next discrete level, viewport-center anchor
    void zoomOut();                    // previous discrete level
    void fitToPage();                  // clamped to kMinZoom
    void actualSize();                 // 100%

    /// Center of the viewport in scene (source pixel) coordinates.
    [[nodiscard]] QPointF viewCenterOnPage() const;
    void centerOnPage(const QPointF& scenePos);

    /// Test-only observation hooks.
    [[nodiscard]] QWidget* placeholderActionButtonForTest() const;
    [[nodiscard]] bool hasPageImageForTest() const;

Q_SIGNALS:
    void zoomChanged(double factor);
    void viewTransformChanged(double zoom, const QPointF& centerOnPage);
    void placeholderActionActivated();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    void applyZoom(double factor);
    void emitViewTransform();
    void layoutPlaceholderButton();

    QGraphicsScene* m_scene{nullptr};               // owned via QGraphicsView::setScene
    QGraphicsPixmapItem* m_pageItem{nullptr};       // scene-owned
    fluent::basicinput::Button* m_placeholderAction{nullptr};
    QPixmap m_pagePixmap;
    ContentState m_contentState{ContentState::NoProject};
    QString m_placeholderMessage;
    double m_zoomFactor{1.0};
    QColor m_backgroundColor;
    QColor m_placeholderTextColor;
};

} // namespace manga::ui::manga
```

`MangaCanvas.cpp` 要点：

- 构造：`m_scene = new QGraphicsScene(this); setScene(m_scene);`；`setDragMode(ScrollHandDrag)`；`setRenderHint(QPainter::SmoothPixmapTransform)`；`setFrameShape(QFrame::NoFrame)`；`setAlignment(Qt::AlignCenter)`；`m_placeholderAction = new fluent::basicinput::Button(this); m_placeholderAction->hide();` 连接 `clicked` → `placeholderActionActivated()`；占位按钮可访问名称随 `actionText` 同步（`setAccessibleName`）。
- `applyZoom(factor)`：Clamp 后 `setTransformationAnchor(QGraphicsView::AnchorViewCenter); setTransform(QTransform::fromScale(f, f));` 更新 `m_zoomFactor`，发 `zoomChanged` 与 `emitViewTransform()`。`wheelEvent`：`event->modifiers() & Qt::ControlModifier` 时 `setTransformationAnchor(AnchorUnderMouse)` 按 `event->angleDelta().y()` 指数步进（每 120 单位 ×1.25）后 Clamp，否则 `QGraphicsView::wheelEvent(event)`。
- `setPageImage`：`m_pagePixmap = QPixmap::fromImage(image);` 惰性创建/复用 `m_pageItem`（`setZValue(canvas::kZPageOriginal)`），`m_scene->setSceneRect(QRectF(m_pagePixmap.rect()).adjusted(-24, -24, 24, 24))`，`setContentState(ContentState::Image)` 内部不再清图（拆出私有 `setContentStateInternal(state, message, actionText, bool clearImage)`，`Loading/Error/NoProject/NoPage` 路径清图）。
- `zoomIn/zoomOut` 档位表放匿名命名空间 `constexpr double kZoomLevels[]`。
- `drawForeground`：非 `Image` 状态时在 `rect` 中心绘制 `m_placeholderMessage`（用缓存的 `m_placeholderTextColor`），`Loading` 附加 `tr("正在加载")` 次级行。只读成员，不做 I/O。
- `onThemeUpdated()`：从 `themeColorsRef()` 缓存 `m_backgroundColor`（画布工作区底色）与 `m_placeholderTextColor`，`setBackgroundBrush(m_backgroundColor)`，`viewport()->update()`。颜色角色的确切字段名以 `components/foundation/FluentElement.h` 的 `Colors` 结构为准（参照 `screen/home` 既有用法）。

- [ ] **Step 4: CMake 接线**

`src/ui/manga/CMakeLists.txt` 全文替换：

```cmake
add_library(manga_ui_manga STATIC
    CanvasZOrder.h
    MangaCanvas.cpp
    MangaCanvas.h
)
manga_apply_project_options(manga_ui_manga)
set_target_properties(manga_ui_manga PROPERTIES AUTOMOC ON)

target_include_directories(manga_ui_manga
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_ui_manga
    PUBLIC
        manga_application
        Qt6::Gui
        Qt6::Widgets
        FluentQt::FluentQt
)
```

删除 `src/ui/manga/manga.cpp`。`tests/unit/ui/CMakeLists.txt` 追加（后续每个新测试目标都镜像此块，只换名称与源文件）：

```cmake
qt_add_executable(manga_tests_ui_manga_canvas
    manga_canvas_test.cpp
)
manga_apply_project_options(manga_tests_ui_manga_canvas)
set_target_properties(manga_tests_ui_manga_canvas PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui_manga_canvas
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui_manga_canvas)
add_test(NAME manga_tests_ui_manga_canvas COMMAND manga_tests_ui_manga_canvas)
set_tests_properties(manga_tests_ui_manga_canvas PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)
```

- [ ] **Step 5: 运行确认通过**

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_manga_canvas
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex manga_tests_ui_manga_canvas
```

Expected: 9 个用例全绿。若 `viewTransformRoundTripsThroughZoomAndCenter` 在 offscreen 下因布局未 settle 失败，在 `show()` 后加 `QCoreApplication::processEvents();` 再断言。

- [ ] **Step 6: Commit**

```bash
git add src/ui/manga/ tests/unit/ui/
git commit -m "feat(ui): add manga canvas with pan, zoom and placeholder states"
```

---

### Task 4: 页面列表组件（`PageListModel` + `PageThumbnailDelegate` + `PageThumbnailList`）

**Files:**
- Create: `src/ui/widget/PageListModel.h`、`src/ui/widget/PageListModel.cpp`、`src/ui/widget/PageThumbnailDelegate.h`、`src/ui/widget/PageThumbnailDelegate.cpp`、`src/ui/widget/PageThumbnailList.h`、`src/ui/widget/PageThumbnailList.cpp`
- Modify: `src/ui/CMakeLists.txt`、`tests/unit/ui/CMakeLists.txt`
- Test: `tests/unit/ui/page_list_model_test.cpp`（`manga_tests_ui_page_list_model`）、`tests/unit/ui/page_thumbnail_list_test.cpp`（`manga_tests_ui_page_thumbnail_list`）

**Interfaces:**
- Consumes: `fluent::collections::ListView`（QListView 子类）、`fluent::status_info::ShimmerPainter`、`domain::PageId`。
- Produces:
  - `manga::ui::widget::PageListEntry { domain::PageId id; int orderIndex; QString displayName; double aspectRatio; }`
  - `manga::ui::widget::ThumbnailStatus { NotLoaded, Loading, Ready, Failed }`
  - `PageListModel`（QAbstractListModel；Roles：`PageIdRole = Qt::UserRole`、`DisplayNameRole`、`OrderIndexRole`、`AspectRatioRole`、`ThumbnailStatusRole`、`ThumbnailErrorRole`）：`setPages(QVector<PageListEntry>)`（内容相同则跳过 reset）、`pageIdAt(row)`、`rowForPageId(PageId)`、`setThumbnailStatus(PageId, ThumbnailStatus, QString errorSummary = {})`。
  - `PageThumbnailList`（QWidget + FluentElement；内部持有 model/delegate/cache/timer）：`model()`、`setCurrentPageId(PageId)`（不触发 `pageActivated`）、`insertThumbnail(PageId, QImage)`、`markThumbnailFailed(PageId, QString)`、`resetLoadingThumbnails()`、`thumbnailCacheTotalCostForTest()`；信号 `pageActivated(PageId)`、`thumbnailRequested(PageId, bool force)`。

**设计要点（对应 `page-thumbnail-list.md` §3 与补充规格 §6）：**

- `QCache<QString, QPixmap>` 由 `PageThumbnailList` 持有，key = `QString::fromStdString(pageId.toString())`；`setMaxCost(64 * 1024)`（单位 KiB），插入 cost = `(image.sizeInBytes() + 1023) / 1024` 且至少 1。**这是内存约束，不是张数约束。**
- Model 只保存 `PageListEntry` + 每行 `ThumbnailStatus`/`errorSummary`；缩略图像素只存在于 `QCache`；禁止 `setIndexWidget()`。
- Delegate 绘制：行高 88 px，缩略图区 56×72 保持纵横比居中；`Ready` 且有缓存 → 画 pixmap；`Loading`/`NotLoaded` → `ShimmerPainter::paint(painter, ShimmerPainter::imageCardElements(thumbRect), palette, progress, animated)`；`Failed` → 警告字形（`\uE7BA`）+ 文件名色文本 + 右侧重试热点区。palette 由 List 在 `onThemeUpdated()` 用 `ShimmerPainter::paletteFromTheme(themeColorsRef())` 解析后注入 delegate。
- 重试：delegate `editorEvent` 命中重试热点 → `list->thumbnailRequested(pageId, /*force=*/true)`。
- 惰性请求：`PageThumbnailList` 在 viewport `paintEvent` 前（或滚动/resize 后）计算可视行 ±2，对状态为 `NotLoaded` 的行发 `thumbnailRequested(pageId, false)` 并把行置为 `Loading`（Model 负责 `dataChanged`）。**不在 delegate 的 `paint()` 里发请求**——`paint()` 只画。
- Shimmer 动效：List 持有 `QTimer`（间隔 50 ms），仅当可视范围内存在 `Loading` 行时运行，推进 `m_shimmerProgress`（周期 1600 ms）并 `viewport()->update()`。Reduce Motion 下禁用动效属 P5-D（本里程碑用动效常开 + 静态兜底即可，见里程碑偏差声明）。
- 当前页：`setCurrentPageId` 用 `QSignalBlocker` 包住 `setCurrentIndex`，避免回环；用户点击 → `pageActivated`。批量多选（Ctrl/Shift）不在 P5-B，selectionMode 保持 `SingleSelection`。

- [ ] **Step 1: 写失败测试**

`tests/unit/ui/page_list_model_test.cpp`：

```cpp
#include "ui/widget/PageListModel.h"

#include <QtTest>

using manga::ui::widget::PageListEntry;
using manga::ui::widget::PageListModel;
using manga::ui::widget::ThumbnailStatus;

namespace {
PageListEntry makeEntry(int order) {
    PageListEntry entry;
    entry.id = manga::domain::PageId::generate();
    entry.orderIndex = order;
    entry.displayName = QStringLiteral("page_%1.png").arg(1000 + order);
    entry.aspectRatio = 2.0 / 3.0;
    return entry;
}
} // namespace

class PageListModelTest : public QObject {
    Q_OBJECT
private slots:
    void setPagesPopulatesRows() {
        PageListModel model;
        QVector<PageListEntry> pages{makeEntry(0), makeEntry(1), makeEntry(2)};
        model.setPages(pages);
        QCOMPARE(model.rowCount(), 3);
        const QModelIndex index = model.index(1);
        QCOMPARE(index.data(PageListModel::OrderIndexRole).toInt(), 1);
        QCOMPARE(index.data(PageListModel::DisplayNameRole).toString(), QStringLiteral("page_1001.png"));
        QCOMPARE(index.data(PageListModel::ThumbnailStatusRole).toInt(),
                 static_cast<int>(ThumbnailStatus::NotLoaded));
        QCOMPARE(index.data(Qt::AccessibleTextRole).toString(),
                 QStringLiteral("第 2 页，page_1001.png"));
    }

    void identicalSetPagesSkipsReset() {
        PageListModel model;
        const QVector<PageListEntry> pages{makeEntry(0), makeEntry(1)};
        model.setPages(pages);
        QSignalSpy spy(&model, &QAbstractItemModel::modelReset);
        model.setPages(pages);
        QCOMPARE(spy.size(), 0);
    }

    void thumbnailStatusTransitionsByPageId() {
        PageListModel model;
        const auto entry = makeEntry(0);
        model.setPages({entry});
        model.setThumbnailStatus(entry.id, ThumbnailStatus::Loading);
        QCOMPARE(model.index(0).data(PageListModel::ThumbnailStatusRole).toInt(),
                 static_cast<int>(ThumbnailStatus::Loading));
        model.setThumbnailStatus(entry.id, ThumbnailStatus::Failed, QStringLiteral("无法解码"));
        QCOMPARE(model.index(0).data(PageListModel::ThumbnailStatusRole).toInt(),
                 static_cast<int>(ThumbnailStatus::Failed));
        QCOMPARE(model.index(0).data(PageListModel::ThumbnailErrorRole).toString(), QStringLiteral("无法解码"));
        model.setThumbnailStatus(manga::domain::PageId::generate(), ThumbnailStatus::Ready); // 未知 ID：忽略
        QCOMPARE(model.rowCount(), 1);
    }

    void modelHoldsNoPixmaps() {
        // 结构性断言：Model 的 Role 集合里没有图像 Role（像素只进 QCache）。
        PageListModel model;
        model.setPages({makeEntry(0)});
        QVERIFY(!model.index(0).data(Qt::DecorationRole).isValid());
    }
};

QTEST_MAIN(PageListModelTest)
#include "page_list_model_test.moc"
```

`tests/unit/ui/page_thumbnail_list_test.cpp`：

```cpp
#include "ui/widget/PageThumbnailList.h"
#include "ui/widget/PageListModel.h"

#include <QtTest>

using manga::ui::widget::PageListEntry;
using manga::ui::widget::PageThumbnailList;
using manga::ui::widget::ThumbnailStatus;

namespace {
PageListEntry makeEntry(int order) {
    PageListEntry entry;
    entry.id = manga::domain::PageId::generate();
    entry.orderIndex = order;
    entry.displayName = QStringLiteral("page_%1.png").arg(1000 + order);
    entry.aspectRatio = 2.0 / 3.0;
    return entry;
}
} // namespace

class PageThumbnailListTest : public QObject {
    Q_OBJECT
private slots:
    void visibleRowsRequestThumbnailsOnlyOnce() {
        PageThumbnailList list;
        QVector<PageListEntry> pages;
        for (int i = 0; i < 10; ++i) pages.append(makeEntry(i));
        list.model()->setPages(pages);
        list.resize(220, 400);
        list.show();
        QCoreApplication::processEvents();
        QSignalSpy spy(&list, &PageThumbnailList::thumbnailRequested);
        list.requestVisibleThumbnails();
        const int firstCount = spy.size();
        QVERIFY(firstCount > 0);
        QVERIFY(firstCount <= 10);
        list.requestVisibleThumbnails(); // 已置 Loading 的行不重复请求
        QCOMPARE(spy.size(), firstCount);
    }

    void thumbnailCacheHonorsSixtyFourMiBBudget() {
        PageThumbnailList list;
        // 每张 1024x1024 RGBA = 4 MiB = 4096 KiB cost；64 MiB 预算最多容纳 16 张。
        const QImage image(1024, 1024, QImage::Format_RGBA8888);
        for (int i = 0; i < 40; ++i) {
            list.insertThumbnail(manga::domain::PageId::generate(), image);
        }
        QVERIFY(list.thumbnailCacheTotalCostForTest() <= 64 * 1024);
    }

    void failedThumbnailCanBeRetriedWithForce() {
        PageThumbnailList list;
        const auto entry = makeEntry(0);
        list.model()->setPages({entry});
        list.markThumbnailFailed(entry.id, QStringLiteral("无法解码"));
        QCOMPARE(list.model()->index(0).data(manga::ui::widget::PageListModel::ThumbnailStatusRole).toInt(),
                 static_cast<int>(ThumbnailStatus::Failed));
        // force 重试由 delegate 热点触发；这里直接验证信号路径存在：
        QSignalSpy spy(&list, &PageThumbnailList::thumbnailRequested);
        list.requestThumbnailForTest(entry.id, true);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().at(1).toBool(), true);
    }

    void resetLoadingThumbnailsReturnsRowsToNotLoaded() {
        PageThumbnailList list;
        const auto entry = makeEntry(0);
        list.model()->setPages({entry});
        list.model()->setThumbnailStatus(entry.id, ThumbnailStatus::Loading);
        list.resetLoadingThumbnails();
        QCOMPARE(list.model()->index(0).data(manga::ui::widget::PageListModel::ThumbnailStatusRole).toInt(),
                 static_cast<int>(ThumbnailStatus::NotLoaded));
    }

    void currentPageSelectionDoesNotReemitActivation() {
        PageThumbnailList list;
        const auto entry = makeEntry(0);
        list.model()->setPages({entry, makeEntry(1)});
        list.show();
        QSignalSpy spy(&list, &PageThumbnailList::pageActivated);
        list.setCurrentPageId(entry.id);
        QCOMPARE(spy.size(), 0);
    }
};

QTEST_MAIN(PageThumbnailListTest)
#include "page_thumbnail_list_test.moc"
```

注：`requestVisibleThumbnails()` 必须是 public（EditorPage 不直接调，它由 List 内部滚动/绘制钩子驱动，public 是为了可测）。`requestThumbnailForTest` 是测试钩子。信号参数 `domain::PageId` 需要 `Q_DECLARE_METATYPE`——Task 5 会在 `EditorState.h` 声明，本 Task 先在 `PageListModel.h` 末尾声明一次（同一类型只允许一个 `Q_DECLARE_METATYPE`，放在最早引入的头文件里，Task 5 改为 include 它）。

- [ ] **Step 2: 运行确认失败**（`ui/widget/...` 不存在，编译失败）

- [ ] **Step 3: 实现**

`src/ui/widget/PageListModel.h`：

```cpp
#pragma once

#include "domain/model/Uuid.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVector>

namespace manga::ui::widget {

enum class ThumbnailStatus { NotLoaded, Loading, Ready, Failed };

/// View-facing projection of one project page. Holds no pixmaps; pixels live in the
/// PageThumbnailList QCache keyed by PageId.
struct PageListEntry {
    domain::PageId id;
    int orderIndex{0};
    QString displayName;
    double aspectRatio{1.0};
    bool operator==(const PageListEntry&) const = default;
};

class PageListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PageIdRole = Qt::UserRole,
        DisplayNameRole,
        OrderIndexRole,
        AspectRatioRole,
        ThumbnailStatusRole,
        ThumbnailErrorRole,
    };

    explicit PageListModel(QObject* parent = nullptr);

    /// Replaces the page projection. No reset when the content is unchanged.
    void setPages(const QVector<PageListEntry>& pages);
    [[nodiscard]] std::optional<domain::PageId> pageIdAt(int row) const;
    [[nodiscard]] int rowForPageId(const domain::PageId& pageId) const;
    void setThumbnailStatus(const domain::PageId& pageId, ThumbnailStatus status, QString errorSummary = {});

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

private:
    struct Row {
        PageListEntry entry;
        ThumbnailStatus thumbnailStatus{ThumbnailStatus::NotLoaded};
        QString thumbnailError;
        bool operator==(const Row&) const = default;
    };
    QVector<Row> m_rows;
    QHash<QString, int> m_rowByPageId; // key: PageId string
};

} // namespace manga::ui::widget

Q_DECLARE_METATYPE(manga::domain::PageId)
```

实现要点：`setPages` 整体比较 `m_rows` 的 entry 部分，相同直接 return；重建 `m_rowByPageId`；尽量保留既有行的 thumbnail 状态（按 PageId 迁移）。`data()` 的 `Qt::AccessibleTextRole` 返回 `tr("第 %1 页，%2").arg(orderIndex + 1).arg(displayName)`；`ToolTipRole` 返回 displayName + 错误摘要。

`PageThumbnailDelegate`（`QStyledItemDelegate` 子类）：`paint()` 按 `ThumbnailStatusRole` 分支绘制（见设计要点），`editorEvent()` 处理重试热点与行激活；持有 `PageThumbnailList*` 非拥有指针以查询缓存与 palette。重试热点矩形（行右下 24×24）为匿名命名空间 `constexpr`。

`PageThumbnailList.h` 骨架：

```cpp
#pragma once

#include "domain/model/Uuid.h"

#include <FluentQt/Foundation.h>

#include <QCache>
#include <QPixmap>
#include <QWidget>

class QListView;
class QTimer;

namespace manga::ui::widget {

class PageListModel;
class PageThumbnailDelegate;

/// Editor page pane: virtualized page list with lazy thumbnails.
/// Owns the 64 MiB QCache pixel cache; the model stores only PageId, aspect and status.
class PageThumbnailList : public QWidget, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
public:
    explicit PageThumbnailList(QWidget* parent = nullptr);

    void onThemeUpdated() override;

    [[nodiscard]] PageListModel* model() const { return m_model; }
    void setCurrentPageId(const domain::PageId& pageId);

    /// Requests thumbnails for visible rows plus two rows of lookahead. Loading rows
    /// are not requested again.
    void requestVisibleThumbnails();

    /// GUI thread: converts to QPixmap, inserts into the bounded cache, marks the row Ready.
    void insertThumbnail(const domain::PageId& pageId, const QImage& image);
    void markThumbnailFailed(const domain::PageId& pageId, const QString& safeMessage);
    /// Generation bump: in-flight binds are discarded, Loading rows return to NotLoaded.
    void resetLoadingThumbnails();

    [[nodiscard]] const QPixmap* thumbnail(const domain::PageId& pageId) const; // delegate lookup
    [[nodiscard]] int thumbnailCacheTotalCostForTest() const;
    void requestThumbnailForTest(const domain::PageId& pageId, bool force);

Q_SIGNALS:
    void pageActivated(const domain::PageId& pageId);
    void thumbnailRequested(const domain::PageId& pageId, bool force);

private:
    void updateShimmerTimer();

    PageListModel* m_model{nullptr};          // owned by this
    PageThumbnailDelegate* m_delegate{nullptr}; // owned by view
    QListView* m_view{nullptr};               // fluent::collections::ListView
    QCache<QString, QPixmap> m_thumbnailCache; // cost in KiB, max 64 * 1024
    QTimer* m_shimmerTimer{nullptr};
    qreal m_shimmerProgress{0.0};
    fluent::status_info::ShimmerPainter::Palette m_shimmerPalette;
};

} // namespace manga::ui::widget
```

`PageThumbnailList.cpp` 要点：构造创建 model/delegate/view + 64 KiB×1024 的 `setMaxCost`；`m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel)`；连接 view 的 `clicked` → `pageActivated(model->pageIdAt(row))`（ guarded）；scrollbar/resize/paint 钩子驱动 `requestVisibleThumbnails()`。`insertThumbnail`：`new QPixmap(QPixmap::fromImage(image))`，cost 见设计要点，插入后 `m_model->setThumbnailStatus(pageId, Ready)`。`onThemeUpdated()` 刷新 palette 并 `viewport()->update()`。

- [ ] **Step 4: CMake 接线**

`src/ui/CMakeLists.txt` 源列表追加 `widget/PageListModel.cpp/h`、`widget/PageThumbnailDelegate.cpp/h`、`widget/PageThumbnailList.cpp/h`。`tests/unit/ui/CMakeLists.txt` 镜像既有块追加 `manga_tests_ui_page_list_model`、`manga_tests_ui_page_thumbnail_list` 两个目标。

- [ ] **Step 5: 运行确认通过**

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_page"
```

Expected: 两个新目标共 9 个用例全绿。

- [ ] **Step 6: Commit**

```bash
git add src/ui/widget/ src/ui/CMakeLists.txt tests/unit/ui/
git commit -m "feat(ui): add page thumbnail list with bounded pixmap cache"
```

---

### Task 5: `EditorViewModel`（会话接管、切页 + viewGeneration、原图加载、缩略图并发调度）

**Files:**
- Create: `src/ui/screen/editor/EditorState.h`、`src/ui/screen/editor/EditorViewModel.h`、`src/ui/screen/editor/EditorViewModel.cpp`
- Create: `tests/unit/ui/fakes/FakeImagePorts.h`（`FakeThumbnailStore` + `FakePageImageLoader`，支持 deferred 手动完成）
- Modify: `src/ui/CMakeLists.txt`、`tests/unit/ui/CMakeLists.txt`
- Test: `tests/unit/ui/editor_view_model_test.cpp`（`manga_tests_ui_editor_view_model`）

**Interfaces:**
- Consumes: `application::IThumbnailStore`、`application::IPageImageLoader`、`application::OperationRegistry`（`createOperation(sessionGeneration, viewGeneration)`/`cancelOperation`）、`application::LoadOrGenerateThumbnailUseCase`、`application::LoadPageImageUseCase`、`application::ProjectSessionDescriptor`（Task 1）。
- Produces:
  - `manga::ui::screen::editor::EditorPageEntry`、`EditorNotice`、`EditorState`（值类型 + `operator==`）
  - `PageImageDelivery { domain::PageId pageId; QImage image; double zoom; QPointF centerOnPage; bool hasSavedTransform; }`（一次性投递，不进 State）
  - `EditorViewModel`：`openProjectSession(ProjectSessionDescriptor)`、`closeProjectSession()`、`selectPage(PageId)`、`selectAdjacentPage(int offset)`、`retryPageLoad()`、`requestThumbnail(PageId, bool force)`、`noteViewTransform(double zoom, QPointF centerOnPage)`、`dismissNotice()`；信号 `stateChanged`、`pageImageReady(PageImageDelivery)`、`thumbnailReady(PageId, QImage)`、`thumbnailFailed(PageId, QString)`、`thumbnailsInvalidated()`。

**关键语义（冻结契约映射，实现与测试都必须遵守）：**

- `viewGeneration`：切页、重建会话、关闭会话时 `++`；其覆盖范围 = 当前页原图加载 + 可视缩略图绑定。bump 时清空缩略图队列、发 `thumbnailsInvalidated()`（View 把 Loading 行复位为 NotLoaded 以便重新请求）；在途缩略图完成时按「请求时捕获的 generation == 当前 state.viewGeneration」校验，不匹配**静默丢弃**。
- `projectSessionGeneration`：`openProjectSession` 时 `++`；缩略图/原图 OperationContext 都携带它，但它不作为 ViewModel 侧的丢弃条件（会话切换已由 `m_session` 替换 + 令牌失效覆盖）。
- 原图加载：`selectPage` → 取消上一页加载（`cancelOperation(m_pageLoadOperation)`）→ `beginRequest()` 令牌 → State 置 `isPageLoading=true`、清 `notice` → `LoadPageImageUseCase`（请求参数 `expectedSize` 取会话中该页 `imageSize`，是防错位守卫）。完成回调：`isRequestCurrent(token)` 且 `state.currentPageId == pageId` 才采用；`ErrorCategory::Cancelled` 静默；其余失败 → `notice = {safeMessage, retryable=true}`。
- 缩略图调度：在途 ≤ 4（`kMaxConcurrentThumbnails`）；超出进 FIFO 队列；`force=false` 时在途/队列去重；完成（无论成败）后泵队列。成功 → `thumbnailReady(pageId, image)`；失败 → `thumbnailFailed(pageId, safeMessage)`。
- **`LoadOrGenerateThumbnailUseCase` 必须是成员变量**（栈上构造会在异步 `.then` 链中留下悬垂 `this`——其 `execute` 的 continuation 捕获 `this` 访问端口）。同理持有 `LoadPageImageUseCase` 成员。这是非显而易见约束，代码注释中写明。
- 每页视图记忆：`noteViewTransform` 只写 `QHash<QString, ViewTransform>`（key = PageId 字符串），不触发 State 更新；页加载成功后从哈希取回，经 `pageImageReady` 投递（`hasSavedTransform`），无记忆则由画布 `fitToPage()`。
- 空项目（0 页）：不启动任何加载，`currentPageId` 保持 nil；画布占位由 View 根据 State 推导。

`src/ui/screen/editor/EditorState.h`：

```cpp
#pragma once

#include "application/dto/ProjectDto.h"
#include "domain/model/Geometry.h"
#include "domain/model/Uuid.h"

#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>

#include <optional>

namespace manga::ui::screen::editor {

/// Renderable projection of one project page. No pixmaps, no domain pointers.
struct EditorPageEntry {
    domain::PageId id;
    int orderIndex{0};
    QString displayName;
    domain::ImageSize imageSize;
    int regionCount{0};

    [[nodiscard]] double aspectRatio() const {
        return imageSize.isValid() ? static_cast<double>(imageSize.width) / imageSize.height : 1.0;
    }
    bool operator==(const EditorPageEntry&) const = default;
};

/// Page-level recoverable problem surfaced by the canvas placeholder.
struct EditorNotice {
    QString safeMessage;
    bool retryable{false};
    bool operator==(const EditorNotice&) const = default;
};

/// P5-B subset of the frozen EditorState (supplement §4): no regions, no tools, no
/// preview mode yet. Only stable IDs and value data; pixmaps never enter State.
struct EditorState {
    domain::ProjectId projectId{}; // nil = no project open
    QString projectName;
    QVector<EditorPageEntry> pages;
    domain::PageId currentPageId{}; // nil = no current page
    double zoomFactor{1.0};
    quint64 viewGeneration{0};
    quint64 projectSessionGeneration{0};
    bool isPageLoading{false};
    std::optional<EditorNotice> notice;
    bool operator==(const EditorState&) const = default;
};

/// One-shot delivery of a decoded page image to the canvas. Never stored in State.
struct PageImageDelivery {
    domain::PageId pageId;
    QImage image;
    double zoom{1.0};
    QPointF centerOnPage;
    bool hasSavedTransform{false};
};

} // namespace manga::ui::screen::editor

Q_DECLARE_METATYPE(manga::ui::screen::editor::PageImageDelivery)
```

`src/ui/screen/editor/EditorViewModel.h`：

```cpp
#pragma once

#include "ui/base/BaseViewModel.h"
#include "ui/screen/editor/EditorState.h"

#include "application/dto/ImportDto.h"
#include "application/dto/ProjectDto.h"
#include "application/operation/AsyncResult.h"

#include <QFutureWatcher>
#include <QHash>
#include <QSet>

namespace manga::application {
class IPageImageLoader;
class IThumbnailStore;
class LoadOrGenerateThumbnailUseCase;
class LoadPageImageUseCase;
class OperationRegistry;
} // namespace manga::application

namespace manga::ui::screen::editor {

/// Drives the editor skeleton: project session, current page image loading and the
/// thumbnail pipeline (max 4 concurrent, generation-checked late results).
class EditorViewModel : public base::BaseViewModel<EditorViewModel, EditorState> {
    Q_OBJECT
public:
    static constexpr int kMaxConcurrentThumbnails = 4;

    EditorViewModel(application::IThumbnailStore* thumbnailStore,
                    application::IPageImageLoader* pageImageLoader,
                    application::OperationRegistry* operationRegistry,
                    QObject* parent = nullptr);
    ~EditorViewModel() override;

    void openProjectSession(const application::ProjectSessionDescriptor& session);
    void closeProjectSession();
    void selectPage(const domain::PageId& pageId);
    void selectAdjacentPage(int offset);
    void retryPageLoad();
    void requestThumbnail(const domain::PageId& pageId, bool force);
    /// Records the live canvas transform for the current page. Memory-only, no State churn.
    void noteViewTransform(double zoom, const QPointF& centerOnPage);
    void dismissNotice();

Q_SIGNALS:
    void stateChanged(const EditorState& state);
    void pageImageReady(const PageImageDelivery& delivery);
    void thumbnailReady(const domain::PageId& pageId, const QImage& image);
    void thumbnailFailed(const domain::PageId& pageId, const QString& safeMessage);
    /// viewGeneration bumped: in-flight thumbnail binds are discarded, rows must re-request.
    void thumbnailsInvalidated();

protected:
    void emitStateChanged() override { Q_EMIT stateChanged(m_state); }

private:
    struct ViewTransform {
        double zoom{1.0};
        QPointF centerOnPage;
    };
    struct ThumbnailTicket {
        application::OperationId operationId;
        quint64 viewGeneration{0};
    };

    void startPageLoad(const domain::PageId& pageId);
    void handlePageLoadFinished();
    void startThumbnail(const domain::PageId& pageId);
    void handleThumbnailFinished(const domain::PageId& pageId);
    void pumpThumbnailQueue();
    void cancelThumbnailRequests();
    [[nodiscard]] const application::PageDescriptor* findPage(const domain::PageId& pageId) const;

    application::IThumbnailStore* m_thumbnailStore;     // non-owning, may be null in tests
    application::IPageImageLoader* m_pageImageLoader;   // non-owning, may be null in tests
    application::OperationRegistry* m_operationRegistry; // non-owning
    // Member lifetime is required: LoadOrGenerateThumbnailUseCase continuations capture `this`.
    std::unique_ptr<application::LoadOrGenerateThumbnailUseCase> m_thumbnailUseCase;
    std::unique_ptr<application::LoadPageImageUseCase> m_pageLoadUseCase;

    application::ProjectSessionDescriptor m_session;
    bool m_hasSession{false};
    QHash<QString, ViewTransform> m_viewTransforms; // key: PageId string

    QFutureWatcher<application::Result<QImage>> m_pageLoadWatcher;
    quint64 m_pageLoadToken{0};
    application::OperationId m_pageLoadOperation{};

    QHash<QString, ThumbnailTicket> m_thumbnailInFlight; // key: PageId string
    QVector<domain::PageId> m_thumbnailQueue;
    QHash<QString, QFutureWatcher<application::Result<QImage>>*> m_thumbnailWatchers;
};

} // namespace manga::ui::screen::editor
```

- [ ] **Step 1: 写失败测试**

先建 `tests/unit/ui/fakes/FakeImagePorts.h`（命名空间 `manga::test`，与 `FakeProjectPorts.h` 同风格）：

```cpp
#pragma once

#include "application/ports/IPageImageLoader.h"
#include "application/ports/IThumbnailStore.h"

#include <QPromise>

#include <memory>

namespace manga::test {

/// Deferred-capable thumbnail store. `readHit` controls whether reads succeed;
/// misses drive the use case into the generate path (loader + writeThumbnail).
class FakeThumbnailStore final : public application::IThumbnailStore {
public:
    bool readHit{false};
    QImage readImage;
    int readCount{0};
    int writeCount{0};

    QFuture<application::Result<QImage>> readThumbnail(application::ReadThumbnailRequest,
                                                       application::OperationContext) override {
        ++readCount;
        if (readHit) {
            return application::makeReadyFuture<application::Result<QImage>>(readImage);
        }
        return application::makeFailedFuture<QImage>(
            application::makeApplicationError(application::ErrorCategory::Persistence,
                                              QStringLiteral("test.thumbnail_missing"), QStringLiteral("missing")));
    }
    QFuture<application::Result<void>> writeThumbnail(application::WriteThumbnailRequest,
                                                      application::OperationContext) override {
        ++writeCount;
        return application::makeReadyFuture<application::Result<void>>({});
    }
};

/// Deferred-capable page image loader: when `deferred` is true the test drives
/// completion through completeAll(); otherwise resolves immediately.
class FakePageImageLoader final : public application::IPageImageLoader {
public:
    QImage nextImage;
    application::ApplicationError nextError;
    bool shouldFail{false};
    bool deferred{false};
    int callCount{0};

    QFuture<application::Result<QImage>> loadPageImage(application::LoadPageImageRequest,
                                                       application::OperationContext context) override {
        ++callCount;
        auto promise = std::make_shared<QPromise<application::Result<QImage>>>();
        auto future = promise->future();
        if (deferred) {
            m_pending.append({std::move(promise), context});
            return future;
        }
        complete(promise, context);
        return future;
    }

    void completeAll() {
        const auto pending = std::exchange(m_pending, {});
        for (const auto& item : pending) {
            complete(item.promise, item.context);
        }
    }

    [[nodiscard]] int pendingCount() const { return static_cast<int>(m_pending.size()); }

private:
    struct Pending {
        std::shared_ptr<QPromise<application::Result<QImage>>> promise;
        application::OperationContext context;
    };
    void complete(const std::shared_ptr<QPromise<application::Result<QImage>>>& promise,
                  const application::OperationContext& context) {
        promise->start();
        if (context.stopToken.stop_requested()) {
            promise->addResult(std::unexpected(application::makeApplicationError(
                application::ErrorCategory::Cancelled, QStringLiteral("cancelled"), QStringLiteral("cancelled"))));
        } else if (shouldFail) {
            promise->addResult(std::unexpected(nextError));
        } else {
            promise->addResult(nextImage);
        }
        promise->finish();
    }
    QVector<Pending> m_pending;
};

} // namespace manga::test
```

`tests/unit/ui/editor_view_model_test.cpp`（辅助函数 `makeSession(int pageCount)` 构造 `ProjectSessionDescriptor`，页名 `page_100N.png`、`ImageSize{1200, 1800}`）：

```cpp
#include "ui/screen/editor/EditorViewModel.h"

#include "fakes/FakeImagePorts.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;
using manga::ui::screen::editor::EditorViewModel;
using manga::ui::screen::editor::PageImageDelivery;

namespace {

application::ProjectSessionDescriptor makeSession(int pageCount) {
    application::ProjectSessionDescriptor session;
    session.projectId = domain::ProjectId::generate();
    session.projectRootPath = QStringLiteral("D:/manga/demo");
    session.projectName = QStringLiteral("Demo Project");
    for (int i = 0; i < pageCount; ++i) {
        application::PageDescriptor page;
        page.id = domain::PageId::generate();
        page.orderIndex = i;
        page.imageSize = domain::ImageSize{1200, 1800};
        page.originalAssetPath = QStringLiteral("assets/original/page_%1.png").arg(1000 + i);
        session.pages.append(page);
    }
    return session;
}

struct Fixture {
    test::FakeThumbnailStore thumbnailStore;
    test::FakePageImageLoader imageLoader;
    application::OperationRegistry registry;
    explicit Fixture() {
        imageLoader.nextImage = QImage(64, 96, QImage::Format_RGBA8888);
        imageLoader.nextImage.fill(Qt::gray);
        thumbnailStore.readImage = QImage(16, 24, QImage::Format_RGBA8888);
    }
};

} // namespace

class EditorViewModelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<PageImageDelivery>();
    }

    void openProjectSessionSelectsFirstPageAndLoadsImage() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        QSignalSpy pageSpy(&viewModel, &EditorViewModel::pageImageReady);
        viewModel.openProjectSession(makeSession(3));
        QCOMPARE(viewModel.state().pages.size(), 3);
        QCOMPARE(viewModel.state().currentPageId, viewModel.state().pages.first().id);
        QVERIFY(viewModel.state().isPageLoading);
        QTRY_VERIFY_WITH_TIMEOUT(pageSpy.size() == 1, 2000);
        QVERIFY(!viewModel.state().isPageLoading);
        QCOMPARE(pageSpy.first().first().value<PageImageDelivery>().pageId,
                 viewModel.state().pages.first().id);
        QVERIFY(!pageSpy.first().first().value<PageImageDelivery>().hasSavedTransform);
    }

    void openEmptyProjectStaysIdle() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        QSignalSpy pageSpy(&viewModel, &EditorViewModel::pageImageReady);
        viewModel.openProjectSession(makeSession(0));
        QVERIFY(viewModel.state().pages.isEmpty());
        QVERIFY(!viewModel.state().currentPageId.isValid());
        QVERIFY(!viewModel.state().isPageLoading);
        QCOMPARE(f.imageLoader.callCount, 0);
        QCOMPARE(pageSpy.size(), 0);
    }

    void pageSwitchBumpsViewGenerationAndCancelsPreviousLoad() {
        Fixture f;
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        QSignalSpy pageSpy(&viewModel, &EditorViewModel::pageImageReady);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        const quint64 generationAfterOpen = viewModel.state().viewGeneration;
        viewModel.selectPage(session.pages.at(1).id);
        QVERIFY(viewModel.state().viewGeneration > generationAfterOpen);
        QCOMPARE(viewModel.state().currentPageId, session.pages.at(1).id);
        f.imageLoader.completeAll(); // 旧页完成：取消后静默；新页完成：投递
        QTRY_VERIFY_WITH_TIMEOUT(pageSpy.size() == 1, 2000);
        QCOMPARE(pageSpy.first().first().value<PageImageDelivery>().pageId, session.pages.at(1).id);
    }

    void cancelledLoadCompletionIsSilent() {
        Fixture f;
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        viewModel.selectPage(session.pages.at(1).id); // 取消第一页在途加载
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        QVERIFY(!viewModel.state().notice.has_value()); // 取消不产生错误提示
    }

    void failedPageLoadShowsRetryableNoticeAndRetryReloads() {
        Fixture f;
        f.imageLoader.shouldFail = true;
        f.imageLoader.nextError = application::makeApplicationError(
            application::ErrorCategory::Persistence, QStringLiteral("io.decode_failed"),
            QStringLiteral("无法解码页面图像"), application::RecoveryAction::Retry, true);
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.openProjectSession(makeSession(1));
        QTRY_VERIFY_WITH_TIMEOUT(viewModel.state().notice.has_value(), 2000);
        QVERIFY(viewModel.state().notice->retryable);
        QVERIFY(!viewModel.state().isPageLoading);

        f.imageLoader.shouldFail = false;
        QSignalSpy pageSpy(&viewModel, &EditorViewModel::pageImageReady);
        viewModel.retryPageLoad();
        QTRY_VERIFY_WITH_TIMEOUT(pageSpy.size() == 1, 2000);
        QVERIFY(!viewModel.state().notice.has_value());
    }

    void thumbnailConcurrencyIsCappedAtFourAndQueuePumps() {
        Fixture f;
        f.imageLoader.deferred = true; // store miss -> generate path -> loader 在途
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(6);
        viewModel.openProjectSession(session);
        f.imageLoader.completeAll(); // 让首页原图加载完成，不影响缩略图统计
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        for (const auto& page : session.pages) {
            viewModel.requestThumbnail(page.id, false);
        }
        // 4 个在途（loader pending），2 个在队列
        QTRY_VERIFY_WITH_TIMEOUT(f.imageLoader.pendingCount() == 4, 2000);
        f.imageLoader.completeAll(); // 完成一批，泵入剩余
        QTRY_VERIFY_WITH_TIMEOUT(f.imageLoader.pendingCount() == 2, 2000);
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(f.thumbnailStore.writeCount == 6, 2000);
    }

    void duplicateThumbnailRequestIsDeduplicated() {
        Fixture f;
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(1);
        viewModel.openProjectSession(session);
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        const auto pageId = session.pages.first().id;
        viewModel.requestThumbnail(pageId, false);
        viewModel.requestThumbnail(pageId, false); // 在途去重
        viewModel.requestThumbnail(pageId, false); // 再试一次
        QTRY_VERIFY_WITH_TIMEOUT(f.imageLoader.pendingCount() == 1, 2000);
    }

    void staleThumbnailCompletionIsDiscardedAfterPageSwitch() {
        Fixture f;
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        QSignalSpy readySpy(&viewModel, &EditorViewModel::thumbnailReady);
        QSignalSpy invalidatedSpy(&viewModel, &EditorViewModel::thumbnailsInvalidated);
        viewModel.requestThumbnail(session.pages.at(0).id, false);
        viewModel.requestThumbnail(session.pages.at(1).id, false);
        QTRY_VERIFY_WITH_TIMEOUT(f.imageLoader.pendingCount() == 2, 2000);
        viewModel.selectPage(session.pages.at(1).id); // bump viewGeneration
        QCOMPARE(invalidatedSpy.size(), 1);
        f.imageLoader.completeAll(); // 缩略图完成但 generation 已过 + 新页原图加载
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        QCOMPARE(readySpy.size(), 0); // 缩略图迟到结果被静默丢弃
    }

    void perPageViewTransformIsRestoredOnRevisit() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        QSignalSpy pageSpy(&viewModel, &EditorViewModel::pageImageReady);
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        viewModel.noteViewTransform(2.5, QPointF(300.0, 450.0));
        viewModel.selectPage(session.pages.at(1).id);
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        viewModel.selectPage(session.pages.at(0).id);
        QTRY_VERIFY_WITH_TIMEOUT(pageSpy.size() == 3, 2000);
        const auto delivery = pageSpy.at(2).first().value<PageImageDelivery>();
        QVERIFY(delivery.hasSavedTransform);
        QCOMPARE(delivery.zoom, 2.5);
        QCOMPARE(delivery.centerOnPage, QPointF(300.0, 450.0));
    }

    void closeProjectSessionResetsState() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.openProjectSession(makeSession(2));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        viewModel.closeProjectSession();
        QVERIFY(!viewModel.state().projectId.isValid());
        QVERIFY(viewModel.state().pages.isEmpty());
        QVERIFY(!viewModel.state().currentPageId.isValid());
    }
};

QTEST_GUILESS_MAIN(EditorViewModelTest)
#include "editor_view_model_test.moc"
```

注意：`openProjectSession` 内部会立刻发起首页原图加载，占用 1 次 `imageLoader.callCount`；缩略图并发测试断言用 `pendingCount()` 而非 `callCount`，规避该耦合。`QTRY_*` 宏依赖事件循环，`QTEST_GUILESS_MAIN` 满足。

- [ ] **Step 2: 运行确认失败**（`ui/screen/editor/...` 不存在，编译失败）

- [ ] **Step 3: 实现**

`EditorViewModel.cpp` 要点（按头文件成员实现）：

- 构造：空端口允许（测试注入场景），但 `openProjectSession`/`requestThumbnail` 在端口为空时立即失败并写 `notice`（`ErrorCategory::Internal` 文案），不崩溃。
- `openProjectSession`：`cancelThumbnailRequests()` + `cancelOperation(m_pageLoadOperation)` + `invalidateRequests()`；`m_session = session; m_hasSession = true; m_viewTransforms.clear();`；`updateState` 填入 `projectId/projectName/pages`（`displayName` 用 `QFileInfo(page.originalAssetPath).fileName()` 派生——纯字符串操作，无 I/O）、`++projectSessionGeneration`、`++viewGeneration`、清 notice；发 `thumbnailsInvalidated()`；非空则 `selectPage(pages.first().id)`。
- `closeProjectSession`：同上取消路径，`m_hasSession=false`，State 复位为默认 + 保留两代际号自增。
- `selectPage`：守卫 `!m_hasSession || pageId == current || findPage(pageId) == nullptr`；取消旧加载；`beginRequest()`；`updateState`：`currentPageId=pageId; isPageLoading=true; notice.reset(); ++viewGeneration; zoomFactor` 暂保持；发 `thumbnailsInvalidated()`；清空 `m_thumbnailQueue`；`startPageLoad(pageId)`。
- `startPageLoad`：`m_pageLoadOperation` 来自 `m_operationRegistry->createOperation(state.projectSessionGeneration, state.viewGeneration)`；`m_pageLoadToken = beginRequest()` 放在 selectPage 内（时序：先令牌后 watcher）。`m_pageLoadWatcher.setFuture(m_pageLoadUseCase->execute(...))`。
- `handlePageLoadFinished`：`!isRequestCurrent(m_pageLoadToken)` → return；`result = m_pageLoadWatcher.result()`；`isPageLoading=false`；成功 → 查 `m_viewTransforms`，组 `PageImageDelivery` 发出；失败：Cancelled 静默，其余 → `notice{safeMessage, retryable = error.retryable || error.recovery == RecoveryAction::Retry}`。
- `requestThumbnail(pageId, force)`：无会话/未知页/空端口 → 忽略；key 已在 `m_thumbnailInFlight` 或 `m_thumbnailQueue` 且 `!force` → return；force 且已在途 → return（不叠加）；`m_thumbnailInFlight.size() >= kMaxConcurrentThumbnails` → 入队（去重）否则 `startThumbnail`。
- `startThumbnail`：创建独立 `QFutureWatcher`（堆分配，挂到 `m_thumbnailWatchers[key]`），context 捕获当前两代际号，watcher `finished` 连接 `handleThumbnailFinished(pageId)`。
- `handleThumbnailFinished`：取回 ticket 与 watcher 并先从容器中移除（`watcher->deleteLater()`）；ticket.viewGeneration != `m_state.viewGeneration` → 丢弃，仍 `pumpThumbnailQueue()`；Cancelled → 丢弃；成功 → `Q_EMIT thumbnailReady(pageId, image)`；失败 → `Q_EMIT thumbnailFailed(pageId, safeMessage)`；最后 `pumpThumbnailQueue()`。
- `pumpThumbnailQueue`：队列非空且在途 < 4 → 逐个 `startThumbnail`。
- `noteViewTransform`：`m_hasSession && currentPageId.isValid()` 时写哈希（含 clamp 到 [0.05, 16.0] 的防御）。
- 析构：`cancelThumbnailRequests()` + 取消页加载；`m_thumbnailWatchers` 全部 `deleteLater()` 不安全（析构中）——直接 `qDeleteAll` 前先 `disconnect`，或给每个 watcher 以 `this` 为 parent（推荐：构造时 `new QFutureWatcher(this)`，析构随 Qt 父树），完成时从哈希移除但不 delete。

- [ ] **Step 4: CMake 接线**

`src/ui/CMakeLists.txt` 源列表追加 `screen/editor/EditorState.h`、`screen/editor/EditorViewModel.cpp`、`screen/editor/EditorViewModel.h`。`tests/unit/ui/CMakeLists.txt` 追加 `manga_tests_ui_editor_view_model` 目标（镜像既有块；该目标用 `QTEST_GUILESS_MAIN`，链接保持不变——`Qt6::Widgets` 仍需要，因为 `manga_ui` 的公共头链到 Widgets）。

- [ ] **Step 5: 运行确认通过**

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_editor_view_model
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex manga_tests_ui_editor_view_model
```

Expected: 10 个用例全绿。

- [ ] **Step 6: Commit**

```bash
git add src/ui/screen/editor/ tests/unit/ui/ src/ui/CMakeLists.txt
git commit -m "feat(ui): add editor view model with page and thumbnail pipelines"
```

---

### Task 6: `EditorPage` + editor 路由注册 + Home→Editor 会话接管

**Files:**
- Create: `src/ui/screen/editor/EditorPage.h`、`src/ui/screen/editor/EditorPage.cpp`
- Modify: `src/app/Application.cpp`（注册 editor 路由/导航项、接线 `projectOpened` → 会话接管 + 导航）、`src/ui/CMakeLists.txt`、`tests/unit/ui/CMakeLists.txt`
- Test: `tests/unit/ui/editor_page_test.cpp`（`manga_tests_ui_editor_page`）

**Interfaces:**
- Consumes: `EditorViewModel`（Task 5）、`PageThumbnailList`（Task 4）、`MangaCanvas`（Task 3）、`BasePage`。
- Produces: `manga::ui::screen::editor::EditorPage`（BasePage）；信号 `openProjectRequested()`、`importPagesRequested()`（由 Application 接到导航）。

**设计要点：**

- 布局：`QSplitter(Qt::Horizontal)` = `PageThumbnailList`（初始 232 px，`setCollapsible(false)`）+ `MangaCanvas`（stretch 1）。不持久化分隔条宽度（P5-D）。
- State → View：`observe` 的 `render(state)` 里——pages 有变化才 `model()->setPages(...)`（Model 自带相等短路）；`setCurrentPageId(state.currentPageId)`；占位推导：无项目 → `NoProject`（`打开或创建项目以开始编辑` / 动作 `打开项目`）；有项目 0 页 → `NoPage`（`项目中还没有页面` / 动作 `导入页面`）；`notice` → `Error`（safeMessage / `重试加载`）；`isPageLoading` → `Loading`（当前页 displayName）；否则不动画布内容状态（图像由 `pageImageReady` 投递）。**render 不做任何异步或 I/O。**
- View → ViewModel：`list.pageActivated` → `selectPage`；`list.thumbnailRequested` → `requestThumbnail(pageId, force)`；`viewModel.thumbnailReady` → `list.insertThumbnail`；`thumbnailFailed` → `list.markThumbnailFailed`；`thumbnailsInvalidated` → `list.resetLoadingThumbnails()`；`canvas.viewTransformChanged` → `noteViewTransform`；`placeholderActionActivated` → 按当前占位状态分发：`NoProject` → `openProjectRequested()`，`NoPage` → `importPagesRequested()`，`Error` → `retryPageLoad()`（EditorPage 记录最近一次占位状态做分发，画布不感知语义）。
- `pageImageReady(delivery)`：`canvas.setPageImage(delivery.image)`；`delivery.hasSavedTransform` → `setZoomFactor(delivery.zoom)` + `centerOnPage(...)`，否则 `fitToPage()`。
- 快捷键（P5-B 子集）：`QShortcut` `PageUp/PageDown` → `selectAdjacentPage(-1/+1)`（context `Qt::WidgetWithChildrenShortcut`）。其余快捷键（工具、Delete、Ctrl+S 等）随 P5-C。
- `setDefaultFocusWidget(m_canvas)`。

- [ ] **Step 1: 写失败测试** `tests/unit/ui/editor_page_test.cpp`

```cpp
#include "ui/screen/editor/EditorPage.h"

#include "fakes/FakeImagePorts.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;

namespace {
application::ProjectSessionDescriptor makeSession(int pageCount) { /* 与 editor_view_model_test.cpp 相同 */ }
}

class EditorPageTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<ui::screen::editor::PageImageDelivery>();
    }

    void noProjectShowsOpenPlaceholder() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        page.show();
        QCOMPARE(page.canvasForTest()->contentState(), ui::manga::MangaCanvas::ContentState::NoProject);
        QCOMPARE(page.pageListForTest()->model()->rowCount(), 0);
        QSignalSpy spy(&page, &ui::screen::editor::EditorPage::openProjectRequested);
        page.canvasForTest()->placeholderActionButtonForTest()->click();
        QCOMPARE(spy.size(), 1);
    }

    void openSessionPopulatesListAndCanvas() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        images.nextImage = QImage(64, 96, QImage::Format_RGBA8888);
        images.nextImage.fill(Qt::white);
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        page.resize(1000, 700);
        page.show();
        viewModel.openProjectSession(makeSession(3));
        QCOMPARE(page.pageListForTest()->model()->rowCount(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(
            page.canvasForTest()->contentState() == ui::manga::MangaCanvas::ContentState::Image, 2000);
    }

    void pageListSelectionSwitchesCurrentPage() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        images.nextImage = QImage(64, 96, QImage::Format_RGBA8888);
        images.nextImage.fill(Qt::white);
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        page.resize(1000, 700);
        page.show();
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        Q_EMIT page.pageListForTest()->pageActivated(session.pages.at(1).id);
        QCOMPARE(viewModel.state().currentPageId, session.pages.at(1).id);
    }

    void emptyProjectShowsImportPlaceholder() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        page.show();
        viewModel.openProjectSession(makeSession(0));
        QCOMPARE(page.canvasForTest()->contentState(), ui::manga::MangaCanvas::ContentState::NoPage);
        QSignalSpy spy(&page, &ui::screen::editor::EditorPage::importPagesRequested);
        page.canvasForTest()->placeholderActionButtonForTest()->click();
        QCOMPARE(spy.size(), 1);
    }
};

QTEST_MAIN(EditorPageTest)
#include "editor_page_test.moc"
```

- [ ] **Step 2: 运行确认失败**（`EditorPage.h` 不存在，编译失败）

- [ ] **Step 3: 实现**

`src/ui/screen/editor/EditorPage.h`：

```cpp
#pragma once

#include "ui/base/BasePage.h"

class QSplitter;

namespace manga::ui::manga {
class MangaCanvas;
}
namespace manga::ui::widget {
class PageThumbnailList;
}

namespace manga::ui::screen::editor {

class EditorViewModel;
struct EditorState;
struct PageImageDelivery;

/// Editor skeleton page: page thumbnail pane plus the manga canvas workspace.
/// Inspector, command bar and region editing arrive with P5-C.
class EditorPage : public base::BasePage {
    Q_OBJECT
public:
    explicit EditorPage(EditorViewModel* viewModel, QWidget* parent = nullptr);

    /// Test-only observation hooks.
    [[nodiscard]] ui::manga::MangaCanvas* canvasForTest() const { return m_canvas; }
    [[nodiscard]] ui::widget::PageThumbnailList* pageListForTest() const { return m_pageList; }

Q_SIGNALS:
    void openProjectRequested();
    void importPagesRequested();

private:
    void buildUi();
    void render(const EditorState& state);
    void handlePageImageReady(const PageImageDelivery& delivery);
    void handlePlaceholderAction();

    EditorViewModel* m_viewModel; // non-owning
    QSplitter* m_splitter{nullptr};
    ui::widget::PageThumbnailList* m_pageList{nullptr};
    ui::manga::MangaCanvas* m_canvas{nullptr};
    ui::manga::MangaCanvas::ContentState m_lastPlaceholder{ui::manga::MangaCanvas::ContentState::NoProject};
};

} // namespace manga::ui::screen::editor
```

（成员类型相对 `manga::ui::screen::editor` 解析：`ui::manga::MangaCanvas` = `manga::ui::manga::MangaCanvas`。）

`EditorPage.cpp` 按设计要点实现；`handlePlaceholderAction` 用 `m_lastPlaceholder` 分发（`render` 每次设置占位时同步更新该成员）。

- [ ] **Step 4: Application 接线 + CMake**

`src/app/Application.cpp` 的 `run()` 在 home 注册之后追加：

```cpp
auto* editorViewModel = new ui::screen::editor::EditorViewModel(
    m_context->thumbnailStore(), m_context->pageImageLoader(), m_context->operationRegistry());
auto* editorPage = new ui::screen::editor::EditorPage(editorViewModel);

ui::navigation::NavigationItemDescriptor editorItem;
editorItem.routeKey = QStringLiteral("editor");
editorItem.text = QCoreApplication::translate("Application", "编辑器");
editorItem.iconGlyph = QStringLiteral("\uE70F"); // Edit 字形；视觉微调归 P5-D
mainWindow->addNavigationItem(editorItem, QStringLiteral("main"));
mainWindow->registerRoute(QStringLiteral("editor"), editorPage, QStringLiteral("main"));

QObject::connect(homeViewModel, &ui::screen::home::HomeViewModel::projectOpened, editorViewModel,
                 &ui::screen::editor::EditorViewModel::openProjectSession);
QObject::connect(homeViewModel, &ui::screen::home::HomeViewModel::projectOpened, controller,
                 [controller](const application::ProjectSessionDescriptor&) {
                     controller->navigate(QStringLiteral("editor"));
                 });
QObject::connect(editorPage, &ui::screen::editor::EditorPage::openProjectRequested, controller,
                 [controller]() { controller->navigate(QStringLiteral("home")); });
QObject::connect(editorPage, &ui::screen::editor::EditorPage::importPagesRequested, controller,
                 [controller]() { controller->navigate(QStringLiteral("import")); });
```

（`import` 路由在 Task 8 注册；此处连接先落地，导航在路由注册前调用会被 `NavigationController` 拒绝，行为安全。）

`src/ui/CMakeLists.txt` 追加 `screen/editor/EditorPage.cpp/h`；`tests/unit/ui/CMakeLists.txt` 追加 `manga_tests_ui_editor_page` 目标。

- [ ] **Step 5: 运行确认通过**

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex "manga_tests_ui_editor|manga_tests_app"
```

Expected: 新用例全绿；`manga_tests_app_smoke` 仍通过（外壳新增 editor 页面与导航项，初始路由仍是 home）。

- [ ] **Step 6: Commit**

```bash
git add src/ui/screen/editor/ src/ui/CMakeLists.txt src/app/Application.cpp tests/unit/ui/
git commit -m "feat(ui): add editor page and wire home project handoff"
```

---

### Task 7: `ImportViewModel`（表单状态、校验、五阶段导入编排、取消与回滚）

**Files:**
- Create: `src/ui/screen/import/ImportState.h`、`src/ui/screen/import/ImportViewModel.h`、`src/ui/screen/import/ImportViewModel.cpp`
- Modify: `src/ui/CMakeLists.txt`、`tests/unit/ui/CMakeLists.txt`
- Test: `tests/unit/ui/import_view_model_test.cpp`（`manga_tests_ui_import_view_model`）

**Interfaces:**
- Consumes: `application::IProjectRepository`、`application::IComicImporter`、`application::OperationRegistry`；既有用例 `CreateProjectUseCase`/`ImportImagesUseCase`/`ApplyImportResultUseCase`/`AcknowledgeImportedAssetsUseCase`/`SaveProjectUseCase`/`CloseProjectUseCase`（全部 header-only，栈上按需构造——这几个用例的 `execute` 不在 continuation 中捕获 `this`，与缩略图用例不同）；`mapping::ProjectMapper::toSessionDescriptor`（Task 1）。
- Produces:
  - `ImportSourceEntry { QString path; QString displayName; bool isFolder; bool supported; QString errorSummary; }`
  - `ImportNotice { Severity { Info, Error }; QString message; }`
  - `ImportState`（值类型 + `operator==`）
  - `ImportViewModel`：`addSourcePaths(QVector<QString>)`、`removeSource(QString)`、`setProjectName/setSaveLocation/setReadingDirection/setSourceLanguageCode/setTargetLanguageCode`、`requestBrowseFiles()/requestBrowseFolder()/requestBrowseLocation()/requestCancel()`、`submit()`、`cancelImport()`、`dismissNotice()`；信号 `stateChanged`、`browseFilesRequested/browseFolderRequested/browseLocationRequested`、`cancelRequested`、`projectImported(ProjectSessionDescriptor)`。

**导入编排（全部经 QFutureWatcher 在 GUI 线程串联，每步前核对请求令牌）：**

```text
submit()
→ 字段级校验（名称非空/无 \/:*?"<>| 非法字符、保存位置非空、语言可解析）
→ Stage::Creating:  CreateProjectUseCase{name, projectRoot = saveLocation + '/' + name.trimmed(), langs, direction}
→ Stage::Importing: ImportImagesUseCase{descriptor.id, descriptor.path, sources 顶层路径列表}
   （文件夹由 FileSystemComicImporter 顶层展开；进度经 watcher progressValueChanged → State）
→ 本地构建 domain::ComicProject(name, descriptor.id) + setLanguages/ReadingDirection
   （与 FileSystemProjectRepository::createProject 的内部构造逐项对齐）
→ ApplyImportResultUseCase{&project}{projectId, result, gen, gen}（任一导入页非法 → 整体失败提示）
→ AcknowledgeImportedAssetsUseCase{projectId, projectRoot, batchId}（采用批次，防止清理误删）
→ Stage::Finalizing: SaveProjectUseCase{project}
→ projectImported(toSessionDescriptor(project, descriptor.path))，失败页映射进 descriptor.warnings
```

- **零页成功**：`importedPages` 为空 → 不走 apply/ack/save；`CloseProjectUseCase{projectId, projectRoot}` 释放会话锁；State 写错误 notice（优先取 `failures` 第一条 `safeMessage`，否则 `没有可导入的页面`）；不发 `projectImported`。已创建的空项目目录遗留——**没有 delete-project Port，记为已知偏差**。
- **取消**：`cancelImport()` 仅 `Stage::Importing` 可用 → `cancelOperation(m_currentOperation)`；`FileSystemComicImporter` 已保证取消时回滚本批已提交资产（`FileSystemComicImporter.cpp`「A cancelled request rolls back every asset it committed」）；watcher 完成收到 Cancelled → 静默复位到编辑态、**保留表单**；随后 `CloseProjectUseCase` 释放锁。
- **部分失败**：`failures` 非空但 `importedPages` 非空 → 正常完成，`failures` 逐条映射为 `ProjectWarning{code, safeMessage, path}` 进 descriptor.warnings（呈现归 P5-D）。
- **任一阶段失败**：State 写 notice（safeMessage），回到编辑态保留表单；`persistence.target_not_empty`（recovery=None，按 code 匹配）写 `locationError` 并把焦点引导交给 View。重试 = 用户修正后重新 submit（新 operationId，符合 RecoveryAction::Retry 语义）。
- `submit()` 重入守卫：`m_stage != Stage::None` 时直接返回。

`src/ui/screen/import/ImportState.h`：

```cpp
#pragma once

#include "domain/model/ReadingDirection.h"

#include <QString>
#include <QVector>

#include <optional>

namespace manga::ui::screen::import_ {

/// One top-level source row: an image file or an image folder.
struct ImportSourceEntry {
    QString path;
    QString displayName;
    bool isFolder{false};
    bool supported{true};
    QString errorSummary;
    bool operator==(const ImportSourceEntry&) const = default;
};

struct ImportNotice {
    enum class Severity { Info, Error };
    Severity severity{Severity::Info};
    QString message;
    bool operator==(const ImportNotice&) const = default;
};

struct ImportState {
    QVector<ImportSourceEntry> sources;
    QString projectName;
    QString saveLocation;
    domain::ReadingDirection readingDirection{domain::ReadingDirection::RightToLeft};
    QString sourceLanguageCode{QStringLiteral("ja")};
    QString targetLanguageCode{QStringLiteral("zh-Hans")};
    bool languageCaution{false}; // source == target: caution only, still allowed
    QString nameError;
    QString locationError;
    int formErrorCount{0};       // drives the disabled submit summary
    bool importInProgress{false};
    int importedPageCount{0};
    int totalPageCount{0};
    std::optional<ImportNotice> notice;
    bool operator==(const ImportState&) const = default;
};

} // namespace manga::ui::screen::import_
```

命名空间用 `import_`（`import` 是 C++ 关键字，不能作为命名空间标识符）。

`src/ui/screen/import/ImportViewModel.h` 骨架：

```cpp
#pragma once

#include "ui/base/BaseViewModel.h"
#include "ui/screen/import/ImportState.h"

#include "application/dto/ImportDto.h"
#include "application/dto/ProjectDto.h"
#include "application/operation/AsyncResult.h"

#include "domain/model/ComicProject.h"

#include <QFutureWatcher>

namespace manga::application {
class IComicImporter;
class IProjectRepository;
class OperationRegistry;
}

namespace manga::ui::screen::import_ {

/// Drives the Import page: source list, project form validation and the staged
/// create → import → apply → acknowledge → save orchestration.
class ImportViewModel : public base::BaseViewModel<ImportViewModel, ImportState> {
    Q_OBJECT
public:
    ImportViewModel(application::IProjectRepository* projectRepository,
                    application::IComicImporter* comicImporter,
                    application::OperationRegistry* operationRegistry,
                    QObject* parent = nullptr);

    void addSourcePaths(const QVector<QString>& paths);
    void removeSource(const QString& path);
    void setProjectName(const QString& name);
    void setSaveLocation(const QString& location);
    void setReadingDirection(domain::ReadingDirection direction);
    void setSourceLanguageCode(const QString& code);
    void setTargetLanguageCode(const QString& code);

    void requestBrowseFiles() { Q_EMIT browseFilesRequested(); }
    void requestBrowseFolder() { Q_EMIT browseFolderRequested(); }
    void requestBrowseLocation() { Q_EMIT browseLocationRequested(); }
    void requestCancel() { Q_EMIT cancelRequested(); }

    void submit();
    void cancelImport();
    void dismissNotice();

Q_SIGNALS:
    void stateChanged(const ImportState& state);
    void browseFilesRequested();
    void browseFolderRequested();
    void browseLocationRequested();
    void cancelRequested();
    void projectImported(const application::ProjectSessionDescriptor& session);

protected:
    void emitStateChanged() override { Q_EMIT stateChanged(m_state); }

private:
    enum class Stage { None, Creating, Importing, Finalizing };

    bool validateForm(); // writes nameError/locationError/formErrorCount/languageCaution
    void handleCreateFinished();
    void handleImportFinished();
    void handleImportProgress(int value, int maximum);
    void handleSaveFinished();
    void failToEditingState(const application::ApplicationError& error);
    void releaseCreatedProject(); // CloseProjectUseCase after failed/cancelled import

    application::IProjectRepository* m_projectRepository; // non-owning
    application::IComicImporter* m_comicImporter;         // non-owning
    application::OperationRegistry* m_operationRegistry;  // non-owning

    QFutureWatcher<application::Result<application::ProjectDescriptor>> m_createWatcher;
    QFutureWatcher<application::Result<application::ImportImagesResult>> m_importWatcher;
    QFutureWatcher<application::Result<application::SaveProjectResult>> m_saveWatcher;
    QFutureWatcher<application::Result<void>> m_closeWatcher;

    Stage m_stage{Stage::None};
    application::OperationId m_currentOperation{};
    domain::ComicProject m_project;
    QString m_projectRoot;
    application::ImportImagesResult m_importResult;
};
```

`addSourcePaths` 的来源级校验：文件扩展名 ∈ `{jpg, jpeg, png, webp}`（大小写不敏感）或目录 → `supported=true`；否则该行 `supported=false` + `errorSummary = tr("不支持的格式")`（V0.1 不显示 CBZ/PDF，见 `import.md` §3.2）。重复路径（`QDir::cleanPath` 归一 + case-fold）忽略。`submit` 时存在 `supported=false` 行 → 提交级汇总 notice `有 N 项需要修正` 并聚焦引导。

- [ ] **Step 1: 写失败测试** `tests/unit/ui/import_view_model_test.cpp`

复用 `tests/unit/application/FakeComicImporter.h`（include 目录方式，见 Step 4）与 `tests/unit/ui/fakes/FakeProjectPorts.h`（Task 1 已扩展）。辅助：构造 `ImportImagesResult` 含 N 个 `ImportedPage`（`domain::ComicPage(PageId::generate(), "assets/original/page_N.png", {1200,1800}, i)`）与有效 `batchId = OperationId::generate()`。

```cpp
#include "ui/screen/import/ImportViewModel.h"

#include "fakes/FakeProjectPorts.h"
#include "FakeComicImporter.h" // tests/unit/application，经 include 目录复用

#include <QSignalSpy>
#include <QtTest>

using namespace manga;
using manga::ui::screen::import_::ImportViewModel;

namespace {

application::ImportImagesResult makeImportResult(int pageCount, int failureCount = 0) {
    application::ImportImagesResult result;
    result.batchId = application::OperationId::generate();
    for (int i = 0; i < pageCount; ++i) {
        application::ImportedPage page;
        page.page = domain::ComicPage(domain::PageId::generate(),
                                      "assets/original/page_" + std::to_string(1000 + i) + ".png",
                                      domain::ImageSize{1200, 1800}, i);
        page.sourcePath = QStringLiteral("D:/src/page_%1.png").arg(i);
        result.importedPages.append(page);
    }
    for (int i = 0; i < failureCount; ++i) {
        application::ImportFailure failure;
        failure.path = QStringLiteral("D:/src/broken_%1.png").arg(i);
        failure.error = application::makeApplicationError(application::ErrorCategory::Input,
                                                          QStringLiteral("input.decode_failed"),
                                                          QStringLiteral("无法解码图像"));
        result.failures.append(failure);
    }
    return result;
}

struct Fixture {
    test::FakeRecentProjectsStore recents; // 未用，保持 FakeProjectPorts.h 自洽
    test::FakeProjectRepository repository;
    application::test::FakeComicImporter importer;
    application::OperationRegistry registry;
};

} // namespace

class ImportViewModelTest : public QObject {
    Q_OBJECT
private slots:
    void addSourcesValidatesFormatsAndDeduplicates() {
        Fixture f;
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        viewModel.addSourcePaths({QStringLiteral("D:/src/a.jpg"), QStringLiteral("D:/src/b.PNG"),
                                  QStringLiteral("D:/src/c.txt"), QStringLiteral("D:/src/folder")});
        // 注：FakeComicImporter 不展开文件夹；ViewModel 只做扩展名/目录形态判断。
        // 目录判断在无盘测试中以「无扩展名且不以 . 结尾」启发式过——见实现注记。
        const auto& sources = viewModel.state().sources;
        QCOMPARE(sources.size(), 4);
        QVERIFY(sources.at(0).supported);
        QVERIFY(sources.at(1).supported);
        QVERIFY(!sources.at(2).supported);
        QVERIFY(sources.at(3).isFolder);
        viewModel.addSourcePaths({QStringLiteral("D:/src/a.jpg")}); // 重复，忽略
        QCOMPARE(viewModel.state().sources.size(), 4);
    }

    void submitWithoutNameFailsValidation() {
        Fixture f;
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        viewModel.addSourcePaths({QStringLiteral("D:/src/a.jpg")});
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QVERIFY(!viewModel.state().nameError.isEmpty());
        QVERIFY(viewModel.state().formErrorCount > 0);
        QVERIFY(f.repository.lastCreatedPath.isEmpty()); // 未发起创建
    }

    void happyPathCreatesImportsSavesAndEmitsSession() {
        Fixture f;
        f.importer.nextResponse = makeImportResult(3);
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        QSignalSpy importedSpy(&viewModel, &ImportViewModel::projectImported);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("我的漫画"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(importedSpy.size() == 1, 4000);
        const auto session =
            importedSpy.first().first().value<application::ProjectSessionDescriptor>();
        QCOMPARE(session.projectName, QStringLiteral("我的漫画"));
        QCOMPARE(session.pages.size(), 3);
        QCOMPARE(session.pages.first().originalAssetPath, QStringLiteral("assets/original/page_1000.png"));
        QCOMPARE(f.importer.callCount, 1);
        QCOMPARE(f.importer.acknowledgeCallCount, 1);
        QCOMPARE(f.repository.saveCallCount, 1);
        QCOMPARE(f.repository.closeCallCount, 0);
        QVERIFY(!viewModel.state().importInProgress);
    }

    void zeroImportedPagesFailsWithoutSaving() {
        Fixture f;
        f.importer.nextResponse = makeImportResult(0, 2); // 全部失败
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        QSignalSpy importedSpy(&viewModel, &ImportViewModel::projectImported);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("空项目"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(viewModel.state().notice.has_value(), 4000);
        QCOMPARE(importedSpy.size(), 0);
        QCOMPARE(f.repository.saveCallCount, 0);
        QCOMPARE(f.importer.acknowledgeCallCount, 0);
        QTRY_VERIFY_WITH_TIMEOUT(f.repository.closeCallCount == 1, 2000); // 释放会话锁
    }

    void cancelDuringImportRollsBackAndKeepsForm() {
        Fixture f;
        f.importer.deferred = true;
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        QSignalSpy importedSpy(&viewModel, &ImportViewModel::projectImported);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("可取消"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(viewModel.state().importInProgress, 4000); // Creating 完成、Importing 中
        viewModel.cancelImport();
        f.importer.complete(); // 以 Cancelled 完成（fake 检查 stop_token）
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().importInProgress, 4000);
        QCOMPARE(importedSpy.size(), 0);
        QCOMPARE(viewModel.state().projectName, QStringLiteral("可取消")); // 表单保留
        QVERIFY(!viewModel.state().notice.has_value());                    // 取消不弹错误
        QTRY_VERIFY_WITH_TIMEOUT(f.repository.closeCallCount == 1, 2000);
    }

    void partialFailuresAreCarriedAsWarnings() {
        Fixture f;
        f.importer.nextResponse = makeImportResult(2, 1);
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        QSignalSpy importedSpy(&viewModel, &ImportViewModel::projectImported);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("部分成功"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(importedSpy.size() == 1, 4000);
        const auto session =
            importedSpy.first().first().value<application::ProjectSessionDescriptor>();
        QCOMPARE(session.pages.size(), 2);
        QCOMPARE(session.warnings.size(), 1);
        QCOMPARE(session.warnings.first().code, QStringLiteral("input.decode_failed"));
    }

    void createFailureMapsToLocationError() {
        Fixture f;
        f.repository.failCreate = true; // makeApplicationError(Persistence, "persistence.target_not_empty", ...)
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("已存在"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().locationError.isEmpty() || viewModel.state().notice.has_value(),
                                 4000);
        QCOMPARE(f.importer.callCount, 0);
    }

    void concurrentSubmitIsIgnored() {
        Fixture f;
        f.importer.deferred = true;
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("防抖"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        viewModel.submit(); // 进行中，忽略
        QTRY_VERIFY_WITH_TIMEOUT(f.importer.callCount == 1, 4000);
        QCOMPARE(f.importer.callCount, 1);
        viewModel.cancelImport();
        f.importer.complete();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().importInProgress, 4000);
    }
};

QTEST_GUILESS_MAIN(ImportViewModelTest)
#include "import_view_model_test.moc"
```

配套：`tests/unit/ui/fakes/FakeProjectPorts.h` 的 `failCreate` 分支错误改为 `makeApplicationError(ErrorCategory::Persistence, "persistence.target_not_empty", "目标目录必须为空")`（Task 1 若未落实，本 Task 补齐）。`happyPath` 中断言 `projectRoot` 拼接规则：`saveLocation + '/' + name.trimmed()`；`FakeProjectRepository::createProject` 原样回显 `request.path`，可在断言中核对。

**实现注记（目录形态启发式）：** ViewModel 不能碰文件系统，但 `import.md` 要求来源行区分文件/文件夹。P5-B 采用纯字符串启发式：路径有受支持扩展名 → 文件；无扩展名 → 按文件夹对待（导入器在导入时会真实展开并逐文件校验，错误来源行进 failures）。该启发式写进实现注释；偏差已在里程碑声明中记录（无扫描 Port）。

- [ ] **Step 2: 运行确认失败**（`ui/screen/import/...` 不存在，编译失败）

- [ ] **Step 3: 实现** `ImportViewModel.cpp` 按上方编排骨架实现；每个 `handle*Finished` 先 `isRequestCurrent`（`submit()` 时 `beginRequest()`）再读 watcher 结果；`Stage` 迁移全部经 `updateState`（`importInProgress`/`importedPageCount`/`totalPageCount`）。进度连接：`connect(&m_importWatcher, &QFutureWatcherBase::progressValueChanged, ...)` 与 `progressRangeChanged` → `handleImportProgress`。

- [ ] **Step 4: CMake 接线**

`src/ui/CMakeLists.txt` 追加 `screen/import/ImportState.h`、`screen/import/ImportViewModel.cpp/h`。`tests/unit/ui/CMakeLists.txt` 追加 `manga_tests_ui_import_view_model` 目标（镜像既有块），并追加一行：

```cmake
target_include_directories(manga_tests_ui_import_view_model
    PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/application" # 复用 FakeComicImporter.h，不复制
)
```

- [ ] **Step 5: 运行确认通过**

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Target manga_tests_ui_import_view_model
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test -Regex manga_tests_ui_import_view_model
```

Expected: 8 个用例全绿。

- [ ] **Step 6: Commit**

```bash
git add src/ui/screen/import/ tests/unit/ui/ src/ui/CMakeLists.txt
git commit -m "feat(ui): add import view model with staged import orchestration"
```

---

### Task 8: `ImportPage`（Drop Zone + 来源列表 + 设置面板）+ import 路由与 Home 导入入口

**Files:**
- Create: `src/ui/screen/import/FileDropZone.h`、`src/ui/screen/import/FileDropZone.cpp`、`src/ui/screen/import/PendingSourcesModel.h`、`src/ui/screen/import/PendingSourcesModel.cpp`、`src/ui/screen/import/ImportPage.h`、`src/ui/screen/import/ImportPage.cpp`
- Modify: `src/app/Application.cpp`（注册 import 路由/导航项、接线 `projectImported`/`cancelRequested`）、`src/ui/screen/home/HomePage.h/.cpp`（新增「新建项目/导入」主按钮 + `importRequested` 信号，见 `docs/ui/pages/home.md` §30）、`src/ui/CMakeLists.txt`、`tests/unit/ui/CMakeLists.txt`
- Test: `tests/unit/ui/import_page_test.cpp`（`manga_tests_ui_import_page`）；`tests/unit/ui/home_page_test.cpp` 追加导入按钮用例

**Interfaces:**
- Consumes: `ImportViewModel`（Task 7）、`fluent::textfields::LineEdit`、`fluent::basicinput::{Button, RadioButton, ComboBox}`、`fluent::status_info::{InfoBar, ProgressBar}`、`fluent::collections::ListView`、`BasePage`。
- Produces: `manga::ui::screen::import_::ImportPage`（BasePage）；`FileDropZone`（页面级自定义 Surface，信号 `pathsDropped(QVector<QString>)`）；`PendingSourcesModel`（QAbstractListModel，Roles：`PathRole = Qt::UserRole`、`DisplayNameRole`、`IsFolderRole`、`SupportedRole`、`ErrorSummaryRole`）。

**设计要点：**

- `FileDropZone`：`setAcceptDrops(true)`；`dragEnterEvent` 检查 `mimeData()->hasUrls()` 且每个 url 是本地文件、后缀受支持或无后缀（文件夹启发式同 Task 7）→ `acceptProposedAction()` 并进入 DragEnter 视觉态；含不支持类型 → 进入 Rejected 视觉态但仍 `ignore()`（让系统显示禁止光标）；`dropEvent` 收集 `QUrl::toLocalFile()` 发 `pathsDropped`。绘制用 `paintEvent` + `themeColorsRef()`：Rest = 1 px 虚线中性边界；DragEnter = Accent 低透明背景 + 实线边界 + 文案 `释放以添加`；Rejected = Critical 边界 + 文案 `不支持的文件类型`。内部图标/文字用子 QLabel 且 `setAttribute(Qt::WA_TransparentForMouseEvents)`，不拦截 Drop。导入进行中 `setEnabled(false)`。可访问名称说明支持来源（`setAccessibleName(tr("漫画文件拖放区，支持 JPG、PNG、WEBP 图片或图片文件夹"))`）。
- `ImportPage` 布局：`>=1200` 两栏（Source Pane 拉伸 + 设置面板固定 380 px）；`<1008` 单列滚动——P5-B 只做正常宽度两栏 + 窄宽单列两档（`updateResponsiveLayout` 按宽度切换 `QStackedLayout`/方向），其余断点档归 P5-D 视觉打磨。
- 设置面板字段（显式标签，不靠 Placeholder）：项目名称 `LineEdit`（`textChanged` → `setProjectName`）、保存位置只读 `LineEdit` + `浏览` `Button`（→ `requestBrowseLocation`）、阅读方向两个 `RadioButton`（`从右到左（日式漫画）`/`从左到右`）、源/目标语言 `ComboBox`（固定项：日本語 ja、English en、简体中文 zh-Hans、繁體中文 zh-Hant、한국어 ko；userData 存 code）。OCR 引擎/翻译 Provider **不渲染**（无契约承载，见里程碑偏差）。
- 主按钮文案推导（View 侧纯函数）：`importInProgress` → `正在导入 %1/%2`（disabled）+ `取消导入` 按钮可见；`formErrorCount > 0` → `修正问题后导入`（disabled）；sources 为空 → `创建并导入`（disabled）；否则 `创建并导入`（enabled）。`取消` 按钮始终可用 → `requestCancel()`。
- 顶部 `InfoBar`：`notice` 有值 → severity/message/`setIsOpen(true)`，关闭 → `dismissNotice()`。进度：`fluent::status_info::ProgressBar` 仅导入中可见，`setRange(0, totalPageCount)`/`setValue(importedPageCount)`。
- 文件/目录/位置选择对话框在 Page 侧响应 ViewModel 的 browse 信号（`QFileDialog::getOpenFileNames` 过滤器 `图片文件 (*.jpg *.jpeg *.png *.webp)`；`getExistingDirectory`），结果回注 ViewModel——与 HomePage 的打开对话框同模式。
- 来源列表：`ListView` + `PendingSourcesModel` + 委托绘制（类型图标字形、名称、`supported=false` 时 Critical 错误摘要文本）；行末移除热点 → `removeSource(path)`。扫描中状态不存在（无扫描 Port），行添加即终态。

- [ ] **Step 1: 写失败测试** `tests/unit/ui/import_page_test.cpp`

```cpp
#include "ui/screen/import/ImportPage.h"

#include "fakes/FakeProjectPorts.h"
#include "ui/screen/import/ImportViewModel.h"
#include "FakeComicImporter.h"

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
        QVERIFY(!page.submitButtonForTest()->isEnabled()); // 仍缺名称
        viewModel.setProjectName(QStringLiteral("项目"));
        QVERIFY(page.submitButtonForTest()->isEnabled());
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
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("进行中"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(viewModel.state().importInProgress, 4000);
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
```

`tests/unit/ui/home_page_test.cpp` 追加：

```cpp
void importButtonEmitsImportRequested() {
    FakeRecentProjectsStore recents;
    FakeProjectRepository repository;
    manga::application::OperationRegistry registry;
    manga::ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
    manga::ui::screen::home::HomePage page(&viewModel);
    page.show();
    QSignalSpy spy(&page, &manga::ui::screen::home::HomePage::importRequested);
    page.importButtonForTest()->click();
    QCOMPARE(spy.size(), 1);
}
```

- [ ] **Step 2: 运行确认失败**（`FileDropZone.h`/`ImportPage.h` 不存在，编译失败）

- [ ] **Step 3: 实现**——按设计要点落 `FileDropZone`、`PendingSourcesModel`、`ImportPage`；`HomePage` 在打开按钮旁加主按钮 `新建项目/导入`（`fluent::basicinput::Button`，`importRequested` 信号，`importButtonForTest()` 钩子）。

- [ ] **Step 4: Application 接线 + CMake**

`src/app/Application.cpp` 的 `run()` 追加：

```cpp
auto* importViewModel = new ui::screen::import_::ImportViewModel(
    m_context->projectRepository(), m_context->comicImporter(), m_context->operationRegistry());
auto* importPage = new ui::screen::import_::ImportPage(importViewModel);

ui::navigation::NavigationItemDescriptor importItem;
importItem.routeKey = QStringLiteral("import");
importItem.text = QCoreApplication::translate("Application", "导入漫画");
importItem.iconGlyph = QStringLiteral("\uE896"); // Download 字形；视觉微调归 P5-D
mainWindow->addNavigationItem(importItem, QStringLiteral("main"));
mainWindow->registerRoute(QStringLiteral("import"), importPage, QStringLiteral("main"));

QObject::connect(importViewModel, &ui::screen::import_::ImportViewModel::projectImported, editorViewModel,
                 &ui::screen::editor::EditorViewModel::openProjectSession);
QObject::connect(importViewModel, &ui::screen::import_::ImportViewModel::projectImported, controller,
                 [controller](const application::ProjectSessionDescriptor&) {
                     controller->navigate(QStringLiteral("editor"));
                 });
QObject::connect(importViewModel, &ui::screen::import_::ImportViewModel::cancelRequested, controller,
                 [controller]() { controller->navigate(QStringLiteral("home")); });
QObject::connect(homePage, &ui::screen::home::HomePage::importRequested, controller,
                 [controller]() { controller->navigate(QStringLiteral("import")); });
```

`src/ui/CMakeLists.txt` 追加 `screen/import/` 全部文件；`tests/unit/ui/CMakeLists.txt` 追加 `manga_tests_ui_import_page` 目标（同样加 `tests/unit/application` include 目录）。

- [ ] **Step 5: 运行确认通过**

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test
```

Expected: 全量测试绿（含 `manga_tests_app_smoke`：三个路由均注册，初始 home）。

- [ ] **Step 6: Commit**

```bash
git add src/ui/screen/import/ src/ui/screen/home/ src/ui/CMakeLists.txt src/app/Application.cpp tests/unit/ui/
git commit -m "feat(ui): add import page with drop zone and project form"
```

---

### Task 9: 全量验证与冒烟

**Files:**
- Modify: 无新代码；只允许修复前面任务暴露的缺陷。

- [ ] **Step 1: 全量构建**

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test -Configure
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/build.ps1 -Preset local-test
```

- [ ] **Step 2: 全量测试**

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/skills/manga-translator-build/scripts/test.ps1 -Preset local-test
```

Expected: 所有 target 全绿。

- [ ] **Step 3: 冒烟运行**

```powershell
# offscreen 冒烟（CI 等价）
$env:QT_QPA_PLATFORM="offscreen"; ./build/local-test/src/app/MangaTranslatorApp.exe --smoke-test; echo $LASTEXITCODE
# 真实窗口（人工/编排者执行，windows QPA）
./build/local-test/src/app/MangaTranslatorApp.exe
```

人工检查清单（记录结果，偏差写入 P5-D 待办）：

- 导航面板出现 `首页`、`导入漫画`、`编辑器` 三项；直接点 `编辑器` 显示无项目占位与 `打开项目` 按钮。
- Home 打开真实项目 → 自动跳转编辑器，页面列表填充，首行缩略图经 Shimmer 占位后显示；画布显示第一页原图；Ctrl+滚轮缩放、左键/中键平移、`0`/`1`/`+`/`-` 生效。
- 快速连续切页：画布不闪旧页；缩略图不错位（乱序完成不串页）。
- 编辑器空项目占位 → `导入页面` → 进入导入页；拖入图片文件夹、填名称与位置、`创建并导入` → 完成后进入编辑器且页面列表与画布正确。
- 导入中点 `取消导入`：表单保留、无错误弹条；同一位置重试导入提示目录非空（已知偏差，见验收对照）。
- 打开项目的 Toast、错误 InfoBar（不存在路径）仍工作（P5-A 回归）。
- Light/Dark（设置键 `ui/theme` = `light`/`dark` 各启动一次）：画布背景、占位文字、Shimmer、页面列表配色跟随主题。

- [ ] **Step 4: prek 全量检查**

```powershell
prek run --all-files
```

- [ ] **Step 5: Commit**（仅当有修复）

```bash
git commit -m "fix(ui): resolve issues found in P5-B full verification"
```

---

## 验收对照与遗留偏差

P5-B 完成后对照 [`phase-5-ui-editor.md`](phase-5-ui-editor.md) 与补充规格的达成情况：

- 页面列表与缩略图：`QListView + Model + Delegate`、无 `setIndexWidget`、`LoadOrGenerateThumbnailUseCase` 管线、工作线程 `QImage` → GUI 线程 `QPixmap`、64 MiB 像素字节预算、最多 4 并发、`ShimmerPainter` 占位、失败占位 + 重试、`PageId + viewGeneration` 迟到校验——已交付。
- 画布：源图像素 Scene 坐标、原图加载、适应窗口、平移、5%–1600% 缩放与离散档位、冻结 Z-Order 常量、每页 zoom+center 记忆——已交付。Region/排版相关图层只有常量没有 Item（P5-C）。
- generation 与 GUI 线程：`viewGeneration`/`projectSessionGeneration`、迟到结果静默丢弃、取消区别于失败——已交付。
- Import：图片/文件夹导入、字段/来源/提交三级校验、进度与取消、partial 继续、零页失败、成功后进编辑器——已交付。
- Home→Editor 会话接管（`ProjectSessionDescriptor`）——已交付，P5-A 遗留关闭。

显式遗留与已知偏差（不属本计划缺口）：

- CommandBar、工具状态机、Region Overlay/编辑、Inspector、Undo/Dirty/保存、行状态机/右键菜单/搜索/多选 → P5-C。
- `AppStatusTaskStrip`、Settings Surface、RecoveryAction 完整映射（`OpenSettings`/`LocateModel`/`FreeDiskSpace` 入口）、`warnings` 的 UI 呈现、视觉验收证据 → P5-D。
- **Spec 缺口（无对应 Port，需后续契约任务）**：导入来源的异步扫描（每行页数/大小渐进显示）；删除已创建项目目录的 Port（导入取消/零页后空目录遗留）。
- **P5-B 主动裁剪**：导入页 OCR 引擎/翻译 Provider 选择（`CreateProjectRequest` 无承载字段）；来源行文件夹/文件判断用字符串启发式（无扫描 Port）；页面 Pane 宽度不持久化；Shimmer 动效不响应 Reduce Motion。
- 编辑器无项目占位的 `打开项目` 导航到 Home 而非直接弹对话框（Home 是唯一项目中心入口）。
