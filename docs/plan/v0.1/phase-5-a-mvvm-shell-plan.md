# P5-A：MVVM 基础设施与应用外壳 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立 MangaTranslator 的 MVVM/UDF 基础设施、`ApplicationContext` 组合根、应用外壳（窗口 + 多 Surface 导航宿主 + 返回历史）和可用的 Home 项目中心，使应用首次以真实数据启动运行。

**Architecture:** 参照 ForgeAI 结构：CRTP `BaseViewModel`（`updateState`/`stateChanged`/`observe` 粘性回放/请求令牌）、`BasePage`（FluentElement 主题钩子 + 默认焦点 + 响应式钩子）、QtWindowKit 外壳（`WindowBase`/`TitleBar`/`NavigationWindow` + 完整导航面板栈，移植自 ForgeAI，见 Task 5 修订说明）、`NavigationController`/`NavigationHistory`（渲染成功才提交历史）。`ApplicationContext` 是唯一组合根，构造顺序遵循 `src/app/AGENTS.md`。

**Tech Stack:** C++23、Qt 6.11.1（Widgets/Test）、FluentQt v1.7.4（pin `5602f5271507972af8292c469972995401dc2e67`，公共 API）、QtWindowKit（pin `35e88f3655720ed0537c7dd1dde243bf4a70c94c`，FetchContent，窗口边框）、CMake presets `local-*`、Qt Test（offscreen）。

**Spec:** [`phase-5-ui-editor.md`](phase-5-ui-editor.md) + [`phase-5-ui-editor-supplement.md`](phase-5-ui-editor-supplement.md)（冻结契约）+ `docs/ui/README.md` 及 foundations / application-shell / home 页面规范。ForgeAI 结构参考：`F:/source/homework/ForgeAI/src/ui/{base,navigation,window}`。

## Global Constraints

每个任务的执行者都必须满足以下约束（相当于每条 Step 的隐含验收）：

- C++23、UTF-8/LF、4 空格缩进、英文标识符与注释；格式由 prek 钩子（clang-format + 空白修复）把关，钩子改写文件后必须 re-add 再提交。
- 命名空间：UI 层 `manga::ui::{base,navigation,window,screen::<name>}`，app 层 `manga::app`；头文件包含根是 `src`（如 `#include "ui/base/BaseViewModel.h"`）。
- 只允许 FluentQt 公共头：`#include <FluentQt/FluentQt.h>` 或 `<FluentQt/<Category>.h>` 伞头；禁止私有头、全局 QSS、`QProxyStyle`、`QApplication::setStyle()`。
- 已核实的 v1.7.4 符号：`fluent::windowing::Window`（`setContentWidget`、`titleBar()`、`setBackdropEffect`）、`fluent::windowing::TitleBar`（`setContentWidget`、`titleBarHeight`）、`fluent::navigation::NavigationView`（`setMainChromeWidget(QWidget*, WidgetOwnership)`、`takeMainChromeWidget()`、`contentHost()`、`setDisplayMode`、`setExpandedPaneWidth`）、`fluent::navigation::StackContentHost`（`insertPage(index, widget)`、`setCurrentIndex(index, TransitionEffect, bool animated)`、`currentIndexChanged(int)`）、`fluent::basicinput::Button`（`Button(QString, QWidget*)`、`setIconGlyph`、`setFluentStyle`）、`fluent::collections::ListView`、`fluent::status_info::{InfoBar, Toast}`。若与编译结果不符，以公共头文件为准并报告偏差。
- State 是值类型并提供 `operator==`；一次性事件（对话框、Toast、导航请求）只能走独立 Signal，不进 State。
- View 不直接访问 Repository/Store/文件系统/QSettings；ViewModel 依赖一律构造注入接口指针。
- 所有 Widget/QPixmap/ViewModel 只在 GUI 线程；`QFutureWatcher` 回调必须核对请求令牌。
- 构建与测试（Git Bash 中通过 PowerShell，先 `Enter-VsDevShell`，详见 `.agents/skills/manga-translator-build/SKILL.md`）：
  - 结构变更后重新配置：`cmake --preset local-test`
  - 构建：`cmake --build --preset local-test --parallel`
  - 聚焦测试：`ctest --preset local-test -R manga_tests_ui --output-on-failure`
  - 全量：`ctest --preset local-test --output-on-failure`
- 提交遵循 Conventional Commits（`<type>(<scope>): <summary>`，英文祈使句小写、≤72 字符）。
- 测试用假实现：不碰真实凭据/网络/用户文件；Qt Test 一律 offscreen（preset 已设 `QT_QPA_PLATFORM=offscreen`）。

## P5 里程碑划分（本 plan 只覆盖 P5-A）

- **P5-A（本计划）**：MVVM 基础设施 + 组合根 + 外壳导航 + Home 项目中心（最近项目、打开项目）。
- **P5-B**：Editor 骨架（页面列表 + 缩略图管线、画布原图加载/平移/缩放）、Import 页。
- **P5-C**：Region 编辑 + Inspector + Undo/Dirty/保存。
- **P5-D**：处理采用、`AppStatusTaskStrip`、Settings Surface 内容页、错误恢复矩阵、视觉验收。

P5-A 明确不交付：Editor/Import/Settings 页面目的地（导航只注册已实现的 Home）、Home 卡片封面缩略图（需要缩略图管线，P5-B）、导航项 Accent 指示条（P5-D 视觉打磨）、打开项目后的编辑器跳转（P5-B 接入 `presentRoute("editor")`）。这些偏差在 P5-D 视觉验收时对照 spec 关闭。

---

### Task 1: `BaseViewModel` CRTP 基类

**Files:**
- Create: `src/ui/base/BaseViewModel.h`
- Test: `tests/unit/ui/base_view_model_test.cpp`
- Modify: `src/ui/CMakeLists.txt`、`tests/unit/ui/CMakeLists.txt`

**Interfaces:**
- Produces: `manga::ui::base::BaseViewModel<Derived, State>`，公共 API：`state()`、`observe(context, slot)`（粘性回放 + UniqueConnection）、`beginRequest()`、`isRequestCurrent(id)`、`invalidateRequests()`；保护 API：`updateState(updater)`、`updateState(const State&)`、`emitStateChanged() = 0`。

- [ ] **Step 1: 写失败测试** `tests/unit/ui/base_view_model_test.cpp`

```cpp
#include "ui/base/BaseViewModel.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

struct CounterState {
    int value{0};
    QString label;
    bool operator==(const CounterState&) const = default;
};

class CounterViewModel : public manga::ui::base::BaseViewModel<CounterViewModel, CounterState> {
    Q_OBJECT
public:
    using BaseViewModel::BaseViewModel;
    void setValue(int value) { updateState([value](CounterState& state) { state.value = value; }); }
    void replace(const CounterState& next) { updateState(next); }
    quint64 begin() { return beginRequest(); }
    using BaseViewModel::isRequestCurrent;
    using BaseViewModel::invalidateRequests;
signals:
    void stateChanged(const CounterState& state);
protected:
    void emitStateChanged() override { Q_EMIT stateChanged(m_state); }
};

class Probe : public QObject {
    Q_OBJECT
public:
    int replayCount{0};
    CounterState last;
public slots:
    void render(const CounterState& state) { ++replayCount; last = state; }
};

} // namespace

class BaseViewModelTest : public QObject {
    Q_OBJECT
private slots:
    void observeReplaysCurrentStateImmediately() {
        CounterViewModel viewModel;
        viewModel.setValue(7);
        Probe probe;
        viewModel.observe(&probe, &Probe::render);
        QCOMPARE(probe.replayCount, 1);
        QCOMPARE(probe.last.value, 7);
    }

    void equalStateIsSuppressed() {
        CounterViewModel viewModel;
        QSignalSpy spy(&viewModel, &CounterViewModel::stateChanged);
        viewModel.setValue(1);
        viewModel.setValue(1); // 相等，抑制
        viewModel.replace(CounterState{});
        viewModel.replace(CounterState{});
        QCOMPARE(spy.count(), 2);
    }

    void partialAndFullUpdateSequence() {
        CounterViewModel viewModel;
        Probe probe;
        viewModel.observe(&probe, &Probe::render);
        viewModel.setValue(3);
        CounterState next;
        next.value = 9;
        next.label = QStringLiteral("done");
        viewModel.replace(next);
        QCOMPARE(probe.replayCount, 3); // 回放 + 两次变更
        QCOMPARE(probe.last.value, 9);
        QCOMPARE(probe.last.label, QStringLiteral("done"));
    }

    void requestTokensInvalidateStaleCompletions() {
        CounterViewModel viewModel;
        const quint64 first = viewModel.begin();
        const quint64 second = viewModel.begin();
        QVERIFY(!viewModel.isRequestCurrent(first));
        QVERIFY(viewModel.isRequestCurrent(second));
        viewModel.invalidateRequests();
        QVERIFY(!viewModel.isRequestCurrent(second));
    }

    void observeTwiceDoesNotDuplicateDelivery() {
        CounterViewModel viewModel;
        Probe probe;
        viewModel.observe(&probe, &Probe::render);
        viewModel.observe(&probe, &Probe::render); // UniqueConnection 去重，但各自回放一次
        QCOMPARE(probe.replayCount, 2);
        viewModel.setValue(5);
        QCOMPARE(probe.replayCount, 3);
    }
};

QTEST_MAIN(BaseViewModelTest)
#include "base_view_model_test.moc"
```

- [ ] **Step 2: 运行确认失败**

把测试文件先加进 `tests/unit/ui/CMakeLists.txt`（替换占位 `ui_init_test.cpp`，见 Step 4 的完整 CMake），然后：

```powershell
cmake --preset local-test; cmake --build --preset local-test --parallel --target manga_tests_ui
```

Expected: 编译失败（`ui/base/BaseViewModel.h` 不存在）。这就是本步的"失败"证据。

- [ ] **Step 3: 实现** `src/ui/base/BaseViewModel.h`

```cpp
#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QThread>

#include <atomic>
#include <functional>
#include <type_traits>
#include <utility>

namespace manga::ui::base {

/// CRTP ViewModel base implementing unidirectional data flow.
///
/// Contract:
/// - State holds persistent UI data as a value type with deterministic equality.
/// - One-shot events (toast, dialog, navigation) must be separate Qt signals, never State.
/// - All methods must be called on the GUI thread.
template <typename Derived, typename State>
class BaseViewModel : public QObject {
public:
    explicit BaseViewModel(QObject* parent = nullptr) : QObject(parent) {}
    ~BaseViewModel() override = default;

    /// Current immutable state snapshot.
    [[nodiscard]] const State& state() const { return m_state; }

    /// Starts a new request: bumps the token so all older completions become stale.
    quint64 beginRequest() { return ++m_requestId; }

    /// Returns false when the completion carrying `requestId` is stale.
    [[nodiscard]] bool isRequestCurrent(quint64 requestId) const { return requestId == m_requestId; }

    /// Invalidates every in-flight request.
    void invalidateRequests() { ++m_requestId; }

    /// Subscribes `slot` to state changes with an immediate replay of the current state.
    /// A newly attached observer never waits for the next mutation.
    template <typename Context, typename Slot>
    void observe(Context* context, Slot slot) {
        Q_ASSERT_X(QThread::currentThread() == qApp->thread(), "BaseViewModel",
                   "observe must be called on the GUI thread");
        connect(static_cast<Derived*>(this), &Derived::stateChanged, context, slot,
                static_cast<Qt::ConnectionType>(Qt::AutoConnection | Qt::UniqueConnection));
        if constexpr (std::is_member_function_pointer_v<Slot>) {
            std::invoke(slot, context, m_state);
        } else {
            std::invoke(slot, m_state);
        }
    }

protected:
    State m_state;

    /// Applies a partial update; emits stateChanged only when the snapshot actually changed.
    void updateState(const std::function<void(State&)>& updater) {
        Q_ASSERT_X(QThread::currentThread() == qApp->thread(), "BaseViewModel",
                   "updateState must be called on the GUI thread");
        State next = m_state;
        updater(next);
        if (next == m_state) {
            return;
        }
        m_state = std::move(next);
        emitStateChanged();
    }

    /// Replaces the whole snapshot with the same equality suppression.
    void updateState(const State& newState) {
        Q_ASSERT_X(QThread::currentThread() == qApp->thread(), "BaseViewModel",
                   "updateState must be called on the GUI thread");
        if (newState == m_state) {
            return;
        }
        m_state = newState;
        emitStateChanged();
    }

    /// Subclasses emit their own `stateChanged(const State&)` here.
    virtual void emitStateChanged() = 0;

private:
    std::atomic<quint64> m_requestId{0};
};

} // namespace manga::ui::base
```

- [ ] **Step 4: CMake 接线**

`src/ui/CMakeLists.txt` 改为（删除占位 `ui.cpp` 并删除该文件）：

```cmake
add_library(manga_ui STATIC
    base/BasePage.cpp
    base/BasePage.h
    base/BaseViewModel.h
)
manga_apply_project_options(manga_ui)

target_include_directories(manga_ui
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

add_subdirectory(manga)

target_link_libraries(manga_ui
    PUBLIC
        manga_application
        manga_rendering
        manga_ui_manga
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        FluentQt::FluentQt
)
```

注意：此时 `base/BasePage.*` 尚不存在（Task 2 才建），本步只在列表里放 `base/BaseViewModel.h`；Task 2 再追加 BasePage 行。`tests/unit/ui/CMakeLists.txt` 改为：

```cmake
qt_add_executable(manga_tests_ui
    base_view_model_test.cpp
)
manga_apply_project_options(manga_tests_ui)
set_target_properties(manga_tests_ui PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_ui
    PRIVATE
        manga_ui
        Qt6::Test
        Qt6::Widgets
)

manga_deploy_qt_runtime(manga_tests_ui)
add_test(NAME manga_tests_ui COMMAND manga_tests_ui)
set_tests_properties(manga_tests_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;ui"
    TIMEOUT 15
)
```

删除占位文件 `tests/unit/ui/ui_init_test.cpp`（其注释本就要求被真实测试替换）。

- [ ] **Step 5: 运行确认通过**

```powershell
cmake --preset local-test; cmake --build --preset local-test --parallel --target manga_tests_ui
ctest --preset local-test -R manga_tests_ui --output-on-failure
```

Expected: 5 个用例全绿。

- [ ] **Step 6: Commit**

```bash
git add src/ui/base/BaseViewModel.h src/ui/CMakeLists.txt src/ui/ui.cpp tests/unit/ui/
git commit -m "feat(ui): add BaseViewModel CRTP base with sticky observe"
```

（`git add` 对 `ui.cpp` 的删除会记录为 deletion；prek 钩子若改写文件，re-add 后重新提交。）

---

### Task 2: `BasePage` 页面基类

**Files:**
- Create: `src/ui/base/BasePage.h`、`src/ui/base/BasePage.cpp`
- Test: `tests/unit/ui/base_page_test.cpp`
- Modify: `src/ui/CMakeLists.txt`（追加 BasePage 行）、`tests/unit/ui/CMakeLists.txt`（追加测试文件）

**Interfaces:**
- Consumes: FluentQt `FluentElement`（`onThemeUpdated()` 虚函数）。
- Produces: `manga::ui::base::BasePage`：`setDefaultFocusWidget(QWidget*)`、`defaultFocusWidget()`、保护虚函数 `updateResponsiveLayout(int availableWidth)`。

- [ ] **Step 1: 写失败测试** `tests/unit/ui/base_page_test.cpp`

```cpp
#include "ui/base/BasePage.h"

#include <QLineEdit>
#include <QtTest>

namespace {

class ProbePage : public manga::ui::base::BasePage {
    Q_OBJECT
public:
    using BasePage::BasePage;
    QVector<int> responsiveWidths;
    int themeUpdates{0};
    void onThemeUpdated() override {
        manga::ui::base::BasePage::onThemeUpdated();
        ++themeUpdates;
    }
protected:
    void updateResponsiveLayout(int availableWidth) override { responsiveWidths.append(availableWidth); }
};

} // namespace

class BasePageTest : public QObject {
    Q_OBJECT
private slots:
    void resizeTriggersResponsiveHook() {
        ProbePage page;
        page.resize(800, 600);
        QVERIFY(page.responsiveWidths.contains(800));
        page.resize(500, 400);
        QVERIFY(page.responsiveWidths.contains(500));
    }

    void showFocusesDefaultWidget() {
        ProbePage page;
        auto* edit = new QLineEdit(&page);
        page.setDefaultFocusWidget(edit);
        page.show();
        QTRY_VERIFY_WITH_TIMEOUT(edit->hasFocus(), 2000);
    }

    void noDefaultFocusIsHarmless() {
        ProbePage page;
        page.show(); // 不设置默认焦点，不得崩溃
        QVERIFY(page.defaultFocusWidget() == nullptr);
    }
};

QTEST_MAIN(BasePageTest)
#include "base_page_test.moc"
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --preset local-test; cmake --build --preset local-test --parallel --target manga_tests_ui
```

Expected: 编译失败（`ui/base/BasePage.h` 不存在）。

- [ ] **Step 3: 实现**

`src/ui/base/BasePage.h`：

```cpp
#pragma once

#include <FluentQt/Foundation.h>

#include <QPointer>
#include <QWidget>

class QResizeEvent;
class QShowEvent;

namespace manga::ui::base {

/// Base class for full-screen pages: theme refresh, default focus and a responsive hook.
class BasePage : public QWidget, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
public:
    explicit BasePage(QWidget* parent = nullptr);
    ~BasePage() override = default;

    void onThemeUpdated() override;

    /// Widget focused automatically when the page is shown.
    void setDefaultFocusWidget(QWidget* widget);
    [[nodiscard]] QWidget* defaultFocusWidget() const { return m_defaultFocusWidget.data(); }

protected:
    /// Called from resizeEvent with the page's current width in pixels.
    virtual void updateResponsiveLayout(int availableWidth);

    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QPointer<QWidget> m_defaultFocusWidget;
};

} // namespace manga::ui::base
```

`src/ui/base/BasePage.cpp`：

```cpp
#include "ui/base/BasePage.h"

#include <QResizeEvent>
#include <QShowEvent>

namespace manga::ui::base {

BasePage::BasePage(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAutoFillBackground(false);
}

void BasePage::onThemeUpdated() {
    QWidget::update();
}

void BasePage::setDefaultFocusWidget(QWidget* widget) {
    m_defaultFocusWidget = widget;
}

void BasePage::updateResponsiveLayout(int availableWidth) {
    Q_UNUSED(availableWidth);
}

void BasePage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateResponsiveLayout(width());
}

void BasePage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (m_defaultFocusWidget && m_defaultFocusWidget->isVisibleTo(this)) {
        m_defaultFocusWidget->setFocus();
    }
}

} // namespace manga::ui::base
```

- [ ] **Step 4: CMake 追加**

`src/ui/CMakeLists.txt` 源列表追加 `base/BasePage.cpp`、`base/BasePage.h`；`tests/unit/ui/CMakeLists.txt` 追加 `base_page_test.cpp`。

- [ ] **Step 5: 运行确认通过**

```powershell
cmake --preset local-test; cmake --build --preset local-test --parallel --target manga_tests_ui
ctest --preset local-test -R manga_tests_ui --output-on-failure
```

Expected: 两个测试文件共 8 个用例全绿。若 `showFocusesDefaultWidget` 在 offscreen 平台焦点不稳定，改为 `QTRY_VERIFY_WITH_TIMEOUT(QApplication::focusWidget() == edit, 2000)`；仍不稳定则在测试中注释原因并改为验证 `defaultFocusWidget()` 与 show 后无崩溃，把焦点行为留到视觉验收。

- [ ] **Step 6: Commit**

```bash
git add src/ui/base/ src/ui/CMakeLists.txt tests/unit/ui/
git commit -m "feat(ui): add BasePage with theme, focus and responsive hooks"
```

---

### Task 3: `ApplicationContext` 组合根 + `Application` 启动

**Files:**
- Create: `src/app/ApplicationContext.h`、`src/app/ApplicationContext.cpp`、`src/app/Application.h`、`src/app/Application.cpp`
- Modify: `src/app/main.cpp`（瘦身）、`src/app/CMakeLists.txt`（新增 `manga_app` 静态库）、`tests/unit/CMakeLists.txt`（追加 `app`）、Create `tests/unit/app/CMakeLists.txt`
- Test: `tests/unit/app/application_context_test.cpp`

**Interfaces:**
- Consumes: `data::FileSystemProjectRepository`（需要 `ProjectIoCoordinator*`）、`data::QSettingsAppSettingsStore`、`data::QSettingsRecentProjectsStore`、`application::OperationRegistry`。
- Produces: `manga::app::ApplicationContext`（生产构造 + 注入构造）与各访问器；`manga::app::Application`：`Application(int&, char**)` + `int run()`。

- [ ] **Step 1: 写失败测试** `tests/unit/app/application_context_test.cpp`

```cpp
#include "app/ApplicationContext.h"

#include "application/operation/AsyncResult.h"
#include "application/ports/IProjectRepository.h"

#include <QtTest>

namespace {

class StubProjectRepository final : public manga::application::IProjectRepository {
public:
    using namespace manga::application;
    QFuture<Result<ProjectDescriptor>> createProject(CreateProjectRequest, OperationContext) override {
        return makeFailedFuture<ProjectDescriptor>({});
    }
    QFuture<Result<OpenProjectResult>> openProject(OpenProjectRequest, OperationContext) override {
        return makeFailedFuture<OpenProjectResult>({});
    }
    QFuture<Result<OpenProjectResult>> recoverProject(OpenProjectRequest, OperationContext) override {
        return makeFailedFuture<OpenProjectResult>({});
    }
    QFuture<Result<SaveProjectResult>> saveProject(SaveProjectRequest, OperationContext) override {
        return makeFailedFuture<SaveProjectResult>({});
    }
    QFuture<Result<SaveAsProjectResult>> saveProjectAs(SaveAsProjectRequest, OperationContext) override {
        return makeFailedFuture<SaveAsProjectResult>({});
    }
    QFuture<Result<void>> closeProject(CloseProjectRequest, OperationContext) override {
        return makeReadyFuture<void>({});
    }
};

class StubRecentProjectsStore final : public manga::application::IRecentProjectsStore {
public:
    [[nodiscard]] QVector<manga::application::RecentProject> loadRecentProjects() const override { return {}; }
    void addRecentProject(const manga::application::RecentProject&) override {}
    void removeRecentProject(const QString&) override {}
    void clearRecentProjects() override {}
};

class StubAppSettingsStore final : public manga::application::IAppSettingsStore {
public:
    [[nodiscard]] std::optional<QVariant> readSetting(const QString&) const override { return std::nullopt; }
    void writeSetting(const QString&, const QVariant&) override {}
    void removeSetting(const QString&) override {}
};

} // namespace

class ApplicationContextTest : public QObject {
    Q_OBJECT
private slots:
    void injectedPortsAreExposed() {
        auto repository = std::make_unique<StubProjectRepository>();
        auto recents = std::make_unique<StubRecentProjectsStore>();
        auto settings = std::make_unique<StubAppSettingsStore>();
        auto* repositoryPtr = repository.get();
        auto* recentsPtr = recents.get();
        auto* settingsPtr = settings.get();

        manga::app::ApplicationContext context(std::move(repository), std::move(recents), std::move(settings));
        QCOMPARE(context.projectRepository(), repositoryPtr);
        QCOMPARE(context.recentProjectsStore(), recentsPtr);
        QCOMPARE(context.appSettingsStore(), settingsPtr);
        QVERIFY(context.operationRegistry() != nullptr);
    }

    void shutdownIsIdempotent() {
        manga::app::ApplicationContext context(std::make_unique<StubProjectRepository>(),
                                               std::make_unique<StubRecentProjectsStore>(),
                                               std::make_unique<StubAppSettingsStore>());
        context.shutdown();
        context.shutdown(); // 第二次不得崩溃
        QVERIFY(true);
    }
};

QTEST_GUILESS_MAIN(ApplicationContextTest)
#include "application_context_test.moc"
```

注意 `makeFailedFuture<T>({})` 依赖 `ApplicationError` 可默认构造；若不可默认构造，改为 `makeApplicationError(ErrorCategory::Internal, "test.stub", "stub")`（`application/error/ApplicationError.h`）。执行者以编译结果为准。

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --preset local-test; cmake --build --preset local-test --parallel --target manga_tests_app
```

Expected: 配置或编译失败（`manga_tests_app` 目标 / `app/ApplicationContext.h` 不存在）。

- [ ] **Step 3: 实现**

`src/app/ApplicationContext.h`：

```cpp
#pragma once

#include <memory>

namespace manga::application {
class IAppSettingsStore;
class IProjectRepository;
class IRecentProjectsStore;
class OperationRegistry;
} // namespace manga::application

namespace manga::data {
class ProjectIoCoordinator;
}

namespace manga::app {

/// Composition root: the only place that constructs the production object graph.
/// Ports are exposed as interface pointers; leaf classes never query this context.
class ApplicationContext {
public:
    /// Production wiring: filesystem repository, QSettings-backed stores.
    ApplicationContext();
    /// Test wiring: inject fake ports; no production adapters are constructed.
    ApplicationContext(std::unique_ptr<application::IProjectRepository> projectRepository,
                       std::unique_ptr<application::IRecentProjectsStore> recentProjectsStore,
                       std::unique_ptr<application::IAppSettingsStore> appSettingsStore);
    ~ApplicationContext();

    ApplicationContext(const ApplicationContext&) = delete;
    ApplicationContext& operator=(const ApplicationContext&) = delete;

    [[nodiscard]] application::IProjectRepository* projectRepository() const { return m_projectRepository.get(); }
    [[nodiscard]] application::IRecentProjectsStore* recentProjectsStore() const { return m_recentProjectsStore.get(); }
    [[nodiscard]] application::IAppSettingsStore* appSettingsStore() const { return m_appSettingsStore.get(); }
    [[nodiscard]] application::OperationRegistry* operationRegistry() const { return m_operationRegistry.get(); }

    /// Deterministic reverse-order teardown entry point. Idempotent.
    void shutdown();

private:
    // Production-only members stay behind the pimpl so the injecting constructor never
    // constructs production adapters.
    struct ProductionAdapters;
    std::unique_ptr<ProductionAdapters> m_production;
    std::unique_ptr<application::IProjectRepository> m_projectRepository;
    std::unique_ptr<application::IRecentProjectsStore> m_recentProjectsStore;
    std::unique_ptr<application::IAppSettingsStore> m_appSettingsStore;
    std::unique_ptr<application::OperationRegistry> m_operationRegistry;
    bool m_shutdown{false};
};

} // namespace manga::app
```

`src/app/ApplicationContext.cpp`：

```cpp
#include "app/ApplicationContext.h"

#include "application/workflow/OperationRegistry.h"
#include "data/project/FileSystemProjectRepository.h"
#include "data/project/ProjectIoCoordinator.h"
#include "data/settings/QSettingsAppSettingsStore.h"
#include "data/settings/QSettingsRecentProjectsStore.h"

namespace manga::app {

struct ApplicationContext::ProductionAdapters {
    std::unique_ptr<data::ProjectIoCoordinator> ioCoordinator;
};

ApplicationContext::ApplicationContext()
    : m_production(std::make_unique<ProductionAdapters>()),
      m_operationRegistry(std::make_unique<application::OperationRegistry>()) {
    m_production->ioCoordinator = std::make_unique<data::ProjectIoCoordinator>();
    m_projectRepository = std::make_unique<data::FileSystemProjectRepository>(m_production->ioCoordinator.get());
    m_recentProjectsStore = std::make_unique<data::QSettingsRecentProjectsStore>();
    m_appSettingsStore = std::make_unique<data::QSettingsAppSettingsStore>();
}

ApplicationContext::ApplicationContext(std::unique_ptr<application::IProjectRepository> projectRepository,
                                       std::unique_ptr<application::IRecentProjectsStore> recentProjectsStore,
                                       std::unique_ptr<application::IAppSettingsStore> appSettingsStore)
    : m_projectRepository(std::move(projectRepository)),
      m_recentProjectsStore(std::move(recentProjectsStore)),
      m_appSettingsStore(std::move(appSettingsStore)),
      m_operationRegistry(std::make_unique<application::OperationRegistry>()) {}

ApplicationContext::~ApplicationContext() {
    shutdown();
}

void ApplicationContext::shutdown() {
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;
    // Later phases cancel in-flight operations and close the open project session here
    // before adapters are destroyed. Destruction order below is the teardown order.
}

} // namespace manga::app
```

`src/app/Application.h`：

```cpp
#pragma once

#include <memory>

class QApplication;

namespace manga::app {

class ApplicationContext;

namespace ui_placeholder {}
} // namespace manga::app

namespace manga::ui::window {
class MainWindow;
}

namespace manga::app {

/// Process lifetime owner: QApplication, composition root, main window.
class Application {
public:
    Application(int& argc, char** argv);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Returns the process exit code.
    int run();

private:
    void applyThemeFromSettings();

    std::unique_ptr<QApplication> m_app;
    std::unique_ptr<ApplicationContext> m_context;
    std::unique_ptr<ui::window::MainWindow> m_mainWindow;
    bool m_smokeTest{false};
    bool m_initFailed{false};
};

} // namespace manga::app
```

`src/app/Application.cpp`：

```cpp
#include "app/Application.h"

#include "app/ApplicationContext.h"
#include "ui/navigation/NavigationController.h"
#include "ui/navigation/NavigationPanel.h"
#include "ui/navigation/NavigationSurfaceHost.h"
#include "ui/screen/home/HomePage.h"
#include "ui/screen/home/HomeViewModel.h"
#include "ui/window/MainWindow.h"

#include <FluentQt/FluentQt.h>

#include <QApplication>
#include <QTimer>

namespace manga::app {

Application::Application(int& argc, char** argv) {
    // FluentQt constraint: High DPI must be prepared before QApplication exists.
    fluent::prepareHighDpiApplication();

    m_app = std::make_unique<QApplication>(argc, argv);
    m_app->setApplicationName(QStringLiteral("MangaTranslator"));
    m_app->setApplicationVersion(QStringLiteral("0.1.0"));
    m_app->setOrganizationName(QStringLiteral("MangaTranslator"));

    if (!fluent::initializeResources()) {
        m_initFailed = true;
        return;
    }
    m_app->setFont(Typography::fontStyle(Typography::FontRole::Body).toQFont());

    m_smokeTest = m_app->arguments().contains(QStringLiteral("--smoke-test"));

    m_context = std::make_unique<ApplicationContext>();
    // Theme is applied before any widget is constructed (window is created in run()).
    applyThemeFromSettings();
}

Application::~Application() {
    m_mainWindow.reset();
    if (m_context) {
        m_context->shutdown();
    }
    m_context.reset();
    m_app.reset();
}

void Application::applyThemeFromSettings() {
    const auto value = m_context->appSettingsStore()->readSetting(QStringLiteral("ui/theme"));
    const QString theme = value ? value->toString() : QStringLiteral("system");
    if (theme == QLatin1String("light")) {
        fluent::FluentElement::setTheme(fluent::FluentElement::Light);
    } else if (theme == QLatin1String("dark")) {
        fluent::FluentElement::setTheme(fluent::FluentElement::Dark);
    }
    // "system" leaves the FluentQt default; following OS changes arrives with the settings page.
}

int Application::run() {
    if (m_initFailed) {
        return EXIT_FAILURE;
    }

    auto* host = new ui::navigation::NavigationSurfaceHost();
    auto* controller = new ui::navigation::NavigationController(host);

    auto* homeViewModel = new ui::screen::home::HomeViewModel(
        m_context->recentProjectsStore(), m_context->projectRepository(), m_context->operationRegistry());
    auto* homePage = new ui::screen::home::HomePage(homeViewModel);

    host->registerSurface(QStringLiteral("main"),
                          new ui::navigation::NavigationPanel({
                              {QStringLiteral("home"), Application::tr("首页"), QStringLiteral("\uE80F")},
                          }));
    host->registerRoute(QStringLiteral("home"), homePage, QStringLiteral("main"));

    m_mainWindow = std::make_unique<ui::window::MainWindow>(m_context->appSettingsStore(), host, controller);
    controller->navigate(QStringLiteral("home"));
    m_mainWindow->show();

    if (m_smokeTest) {
        QTimer::singleShot(0, m_app.get(), &QCoreApplication::quit);
    }
    return m_app->exec();
}

} // namespace manga::app
```

注意：若 `fluent::FluentElement::Light/Dark` 枚举实际拼写不同（如 `Theme::Light`），以 `components/foundation/FluentElement.h` 为准。`\uE80F` 是 Fluent 图标 Home 字形；若编译期不接受 QString 字形，用 `fluent::basicinput::Button::setIconGlyph(QChar(0xE80F))` 形式。

`src/app/main.cpp` 全文替换为：

```cpp
#include "app/Application.h"

int main(int argc, char* argv[]) {
    manga::app::Application application(argc, argv);
    return application.run();
}
```

`src/app/CMakeLists.txt` 全文替换为：

```cmake
add_library(manga_app STATIC
    Application.cpp
    Application.h
    ApplicationContext.cpp
    ApplicationContext.h
)
manga_apply_project_options(manga_app)

target_include_directories(manga_app
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(manga_app
    PUBLIC
        manga_application
        manga_data
        manga_ui
        Qt6::Widgets
        FluentQt::FluentQt
)

qt_add_executable(MangaTranslatorApp
    main.cpp
)
manga_apply_project_options(MangaTranslatorApp)

target_link_libraries(MangaTranslatorApp
    PRIVATE
        manga_app
)

manga_deploy_qt_runtime(MangaTranslatorApp)
```

- [ ] **Step 4: 测试 CMake 接线**

`tests/unit/CMakeLists.txt` 末尾追加 `add_subdirectory(app)`（放在 `add_subdirectory(ui)` 之前，保持字母序）。新建 `tests/unit/app/CMakeLists.txt`：

```cmake
qt_add_executable(manga_tests_app application_context_test.cpp)
manga_apply_project_options(manga_tests_app)
set_target_properties(manga_tests_app PROPERTIES AUTOMOC ON)

target_link_libraries(manga_tests_app
    PRIVATE
        manga_app
        manga_application
        Qt6::Test
)

manga_deploy_qt_runtime(manga_tests_app)
add_test(NAME manga_tests_app COMMAND manga_tests_app)
set_tests_properties(manga_tests_app PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "unit;app"
    TIMEOUT 15
)
```

注意：Task 3 编译 `manga_app` 需要 Task 4–6 的导航/窗口/Home 头文件存在。执行顺序上本 Task 只落地 `ApplicationContext.*`、CMake 的 `manga_app` 库与测试；`Application.*` 和新 `main.cpp` 在 Task 6 完成后接线（计划顺序按此执行，避免中间态编译断裂）。据此调整：`src/app/CMakeLists.txt` 本步只加入 `ApplicationContext.cpp/h`；`Application.cpp/h`、`main.cpp` 替换在 Task 6 的 Step 5 完成。

- [ ] **Step 5: 运行确认通过**

```powershell
cmake --preset local-test; cmake --build --preset local-test --parallel --target manga_tests_app
ctest --preset local-test -R "manga_tests_app|manga_tests_ui" --output-on-failure
```

Expected: 全绿；既有 `manga_tests_app_smoke` 不受影响（`main.cpp` 尚未改）。

- [ ] **Step 6: Commit**

```bash
git add src/app/ tests/unit/CMakeLists.txt tests/unit/app/
git commit -m "feat(app): add ApplicationContext composition root with injectable ports"
```

---

### Task 4: 导航核心（类型 / 历史 / 控制器，纯逻辑）

**Files:**
- Create: `src/ui/navigation/NavigationTypes.h`、`src/ui/navigation/NavigationHistory.h`、`src/ui/navigation/NavigationHistory.cpp`、`src/ui/navigation/INavigationPresenter.h`、`src/ui/navigation/INavigationRegistrar.h`、`src/ui/navigation/NavigationController.h`、`src/ui/navigation/NavigationController.cpp`
- Test: `tests/unit/ui/navigation_controller_test.cpp`
- Modify: `src/ui/CMakeLists.txt`、`tests/unit/ui/CMakeLists.txt`

**Interfaces:**
- Consumes: 无（纯逻辑，不依赖 Widget）。
- Produces:
  - `manga::ui::navigation::NavigationDirection { None, Back, Forward }`
  - `INavigationPresenter`：`canPresentRoute(routeKey) const -> bool`、`presentRoute(routeKey, NavigationDirection) -> bool`
  - `INavigationRegistrar`：`registerSurface(surfaceId, NavigationPanel*)`、`registerRoute(routeKey, QWidget* page, surfaceId)`
  - `NavigationHistory`：`push(routeKey)`、`canGoBack()/canGoForward()`、`backTarget()/forwardTarget() -> std::optional<QString>`、`commitBack()/commitForward()`、`current() -> std::optional<QString>`、`clear()`
  - `NavigationController`（QObject）：`navigate(routeKey)`、`goBack()`、`goForward()`、`canGoBack()`；信号 `routeChanged(QString)`、`canGoBackChanged(bool)`

- [ ] **Step 1: 写失败测试** `tests/unit/ui/navigation_controller_test.cpp`

```cpp
#include "ui/navigation/NavigationController.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

class FakePresenter final : public manga::ui::navigation::INavigationPresenter {
public:
    QVector<QString> presented;
    QVector<QString> knownRoutes{QStringLiteral("home"), QStringLiteral("settings.general")};
    bool failNextPresent{false};
    manga::ui::navigation::NavigationController* reentrancyProbe{nullptr};
    QString reentrantRoute;

    bool canPresentRoute(const QString& routeKey) const override { return knownRoutes.contains(routeKey); }
    bool presentRoute(const QString& routeKey, manga::ui::navigation::NavigationDirection) override {
        if (failNextPresent) {
            failNextPresent = false;
            return false;
        }
        presented.append(routeKey);
        if (reentrancyProbe && !reentrantRoute.isEmpty()) {
            auto route = reentrantRoute;
            reentrantRoute.clear();
            reentrancyProbe->navigate(route); // 渲染回调中重入，必须被忽略
        }
        return true;
    }
};

} // namespace

class NavigationControllerTest : public QObject {
    Q_OBJECT
private slots:
    void navigateCommitsHistoryAfterRender() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        QSignalSpy routeSpy(&controller, &manga::ui::navigation::NavigationController::routeChanged);
        QSignalSpy backSpy(&controller, &manga::ui::navigation::NavigationController::canGoBackChanged);

        controller.navigate(QStringLiteral("home"));
        controller.navigate(QStringLiteral("settings.general"));
        QCOMPARE(presented.size(), 2);
        QCOMPARE(routeSpy.size(), 2);
        QVERIFY(controller.canGoBack());
        QCOMPARE(backSpy.size(), 1); // false->true 只翻转一次
    }

    void unknownRouteIsRejected() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        QSignalSpy routeSpy(&controller, &manga::ui::navigation::NavigationController::routeChanged);
        controller.navigate(QStringLiteral("missing"));
        QVERIFY(presenter.presented.isEmpty());
        QCOMPARE(routeSpy.size(), 0);
        QVERIFY(!controller.canGoBack());
    }

    void failedPresentDoesNotCommitHistory() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        controller.navigate(QStringLiteral("home"));
        presenter.failNextPresent = true;
        controller.navigate(QStringLiteral("settings.general"));
        QVERIFY(!controller.canGoBack()); // 失败的目标没有进入历史
    }

    void backAndForwardRestoreRoutes() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        controller.navigate(QStringLiteral("home"));
        controller.navigate(QStringLiteral("settings.general"));
        controller.goBack();
        QCOMPARE(presenter.presented.last(), QStringLiteral("home"));
        controller.goForward();
        QCOMPARE(presenter.presented.last(), QStringLiteral("settings.general"));
    }

    void reentrantNavigateIsIgnored() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        presenter.reentrancyProbe = &controller;
        presenter.reentrantRoute = QStringLiteral("settings.general");
        controller.navigate(QStringLiteral("home"));
        QCOMPARE(presenter.presented.size(), 1); // 重入被忽略
    }

    void sameRouteNavigateIsNoOp() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        controller.navigate(QStringLiteral("home"));
        controller.navigate(QStringLiteral("home"));
        QCOMPARE(presenter.presented.size(), 1);
    }
};

QTEST_GUILESS_MAIN(NavigationControllerTest)
#include "navigation_controller_test.moc"
```

- [ ] **Step 2: 运行确认失败**（编译失败，头文件不存在）

- [ ] **Step 3: 实现**

`src/ui/navigation/NavigationTypes.h`：

```cpp
#pragma once

#include <QString>

namespace manga::ui::navigation {

/// Direction of a presented route, used for transition selection and history bookkeeping.
enum class NavigationDirection {
    None,
    Back,
    Forward,
};

} // namespace manga::ui::navigation
```

`src/ui/navigation/NavigationHistory.h`：

```cpp
#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace manga::ui::navigation {

/// Back/forward stacks for shell navigation. Targets are committed only after the
/// presenter reports a successful render, so the history never diverges from the visible page.
class NavigationHistory {
public:
    /// Pushes a route after a successful present; drops any forward entries.
    void push(const QString& routeKey);

    [[nodiscard]] bool canGoBack() const { return m_index > 0; }
    [[nodiscard]] bool canGoForward() const { return m_index >= 0 && m_index + 1 < m_entries.size(); }

    /// Peeks at the route goBack/goForward would present. Does not mutate.
    [[nodiscard]] std::optional<QString> backTarget() const;
    [[nodiscard]] std::optional<QString> forwardTarget() const;

    /// Commits the cursor move after the back/forward present succeeded.
    void commitBack();
    void commitForward();

    [[nodiscard]] std::optional<QString> current() const;
    void clear();

private:
    QVector<QString> m_entries;
    qsizetype m_index{-1};
};

} // namespace manga::ui::navigation
```

`src/ui/navigation/NavigationHistory.cpp`：

```cpp
#include "ui/navigation/NavigationHistory.h"

namespace manga::ui::navigation {

void NavigationHistory::push(const QString& routeKey) {
    if (current() == routeKey) {
        return;
    }
    m_entries.resize(m_index + 1); // drop forward entries
    m_entries.append(routeKey);
    m_index = m_entries.size() - 1;
}

std::optional<QString> NavigationHistory::backTarget() const {
    if (!canGoBack()) {
        return std::nullopt;
    }
    return m_entries.at(m_index - 1);
}

std::optional<QString> NavigationHistory::forwardTarget() const {
    if (!canGoForward()) {
        return std::nullopt;
    }
    return m_entries.at(m_index + 1);
}

void NavigationHistory::commitBack() {
    if (canGoBack()) {
        --m_index;
    }
}

void NavigationHistory::commitForward() {
    if (canGoForward()) {
        ++m_index;
    }
}

std::optional<QString> NavigationHistory::current() const {
    if (m_index < 0 || m_index >= m_entries.size()) {
        return std::nullopt;
    }
    return m_entries.at(m_index);
}

void NavigationHistory::clear() {
    m_entries.clear();
    m_index = -1;
}

} // namespace manga::ui::navigation
```

`src/ui/navigation/INavigationPresenter.h`：

```cpp
#pragma once

#include "ui/navigation/NavigationTypes.h"

class QString;

namespace manga::ui::navigation {

/// Renders routes in a content host. Implemented by NavigationSurfaceHost.
class INavigationPresenter {
public:
    virtual ~INavigationPresenter() = default;

    [[nodiscard]] virtual bool canPresentRoute(const QString& routeKey) const = 0;
    /// Returns true when the route became visible. False means history must not be committed.
    virtual bool presentRoute(const QString& routeKey, NavigationDirection direction) = 0;
};

} // namespace manga::ui::navigation
```

`src/ui/navigation/INavigationRegistrar.h`：

```cpp
#pragma once

class QString;
class QWidget;

namespace manga::ui::navigation {

class NavigationPanel;

/// Registers navigation surfaces and routes. Implemented by NavigationSurfaceHost.
class INavigationRegistrar {
public:
    virtual ~INavigationRegistrar() = default;

    virtual void registerSurface(const QString& surfaceId, NavigationPanel* panel) = 0;
    virtual void registerRoute(const QString& routeKey, QWidget* page, const QString& surfaceId) = 0;
};

} // namespace manga::ui::navigation
```

`src/ui/navigation/NavigationController.h`：

```cpp
#pragma once

#include "ui/navigation/NavigationHistory.h"

#include <QObject>

namespace manga::ui::navigation {

class INavigationPresenter;

/// Single decision entry for shell navigation: guards reentrancy, rejects unknown routes,
/// and commits history only after the presenter rendered the route.
class NavigationController : public QObject {
    Q_OBJECT
public:
    explicit NavigationController(INavigationPresenter* presenter, QObject* parent = nullptr);

    void navigate(const QString& routeKey);
    void goBack();
    void goForward();

    [[nodiscard]] bool canGoBack() const { return m_history.canGoBack(); }
    [[nodiscard]] bool canGoForward() const { return m_history.canGoForward(); }

signals:
    void routeChanged(const QString& routeKey);
    void canGoBackChanged(bool canGoBack);
    void canGoForwardChanged(bool canGoForward);

private:
    void emitAvailabilityChanges();

    INavigationPresenter* m_presenter; // non-owning
    NavigationHistory m_history;
    bool m_navigating{false};
};

} // namespace manga::ui::navigation
```

`src/ui/navigation/NavigationController.cpp`：

```cpp
#include "ui/navigation/NavigationController.h"

#include "ui/navigation/INavigationPresenter.h"

namespace manga::ui::navigation {

NavigationController::NavigationController(INavigationPresenter* presenter, QObject* parent)
    : QObject(parent), m_presenter(presenter) {}

void NavigationController::navigate(const QString& routeKey) {
    if (m_navigating || m_presenter == nullptr || routeKey.isEmpty()) {
        return;
    }
    if (m_history.current() == routeKey) {
        return;
    }
    if (!m_presenter->canPresentRoute(routeKey)) {
        return;
    }
    m_navigating = true;
    const bool presented = m_presenter->presentRoute(routeKey, NavigationDirection::None);
    m_navigating = false;
    if (!presented) {
        return;
    }
    const bool couldGoBack = m_history.canGoBack();
    const bool couldGoForward = m_history.canGoForward();
    m_history.push(routeKey);
    Q_EMIT routeChanged(routeKey);
    if (couldGoBack != m_history.canGoBack()) {
        Q_EMIT canGoBackChanged(m_history.canGoBack());
    }
    if (couldGoForward != m_history.canGoForward()) {
        Q_EMIT canGoForwardChanged(m_history.canGoForward());
    }
}

void NavigationController::goBack() {
    const auto target = m_history.backTarget();
    if (!target || m_navigating || m_presenter == nullptr) {
        return;
    }
    m_navigating = true;
    const bool presented = m_presenter->presentRoute(*target, NavigationDirection::Back);
    m_navigating = false;
    if (!presented) {
        return;
    }
    const bool couldGoBack = m_history.canGoBack();
    const bool couldGoForward = m_history.canGoForward();
    m_history.commitBack();
    Q_EMIT routeChanged(*target);
    if (couldGoBack != m_history.canGoBack()) {
        Q_EMIT canGoBackChanged(m_history.canGoBack());
    }
    if (couldGoForward != m_history.canGoForward()) {
        Q_EMIT canGoForwardChanged(m_history.canGoForward());
    }
}

void NavigationController::goForward() {
    const auto target = m_history.forwardTarget();
    if (!target || m_navigating || m_presenter == nullptr) {
        return;
    }
    m_navigating = true;
    const bool presented = m_presenter->presentRoute(*target, NavigationDirection::Forward);
    m_navigating = false;
    if (!presented) {
        return;
    }
    const bool couldGoBack = m_history.canGoBack();
    const bool couldGoForward = m_history.canGoForward();
    m_history.commitForward();
    Q_EMIT routeChanged(*target);
    if (couldGoBack != m_history.canGoBack()) {
        Q_EMIT canGoBackChanged(m_history.canGoBack());
    }
    if (couldGoForward != m_history.canGoForward()) {
        Q_EMIT canGoForwardChanged(m_history.canGoForward());
    }
}

} // namespace manga::ui::navigation
```

- [ ] **Step 4: CMake 追加**——`src/ui/CMakeLists.txt` 源列表追加 `navigation/INavigationPresenter.h`、`navigation/INavigationRegistrar.h`、`navigation/NavigationTypes.h`、`navigation/NavigationHistory.h/.cpp`、`navigation/NavigationController.h/.cpp`；`tests/unit/ui/CMakeLists.txt` 追加 `navigation_controller_test.cpp`。

- [ ] **Step 5: 运行确认通过**

```powershell
cmake --preset local-test; cmake --build --preset local-test --parallel --target manga_tests_ui
ctest --preset local-test -R manga_tests_ui --output-on-failure
```

Expected: 6 个新用例全绿。

- [ ] **Step 6: Commit**

```bash
git add src/ui/navigation/ src/ui/CMakeLists.txt tests/unit/ui/
git commit -m "feat(ui): add navigation controller with commit-after-render history"
```

---
### Task 5（2026-08-30 修订）: QtWindowKit 外壳与 ForgeAI 窗口/导航面板移植

> **修订说明（替代原 Task 5 全文）**：产品负责人裁定 FluentQt 的 `fluent::windowing::Window` 实现有缺陷（堆损坏断言），外壳窗口改基于 **QtWindowKit**，并直接移植 ForgeAI（`F:/source/homework/ForgeAI/src/ui`）已验证的 `window/`（WindowBase/TitleBar/NavigationWindow）与 `navigation/` 完整面板栈（WinUI3 规范）。原简化版 `NavigationPanel`/`NavigationSurfaceHost`/`MainWindow`（提交 fd75fe9）由本任务删除。Task 4 交付的导航核心（Controller/History/接口）与 ForgeAI 同源同形，保留不动。

**Files:**
- Create（移植自 ForgeAI，命名空间改为 `manga::ui::window` / `manga::ui::navigation`，注释与字符串翻译为英文，行为保持一致）：
  - `src/ui/window/WindowBase.h/.cpp` ← `ForgeAI/src/ui/window/WindowBase.*`
  - `src/ui/window/TitleBar.h/.cpp` ← `ForgeAI/src/ui/window/TitleBar.*`
  - `src/ui/window/NavigationWindow.h/.cpp` ← `ForgeAI/src/ui/window/NavigationWindow.*`
  - `src/ui/navigation/NavigationPanel.h/.cpp` 及其传递依赖（`NavigationTreeWidget(Base)`、`NavigationTreeItem`、`NavigationIndicator`、`NavigationWidget`、`NavigationToolButton`/`PushButton`、`NavigationSectionHeader`、`NavigationSeparator`、`NavigationFlyout`、`NavigationMetrics`、`NavigationFocusHost` 等——按 include 图只移植实际被引用的文件）← `ForgeAI/src/ui/navigation/`
  - `src/ui/window/MainWindow.h/.cpp`：薄子类，承担几何持久化与返回接线（ForgeAI 没有该职责）
- Delete（原 Task 5 简化版，fd75fe9）：`src/ui/navigation/NavigationPanel.h/.cpp`、`src/ui/navigation/NavigationSurfaceHost.h/.cpp`、旧 `src/ui/window/MainWindow.h/.cpp`、`tests/unit/ui/navigation_host_test.cpp`、旧 `tests/unit/ui/main_window_test.cpp` 及其 CMake 目标
- Modify：
  - 根 `CMakeLists.txt`：新增 QtWindowKit 获取块（见 Step 1）
  - `src/ui/CMakeLists.txt`：`manga_ui` 链接 `QWindowKit::Widgets`，源列表以新文件替换被删文件
  - `src/ui/navigation/INavigationRegistrar.h`：若移植的 NavigationWindow 需要，补充 `addNavigationItem(const NavigationItemDescriptor&, const QString& surfaceId)` 与 `NavigationItemDescriptor`（放入 `NavigationTypes.h`，字段对齐 ForgeAI：`routeKey/iconGlyph/text/parentRouteKey/position/selectable/tooltip`，裁剪 `AnimatedVisualSource`）
  - `tests/unit/ui/CMakeLists.txt`
- Test：`tests/unit/ui/navigation_window_test.cpp`、`tests/unit/ui/main_window_test.cpp`（重写，见 Step 2）

**移植铁律（每条都是验收点）：**

- 只翻译注释/字符串、改命名空间与 include 路径；不改控制流、不删所有权守卫、不"顺手优化"。
- 必须原样保留的 ForgeAI 守卫：`NavigationWindow` 析构先断开各面板信号再清 `m_surfaces`（`destroyed()` lambda 不得触碰已销毁成员）；`WindowBase::setContentWidget` 旧内容 `deleteLater()`；hit-test 区域经 `TitleBar::hitTestWidgetChanged` 信号与 QWK 同步。
- `TitleBar` 继承 `fluent::windowing::TitleBar`（纯容器，允许）；**禁止**再使用 `fluent::windowing::Window`。
- `WindowBase` 保留 FluentQt 桥：构造内 `QWK::WidgetWindowAgent` 为子对象 + `setup(this)`；`applyPlatformWindowAttributes`（Windows: `dark-mode`/`mica`/`acrylic-material` 窗口属性，失败回退 Solid）；`fluent::windowing::publishWindowBackdropState` 回填。
- ForgeAI 源中使用其自身 `ui::` 命名空间与 `core/settings` 的部分（如主题 Provider）不移植：主题桥简化为 `WindowBase` 是 `FluentElement`，`onThemeUpdated()` 里重应用 QWK 属性与 TitleBar 主题（ForgeAI 同款机制，由 Application 侧的 `fluent::FluentElement::setTheme*` 触发）。
- 所有 FluentQt 符号以 `build/local-test/_deps/fluentqt-src` 公共头核实（ForgeAI pin ff2ee27 与本仓 5602f52 在此范围内兼容，已核实）；所有 QWK 符号以 FetchContent 拉取的 `qwindowkit-src` 公共头核实。

- [ ] **Step 1: QtWindowKit CMake 集成（先行，验证能配置编译）**

根 `CMakeLists.txt` 在 FluentQt 块后镜像追加：

```cmake
find_package(QWindowKit CONFIG QUIET)
if(NOT TARGET QWindowKit::Widgets)
    set(QWINDOWKIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(QWINDOWKIT_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(QWINDOWKIT_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(qwindowkit
        GIT_REPOSITORY https://github.com/stdware/qwindowkit.git
        GIT_TAG 35e88f3655720ed0537c7dd1dde243bf4a70c94c # pre-1.5.0, same pin as ForgeAI
        UPDATE_DISCONNECTED TRUE
    )
    FetchContent_MakeAvailable(qwindowkit)
endif()
```

注意：QWK 的实际 option 名以 `qwindowkit-src/CMakeLists.txt` 为准（如 `QWINDOWKIT_BUILD_STATIC` 等命名差异），配置失败时按其 `options` 修正并记录。`manga_ui` 链接 `QWindowKit::Widgets`。本步先提交一次：`build(cmake): add QtWindowKit dependency for window chrome`（参照 ForgeAI `CMakeLists.txt:27-30,105` 的接法）。

- [ ] **Step 2: 写失败测试**

`tests/unit/ui/navigation_window_test.cpp`（新目标 `manga_tests_ui_navigation_window`）：

```cpp
#include "ui/window/NavigationWindow.h"

#include "ui/navigation/INavigationRegistrar.h"
#include "ui/navigation/NavigationPanel.h"

#include <QtTest>

using namespace manga::ui;

class NavigationWindowTest : public QObject {
    Q_OBJECT
private slots:
    void presentRouteSwitchesPageAndSurface() {
        window::NavigationWindow window;
        auto* mainPanel = new navigation::NavigationPanel;
        auto* settingsPanel = new navigation::NavigationPanel;
        QVERIFY(window.registerSurface(QStringLiteral("main"), mainPanel));
        QVERIFY(window.registerSurface(QStringLiteral("settings"), settingsPanel));
        auto* homePage = new QWidget;
        auto* settingsPage = new QWidget;
        QVERIFY(window.registerRoute(QStringLiteral("home"), homePage, QStringLiteral("main")));
        QVERIFY(window.registerRoute(QStringLiteral("settings.general"), settingsPage, QStringLiteral("settings")));

        QVERIFY(window.canPresentRoute(QStringLiteral("home")));
        QVERIFY(!window.canPresentRoute(QStringLiteral("missing")));

        QVERIFY(window.presentRoute(QStringLiteral("home"), navigation::NavigationDirection::None));
        QVERIFY(window.presentRoute(QStringLiteral("settings.general"), navigation::NavigationDirection::None));
        // surface 切换后切回，页面与面板仍存活
        QVERIFY(window.presentRoute(QStringLiteral("home"), navigation::NavigationDirection::Back));
    }

    void unknownRouteIsRefused() {
        window::NavigationWindow window;
        QVERIFY(!window.presentRoute(QStringLiteral("missing"), navigation::NavigationDirection::None));
    }

    void panelsSurviveWindowDestructionOrder() {
        // 回归：NavigationWindow 析构不得因面板 destroyed() 信号访问已销毁成员（ForgeAI 守卫）
        auto* window = new window::NavigationWindow;
        window->registerSurface(QStringLiteral("main"), new navigation::NavigationPanel);
        window->registerRoute(QStringLiteral("home"), new QWidget, QStringLiteral("main"));
        window->show();
        QCoreApplication::processEvents();
        delete window; // 不得崩溃
        QVERIFY(true);
    }
};

QTEST_MAIN(NavigationWindowTest)
#include "navigation_window_test.moc"
```

`tests/unit/ui/main_window_test.cpp`（重写，新目标 `manga_tests_ui_main_window` 复用原名）：沿用原几何持久化测试（保存 normalGeometry/maximized、离屏钳制回退），fixture 改为 `MainWindow(IAppSettingsStore*)` + 直接向 MainWindow 注册路由；FakeSettingsStore 照旧。两个测试目标均 offscreen。构造 `NavigationPanel` 的确切参数以移植后的头文件为准（ForgeAI 版大概率是无参构造 + `addNavigationItem`）。

- [ ] **Step 3: 运行确认失败**（头文件不存在 / 目标不存在，编译或配置失败）

- [ ] **Step 4: 移植窗口栈与面板栈**

按"移植铁律"执行。`MainWindow` 薄子类：

```cpp
#pragma once

#include "ui/window/NavigationWindow.h"

namespace manga::application { class IAppSettingsStore; }
namespace manga::ui::navigation { class NavigationController; }

namespace manga::ui::window {

/// Shell window: NavigationWindow plus geometry persistence and back-button wiring.
class MainWindow : public NavigationWindow {
    Q_OBJECT
public:
    explicit MainWindow(application::IAppSettingsStore* settingsStore, QWidget* parent = nullptr);
    void setNavigationController(navigation::NavigationController* controller); // non-owning

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void restoreWindowGeometry();
    void saveWindowGeometry();

    application::IAppSettingsStore* m_settingsStore;   // non-owning
    navigation::NavigationController* m_controller{nullptr}; // non-owning
};

} // namespace manga::ui::window
```

- 几何持久化键沿用 `ui/mainWindow/normalGeometry`、`ui/mainWindow/maximized`；钳制逻辑（≥64×64 落在某可用屏幕内，否则 1280×800 主屏居中）与最大化时不覆写 normal geometry 的规则不变（沿用原 Task 5 已测试的实现逻辑，仅宿主类换成 QWK 窗口）。
- `setNavigationController`：`titleBar()->backButtonClicked` → `goBack()`；`controller->canGoBackChanged` → `titleBar()->setBackButtonVisible(canGoBack)`；`QShortcut(QKeySequence::Back, this)` → `goBack()`。
- offscreen 平台 `setBackdropEffect(BackdropEffect::Solid)`；默认 Mica。
- `setMinimumSize(640, 480)`、默认 `resize(1280, 800)`、标题 `MangaTranslator`。

- [ ] **Step 5: CMake 与清理**

`src/ui/CMakeLists.txt`：删除旧 Panel/Host/MainWindow 源，加入新窗口/面板源列表；`tests/unit/ui/CMakeLists.txt`：删除 `manga_tests_ui_navigation_host` 与旧 `manga_tests_ui_main_window` 目标，加入两个新目标。

- [ ] **Step 6: 运行确认通过**

```bash
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build/build.ps1 -Preset local-test -Configure
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build/build.ps1 -Preset local-test
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build/test.ps1 -Preset local-test
```

Expected: 全量构建与全部测试绿；特别确认 `manga_tests_ui_main_window` 与 `manga_tests_ui_navigation_window` 不再有堆断言（旧缺陷随 `fluent::windowing::Window` 移除而消失）。

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/ui/ tests/unit/ui/
git commit -m "feat(ui): port QtWindowKit window shell and navigation pane from ForgeAI"
```

---

### Task 6: Home 项目中心 + Application 接线

**Files:**
- Create: `src/ui/screen/home/RelativeTime.h`、`src/ui/screen/home/RelativeTime.cpp`、`src/ui/screen/home/RecentProjectsModel.h`、`src/ui/screen/home/RecentProjectsModel.cpp`、`src/ui/screen/home/HomeState.h`、`src/ui/screen/home/HomeViewModel.h`、`src/ui/screen/home/HomeViewModel.cpp`、`src/ui/screen/home/HomePage.h`、`src/ui/screen/home/HomePage.cpp`
- Test: `tests/unit/ui/relative_time_test.cpp`、`tests/unit/ui/home_view_model_test.cpp`、`tests/unit/ui/home_page_test.cpp`
- Modify: `src/ui/CMakeLists.txt`、`tests/unit/ui/CMakeLists.txt`、`src/app/CMakeLists.txt`（`manga_app` 加入 `Application.cpp/h`）、`src/app/main.cpp`（按 Task 3 草稿全文替换）

**Interfaces:**
- Consumes: `application::IRecentProjectsStore`（`RecentProject{name, path, lastOpenedTimeMs}`）、`application::IProjectRepository::openProject(OpenProjectRequest, OperationContext)`、`application::OperationRegistry::createOperation(sessionGeneration, viewGeneration)`、Task 1 `BaseViewModel`、Task 2 `BasePage`。
- Produces:
  - `manga::ui::screen::home::formatRelativeTime(qint64 lastOpenedMs, qint64 nowMs) -> QString`
  - `RecentProjectsModel`（QAbstractListModel，Roles：`NameRole = Qt::UserRole`、`PathRole`、`LastOpenedRole`）
  - `HomeState` / `HomeNotice`
  - `HomeViewModel`：`refresh()`、`openProjectPath(path)`、`removeRecent(path)`、`dismissNotice()`、`requestOpenProjectDialog()`；信号 `stateChanged`、`toastRequested(QString, bool isError)`、`openProjectDialogRequested()`、`projectOpened(QString projectRootPath, QString projectName)`。
  - `HomePage`（BasePage）：构造接收 `HomeViewModel*`。

- [ ] **Step 1: 写失败测试**

`tests/unit/ui/relative_time_test.cpp`：

```cpp
#include "ui/screen/home/RelativeTime.h"

#include <QtTest>

class RelativeTimeTest : public QObject {
    Q_OBJECT
private slots:
    void thresholds() {
        const qint64 now = 1'800'000'000'000LL;
        QCOMPARE(manga::ui::screen::home::formatRelativeTime(now - 30'000, now), QStringLiteral("刚刚"));
        QCOMPARE(manga::ui::screen::home::formatRelativeTime(now - 5 * 60'000, now), QStringLiteral("5 分钟前"));
        QCOMPARE(manga::ui::screen::home::formatRelativeTime(now - 3 * 3'600'000, now), QStringLiteral("3 小时前"));
        QCOMPARE(manga::ui::screen::home::formatRelativeTime(now - 4 * 86'400'000LL, now), QStringLiteral("4 天前"));
        const QString old = manga::ui::screen::home::formatRelativeTime(now - 60 * 86'400'000LL, now);
        QVERIFY(QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$")).match(old).hasMatch());
    }

    void futureTimestampIsJustNow() {
        const qint64 now = 1'800'000'000'000LL;
        QCOMPARE(manga::ui::screen::home::formatRelativeTime(now + 10'000, now), QStringLiteral("刚刚"));
    }
};

QTEST_GUILESS_MAIN(RelativeTimeTest)
#include "relative_time_test.moc"
```

`tests/unit/ui/home_view_model_test.cpp`：

```cpp
#include "ui/screen/home/HomeViewModel.h"

#include "application/dto/ProjectDto.h"
#include "application/operation/AsyncResult.h"
#include "application/ports/IProjectRepository.h"
#include "application/ports/IRecentProjectsStore.h"
#include "application/workflow/OperationRegistry.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;

namespace {

class FakeRecentProjectsStore final : public application::IRecentProjectsStore {
public:
    [[nodiscard]] QVector<application::RecentProject> loadRecentProjects() const override { return m_projects; }
    void addRecentProject(const application::RecentProject& project) override {
        m_projects.removeIf([&](const application::RecentProject& p) { return p.path == project.path; });
        m_projects.prepend(project);
    }
    void removeRecentProject(const QString& path) override {
        m_projects.removeIf([&](const application::RecentProject& p) { return p.path == path; });
    }
    void clearRecentProjects() override { m_projects.clear(); }

    QVector<application::RecentProject> m_projects;
};

class FakeProjectRepository final : public application::IProjectRepository {
public:
    bool failOpen{false};
    QString lastOpenedPath;

    QFuture<application::Result<application::ProjectDescriptor>> createProject(
        application::CreateProjectRequest, application::OperationContext) override {
        return application::makeFailedFuture<application::ProjectDescriptor>({});
    }
    QFuture<application::Result<application::OpenProjectResult>> openProject(
        application::OpenProjectRequest request, application::OperationContext) override {
        lastOpenedPath = request.path;
        if (failOpen) {
            return application::makeFailedFuture<application::OpenProjectResult>(
                application::makeApplicationError(application::ErrorCategory::Validation,
                                                  QStringLiteral("test.open_failed"),
                                                  QStringLiteral("无法打开项目")));
        }
        application::OpenProjectResult result;
        result.project = domain::ComicProject("Demo Project");
        return application::makeReadyFuture<application::OpenProjectResult>(std::move(result));
    }
    QFuture<application::Result<application::OpenProjectResult>> recoverProject(
        application::OpenProjectRequest, application::OperationContext) override {
        return application::makeFailedFuture<application::OpenProjectResult>({});
    }
    QFuture<application::Result<application::SaveProjectResult>> saveProject(
        application::SaveProjectRequest, application::OperationContext) override {
        return application::makeFailedFuture<application::SaveProjectResult>({});
    }
    QFuture<application::Result<application::SaveAsProjectResult>> saveProjectAs(
        application::SaveAsProjectRequest, application::OperationContext) override {
        return application::makeFailedFuture<application::SaveAsProjectResult>({});
    }
    QFuture<application::Result<void>> closeProject(application::CloseProjectRequest,
                                                    application::OperationContext) override {
        return application::makeReadyFuture<void>({});
    }
};

} // namespace

class HomeViewModelTest : public QObject {
    Q_OBJECT
private slots:
    void refreshLoadsRecentsIntoState() {
        FakeRecentProjectsStore recents;
        recents.m_projects = {{QStringLiteral("A"), QStringLiteral("D:/a"), 100},
                              {QStringLiteral("B"), QStringLiteral("D:/b"), 200}};
        FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        QSignalSpy spy(&viewModel, &ui::screen::home::HomeViewModel::stateChanged);
        viewModel.refresh();
        QVERIFY(spy.size() >= 1);
        QCOMPARE(viewModel.state().recentProjects.size(), 2);
    }

    void openSuccessAddsRecentAndEmitsProjectOpened() {
        FakeRecentProjectsStore recents;
        FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        QSignalSpy openedSpy(&viewModel, &ui::screen::home::HomeViewModel::projectOpened);
        QSignalSpy toastSpy(&viewModel, &ui::screen::home::HomeViewModel::toastRequested);
        viewModel.openProjectPath(QStringLiteral("D:/manga/demo"));
        QTRY_VERIFY_WITH_TIMEOUT(openedSpy.size() == 1, 2000);
        QCOMPARE(recents.m_projects.size(), 1);
        QCOMPARE(recents.m_projects.first().name, QStringLiteral("Demo Project"));
        QCOMPARE(toastSpy.size(), 1);
        QVERIFY(!viewModel.state().isOpeningProject);
        QVERIFY(!viewModel.state().notice.has_value());
    }

    void openFailureShowsErrorNoticeWithRetryPath() {
        FakeRecentProjectsStore recents;
        FakeProjectRepository repository;
        repository.failOpen = true;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        viewModel.openProjectPath(QStringLiteral("D:/manga/broken"));
        QTRY_VERIFY_WITH_TIMEOUT(viewModel.state().notice.has_value(), 2000);
        QCOMPARE(viewModel.state().notice->severity, ui::screen::home::HomeNotice::Severity::Error);
        QCOMPARE(viewModel.state().notice->retryPath, QStringLiteral("D:/manga/broken"));
        QVERIFY(!viewModel.state().isOpeningProject);
        QVERIFY(recents.m_projects.isEmpty());
    }

    void concurrentOpenIsIgnored() {
        FakeRecentProjectsStore recents;
        FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        viewModel.openProjectPath(QStringLiteral("D:/manga/demo"));
        viewModel.openProjectPath(QStringLiteral("D:/manga/other")); // 打开中，必须被忽略
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isOpeningProject, 2000);
        QCOMPARE(repository.lastOpenedPath, QStringLiteral("D:/manga/demo"));
    }

    void removeRecentUpdatesState() {
        FakeRecentProjectsStore recents;
        recents.m_projects = {{QStringLiteral("A"), QStringLiteral("D:/a"), 100}};
        FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        viewModel.refresh();
        viewModel.removeRecent(QStringLiteral("D:/a"));
        QVERIFY(viewModel.state().recentProjects.isEmpty());
    }
};

QTEST_GUILESS_MAIN(HomeViewModelTest)
#include "home_view_model_test.moc"
```

注意：`makeApplicationError`/`makeFailedFuture` 的实参形以 `application/error/ApplicationError.h`、`application/operation/AsyncResult.h` 实际签名为准；`QTRY_*` 宏在 `QTEST_GUILESS_MAIN` 下可用。若 `ComicProject` 不可拷贝/移动进 `OpenProjectResult`，按编译器诊断调整（如就地构造）。

`tests/unit/ui/home_page_test.cpp`：

```cpp
#include "ui/screen/home/HomePage.h"

#include "ui/screen/home/HomeViewModel.h"
// 复用 home_view_model_test.cpp 中的 Fake 实现；若需要共享，抽到 tests/unit/ui/fakes/ 头文件
#include "home_view_model_test_fakes.h" // 见下

#include <QtTest>

class HomePageTest : public QObject {
    Q_OBJECT
private slots:
    void emptyStateVisibleWithoutRecents() {
        FakeRecentProjectsStore recents;
        FakeProjectRepository repository;
        manga::application::OperationRegistry registry;
        manga::ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        manga::ui::screen::home::HomePage page(&viewModel);
        page.show();
        viewModel.refresh();
        QVERIFY(page.isEmptyStateVisibleForTest());
    }

    void listAppearsWithRecents() {
        FakeRecentProjectsStore recents;
        recents.m_projects = {{QStringLiteral("A"), QStringLiteral("D:/a"), 100}};
        FakeProjectRepository repository;
        manga::application::OperationRegistry registry;
        manga::ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        manga::ui::screen::home::HomePage page(&viewModel);
        page.show();
        viewModel.refresh();
        QVERIFY(!page.isEmptyStateVisibleForTest());
    }
};

QTEST_MAIN(HomePageTest)
#include "home_page_test.moc"
```

把 `FakeRecentProjectsStore`/`FakeProjectRepository` 抽到 `tests/unit/ui/fakes/FakeProjectPorts.h`（纯头文件，不进 CMake 源列表以外的特殊处理），两个测试文件共享。

- [ ] **Step 2: 运行确认失败**（`ui/screen/home/...` 不存在，编译失败）

- [ ] **Step 3: 实现**

`src/ui/screen/home/RelativeTime.h`：

```cpp
#pragma once

#include <QString>
#include <QtGlobal>

namespace manga::ui::screen::home {

/// "刚刚" / "N 分钟前" / "N 小时前" / "N 天前" / "yyyy-MM-dd" for timestamps older than 30 days.
QString formatRelativeTime(qint64 lastOpenedMs, qint64 nowMs);

} // namespace manga::ui::screen::home
```

`src/ui/screen/home/RelativeTime.cpp`：

```cpp
#include "ui/screen/home/RelativeTime.h"

#include <QDateTime>

namespace manga::ui::screen::home {

QString formatRelativeTime(qint64 lastOpenedMs, qint64 nowMs) {
    const qint64 deltaMs = qMax<qint64>(0, nowMs - lastOpenedMs);
    constexpr qint64 kMinuteMs = 60'000;
    constexpr qint64 kHourMs = 3'600'000;
    constexpr qint64 kDayMs = 86'400'000;
    if (deltaMs < kMinuteMs) {
        return QStringLiteral("刚刚");
    }
    if (deltaMs < kHourMs) {
        return QStringLiteral("%1 分钟前").arg(deltaMs / kMinuteMs);
    }
    if (deltaMs < kDayMs) {
        return QStringLiteral("%1 小时前").arg(deltaMs / kHourMs);
    }
    if (deltaMs < 30 * kDayMs) {
        return QStringLiteral("%1 天前").arg(deltaMs / kDayMs);
    }
    return QDateTime::fromMSecsSinceEpoch(lastOpenedMs).toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace manga::ui::screen::home
```

`src/ui/screen/home/HomeState.h`：

```cpp
#pragma once

#include "application/ports/IRecentProjectsStore.h"

#include <QString>
#include <QVector>

#include <optional>

namespace manga::ui::screen::home {

struct HomeNotice {
    enum class Severity { Info, Error };
    Severity severity{Severity::Info};
    QString message;
    QString retryPath; // empty means no retry action
    bool operator==(const HomeNotice&) const = default;
};

struct HomeState {
    QVector<application::RecentProject> recentProjects;
    bool isOpeningProject{false};
    std::optional<HomeNotice> notice;

    bool operator==(const HomeState& other) const {
        if (isOpeningProject != other.isOpeningProject || notice != other.notice
            || recentProjects.size() != other.recentProjects.size()) {
            return false;
        }
        for (qsizetype i = 0; i < recentProjects.size(); ++i) {
            const auto& a = recentProjects.at(i);
            const auto& b = other.recentProjects.at(i);
            if (a.name != b.name || a.path != b.path || a.lastOpenedTimeMs != b.lastOpenedTimeMs) {
                return false;
            }
        }
        return true;
    }
};

} // namespace manga::ui::screen::home
```

`src/ui/screen/home/RecentProjectsModel.h`：

```cpp
#pragma once

#include "application/ports/IRecentProjectsStore.h"

#include <QAbstractListModel>
#include <QVector>

namespace manga::ui::screen::home {

/// List model over IRecentProjectsStore snapshots. Replaced wholesale on refresh.
class RecentProjectsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { NameRole = Qt::UserRole, PathRole, LastOpenedRole };

    explicit RecentProjectsModel(QObject* parent = nullptr);

    void setProjects(const QVector<application::RecentProject>& projects);
    [[nodiscard]] const application::RecentProject* at(int row) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

private:
    QVector<application::RecentProject> m_projects;
};

} // namespace manga::ui::screen::home
```

`RecentProjectsModel.cpp`：`setProjects` 用 `beginResetModel/endResetModel`；`data`：`DisplayRole` → name、`ToolTipRole` → path、`Qt::AccessibleTextRole` → `tr("项目 %1，路径 %2，上次打开 %3")`（时间用 `formatRelativeTime(lastOpenedTimeMs, QDateTime::currentMSecsSinceEpoch())`）、自定义三个 Role 直出字段。

`src/ui/screen/home/HomeViewModel.h`：

```cpp
#pragma once

#include "ui/base/BaseViewModel.h"
#include "ui/screen/home/HomeState.h"

#include "application/operation/AsyncResult.h"

#include <QFutureWatcher>

namespace manga::application {
class IProjectRepository;
class IRecentProjectsStore;
class OperationRegistry;
struct OpenProjectResult;
} // namespace manga::application

namespace manga::ui::screen::home {

/// Drives the Home surface: recent projects list and open-project flow.
class HomeViewModel : public base::BaseViewModel<HomeViewModel, HomeState> {
    Q_OBJECT
public:
    HomeViewModel(application::IRecentProjectsStore* recentStore,
                  application::IProjectRepository* projectRepository,
                  application::OperationRegistry* operationRegistry,
                  QObject* parent = nullptr);

    void refresh();
    void openProjectPath(const QString& path);
    void removeRecent(const QString& path);
    void dismissNotice();
    void requestOpenProjectDialog() { Q_EMIT openProjectDialogRequested(); }

signals:
    void stateChanged(const HomeState& state);
    void toastRequested(const QString& message, bool isError);
    void openProjectDialogRequested();
    void projectOpened(const QString& projectRootPath, const QString& projectName);

protected:
    void emitStateChanged() override { Q_EMIT stateChanged(m_state); }

private:
    void handleOpenFinished();

    application::IRecentProjectsStore* m_recentStore;       // non-owning
    application::IProjectRepository* m_projectRepository;   // non-owning
    application::OperationRegistry* m_operationRegistry;    // non-owning
    QFutureWatcher<application::Result<application::OpenProjectResult>> m_openWatcher;
    quint64 m_openToken{0};
    QString m_pendingOpenPath;
};

} // namespace manga::ui::screen::home
```

`src/ui/screen/home/HomeViewModel.cpp`：

```cpp
#include "ui/screen/home/HomeViewModel.h"

#include "application/dto/ProjectDto.h"
#include "application/ports/IProjectRepository.h"
#include "application/ports/IRecentProjectsStore.h"
#include "application/workflow/OperationRegistry.h"

#include <QDateTime>

namespace manga::ui::screen::home {

HomeViewModel::HomeViewModel(application::IRecentProjectsStore* recentStore,
                             application::IProjectRepository* projectRepository,
                             application::OperationRegistry* operationRegistry,
                             QObject* parent)
    : base::BaseViewModel<HomeViewModel, HomeState>(parent),
      m_recentStore(recentStore),
      m_projectRepository(projectRepository),
      m_operationRegistry(operationRegistry) {
    connect(&m_openWatcher, &QFutureWatcherBase::finished, this, [this]() { handleOpenFinished(); });
}

void HomeViewModel::refresh() {
    updateState([this](HomeState& state) { state.recentProjects = m_recentStore->loadRecentProjects(); });
}

void HomeViewModel::openProjectPath(const QString& path) {
    const QString trimmed = path.trimmed();
    if (m_state.isOpeningProject || trimmed.isEmpty()) {
        return;
    }
    m_pendingOpenPath = trimmed;
    m_openToken = beginRequest();
    updateState([](HomeState& state) {
        state.isOpeningProject = true;
        state.notice.reset();
    });
    const auto context = m_operationRegistry->createOperation(0, 0);
    m_openWatcher.setFuture(
        m_projectRepository->openProject(application::OpenProjectRequest{trimmed}, context));
}

void HomeViewModel::removeRecent(const QString& path) {
    m_recentStore->removeRecentProject(path);
    refresh();
}

void HomeViewModel::dismissNotice() {
    updateState([](HomeState& state) { state.notice.reset(); });
}

void HomeViewModel::handleOpenFinished() {
    if (!isRequestCurrent(m_openToken)) {
        return; // stale completion
    }
    const auto result = m_openWatcher.result();
    updateState([](HomeState& state) { state.isOpeningProject = false; });
    if (result.has_value()) {
        const QString name = QString::fromStdString(result.value().project.name());
        m_recentStore->addRecentProject(
            {name, m_pendingOpenPath, QDateTime::currentMSecsSinceEpoch()});
        refresh();
        Q_EMIT toastRequested(tr("已打开项目「%1」").arg(name), false);
        Q_EMIT projectOpened(m_pendingOpenPath, name);
        // P5-B 在此处接 presentRoute("editor") 与编辑会话接管；warnings/recoverySource 的错误恢复矩阵归 P5-D。
    } else {
        const QString safeMessage = result.error().safeMessage;
        const QString retryPath = m_pendingOpenPath;
        updateState([&](HomeState& state) {
            state.notice = HomeNotice{HomeNotice::Severity::Error, safeMessage, retryPath};
        });
    }
}

} // namespace manga::ui::screen::home
```

`src/ui/screen/home/HomePage.h`：

```cpp
#pragma once

#include "ui/base/BasePage.h"

class QLabel;
class QStackedLayout;

namespace fluent::basicinput {
class Button;
}
namespace fluent::collections {
class ListView;
}
namespace fluent::status_info {
class InfoBar;
}

namespace manga::ui::screen::home {

class HomeViewModel;
class RecentProjectsModel;
struct HomeState;

/// Home surface: welcome header, open action, recent projects list with empty state.
class HomePage : public base::BasePage {
    Q_OBJECT
public:
    explicit HomePage(HomeViewModel* viewModel, QWidget* parent = nullptr);

    /// Test-only observation hook.
    [[nodiscard]] bool isEmptyStateVisibleForTest() const;

private:
    void buildUi();
    void render(const HomeState& state);
    void showContextMenu(const QPoint& pos);

    HomeViewModel* m_viewModel; // non-owning
    RecentProjectsModel* m_model{nullptr};
    fluent::basicinput::Button* m_openButton{nullptr};
    fluent::status_info::InfoBar* m_infoBar{nullptr};
    QStackedLayout* m_stack{nullptr};
    QWidget* m_emptyState{nullptr};
    fluent::collections::ListView* m_listView{nullptr};
};

} // namespace manga::ui::screen::home
```

`src/ui/screen/home/HomePage.cpp` 要点：

- `buildUi()`：根 `QVBoxLayout`（margins 32/24，spacing 16）；标题 `QLabel(tr("欢迎回来"))` 用 `themeFont(Typography::FontRole::Title)`（以 FluentElement 字体访问器实际 API 为准），副标题 `tr("继续漫画翻译项目，或开始新的工作。")`；`m_openButton = new fluent::basicinput::Button(tr("打开项目"), this)`；`m_infoBar` 初始关闭；`m_stack` 管理 `m_emptyState`（居中 `QLabel(tr("还没有最近项目\n点击「打开项目」开始。"))`）与 `m_listView`（`setModel(m_model)`）。
- 构造末尾：`m_viewModel->observe(this, [this](const HomeState& state) { render(state); });` `setDefaultFocusWidget(m_openButton);`
- `render(state)`：`m_model->setProjects(state.recentProjects)`；`m_stack->setCurrentWidget(state.recentProjects.isEmpty() ? m_emptyState : m_listView)`；`m_openButton->setEnabled(!state.isOpeningProject)`；InfoBar：`state.notice` 有值时设置 severity（Error→`InfoBarSeverity::Error`）、message、`setOpen(true)`，否则 `setOpen(false)`。InfoBar 的确切 API（`setSeverity`/`setMessage`/`setOpen`/`actionWidget`）以 `status_info/InfoBar.h` 为准。
- 连接：`m_openButton.clicked` → `m_viewModel->requestOpenProjectDialog()`；`m_viewModel->openProjectDialogRequested` → `QFileDialog::getExistingDirectory(this, tr("打开 MangaTranslator 项目"))`，非空则 `m_viewModel->openProjectPath(dir)`；`toastRequested` → `fluent::status_info::Toast`（按 v1.7.4 公共 API 展示，签名以头文件为准）；InfoBar action 按钮 → `m_viewModel->openProjectPath(notice.retryPath)`；InfoBar 关闭 → `dismissNotice()`；`m_listView` 双击/激活 → 以 `PathRole` 调 `openProjectPath`；`customContextMenuRequested` → `showContextMenu`：菜单项 `打开`、`从最近列表移除`（优先 `fluent::menus_toolbars::Menu`，构造不满足则退回 `QMenu` 并记录偏差）。
- 错误只在 ViewModel 合并进 State；Page 不做任何 Store/Repository 访问。

- [ ] **Step 4: 接线 Application 与 CMake**

注意：Task 3 的 `Application.cpp` 草稿基于已删除的 `NavigationSurfaceHost`/旧 `MainWindow`，已过期，以本步代码为准。`src/app/Application.cpp` 的 `run()` 改为：

```cpp
int Application::run() {
    if (m_initFailed) {
        return EXIT_FAILURE;
    }

    auto* mainWindow = new ui::window::MainWindow(m_context->appSettingsStore());
    auto* controller = new ui::navigation::NavigationController(mainWindow, mainWindow);
    mainWindow->setNavigationController(controller);

    auto* homeViewModel = new ui::screen::home::HomeViewModel(
        m_context->recentProjectsStore(), m_context->projectRepository(), m_context->operationRegistry());
    auto* homePage = new ui::screen::home::HomePage(homeViewModel);

    mainWindow->registerSurface(QStringLiteral("main"), new ui::navigation::NavigationPanel(mainWindow));
    ui::navigation::NavigationItemDescriptor homeItem;
    homeItem.routeKey = QStringLiteral("home");
    homeItem.text = tr("首页");
    homeItem.iconGlyph = QStringLiteral("\uE80F");
    mainWindow->addNavigationItem(homeItem, QStringLiteral("main"));
    mainWindow->registerRoute(QStringLiteral("home"), homePage, QStringLiteral("main"));

    m_mainWindow = std::unique_ptr<ui::window::MainWindow>(mainWindow);
    controller->navigate(QStringLiteral("home"));
    m_mainWindow->show();

    if (m_smokeTest) {
        QTimer::singleShot(0, m_app.get(), &QCoreApplication::quit);
    }
    return m_app->exec();
}
```

`addNavigationItem` 的确切签名与 `NavigationItemDescriptor` 字段以 Task 5 移植后的头文件为准；若 registrar 最终未加 `addNavigationItem`，改为直接调用面板的对应方法并在报告中记录。

1. `src/app/CMakeLists.txt` 的 `manga_app` 源列表加入 `Application.cpp`、`Application.h`；链接保持不变。
2. `src/app/main.cpp` 按 Task 3 草稿全文替换为瘦入口（该草稿不含外壳引用，仍然有效）。
3. `src/ui/CMakeLists.txt` 源列表追加 `screen/home/` 全部文件；`tests/unit/ui/CMakeLists.txt` 追加三个测试文件（每个 QTEST_MAIN 一个目标，前缀 `manga_tests_ui_`）。

- [ ] **Step 5: 运行确认通过**

```powershell
cmake --preset local-test; cmake --build --preset local-test --parallel
ctest --preset local-test -R "manga_tests_ui|manga_tests_app" --output-on-failure
```

Expected: 全绿；`manga_tests_app_smoke`（`--smoke-test` 启动真实窗口树）通过。

- [ ] **Step 6: Commit**

```bash
git add src/ui/screen/ src/ui/CMakeLists.txt src/app/ tests/unit/ui/
git commit -m "feat(ui): add home project center with recent projects and open flow"
```

---

### Task 7: 全量验证与冒烟

**Files:**
- Modify: 无新代码；只允许修复前面任务暴露的缺陷。

- [ ] **Step 1: 全新配置 + 全量构建**

```powershell
cmake --preset local-test
cmake --build --preset local-test --parallel
```

- [ ] **Step 2: 全量测试**

```powershell
ctest --preset local-test --output-on-failure
```

Expected: 所有 target 全绿（domain/application/data/rendering/ui/app）。

- [ ] **Step 3: 冒烟运行**

```powershell
# offscreen 冒烟（CI 等价）
$env:QT_QPA_PLATFORM="offscreen"; ./build/local-test/src/app/MangaTranslatorApp.exe --smoke-test; echo $LASTEXITCODE
# 真实窗口（人工/编排者执行，windows QPA）
./build/local-test/src/app/MangaTranslatorApp.exe
```

人工检查清单（记录结果，偏差写入 P5-D 待办）：

- 窗口以 Fluent 边框启动，标题栏含返回按钮（初始隐藏）与标题。
- 首页显示欢迎标题、打开项目按钮、空态或最近项目列表。
- 打开项目成功路径：Toast 出现、最近列表插入新条目。
- 打开不存在路径：InfoBar 错误条出现，带重试动作。
- 关闭再启动：窗口几何恢复；最近项目仍在。
- Light/Dark：通过设置键 `ui/theme` = `light`/`dark` 各启动一次，确认外壳与首页配色跟随。
- 窄宽度（<720px）：外壳不破版（P5-A 仅 Home，无三栏布局，记录现状即可）。

- [ ] **Step 4: prek 全量检查**

```powershell
prek run --all-files
```

- [ ] **Step 5: Commit**（仅当有修复）

```bash
git commit -m "fix(ui): resolve issues found in P5-A full verification"
```

---

## 验收对照与遗留偏差

P5-A 完成后对照 [`phase-5-ui-editor.md`](phase-5-ui-editor.md) 的达成情况：

- MVVM 与 UDF：`BaseViewModel`/`updateState`/`stateChanged`/`observe`、一次性事件走 Signal、State 值类型——已交付。
- 应用外壳：Fluent 窗口 + 单 NavigationView chrome + 多 Surface 宿主 + 返回历史（渲染成功才提交）+ 几何持久化——已交付。
- Home 项目中心：最近项目、打开项目、空态、InfoBar/Toast 反馈——已交付。
- 导航只注册已实现目的地（home）——符合 spec「导航只展示 V0.1 已实现且可完成的目的地」。

显式遗留（已在 P5 里程碑划分中声明，不属本计划缺口）：

- `editor`/`import`/`settings` 路由与页面 → P5-B/P5-D。
- Home 卡片封面缩略图（需 `LoadOrGenerateThumbnailUseCase` 管线）→ P5-B。
- `projectOpened` 目前只携带路径与名称；编辑器会话接管 `OpenProjectResult` 的重构 → P5-B。
- 打开结果中 `warnings`/`recoverySource` 的错误恢复矩阵、`AppStatusTaskStrip`、Settings Surface → P5-D。
- Back/Forward 过渡动画差异化、导航项视觉打磨 → P5-D。
- Home 卡片右键缺「在文件夹中显示」、缺「查看全部」入口、打开中卡片的 ProgressRing/正在打开 态（home.md §2.2/§4.1）→ P5-D。
- Light/Dark/窄宽度的正式视觉验收证据（截图）→ P5-D 视觉验收阶段补齐。
