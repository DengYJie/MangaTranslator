# P5：UI、MVVM 与漫画画布

> 状态：Planned
> 前置阶段：P2
> 可与 P3、P4 并行
> 输出：可完成人工 Region 编辑、文本校对和基础排版的桌面工作台

## 必读规范

实现前依次阅读：

1. [`src/ui/AGENTS.md`](../../../src/ui/AGENTS.md)
2. [`src/ui/manga/AGENTS.md`](../../../src/ui/manga/AGENTS.md)
3. [`docs/ui/README.md`](../../ui/README.md)
4. UI README 指定的 foundations、control reference、相关页面和控件文档

## TODO

### 前置任务

- [ ] 先完成本文件末尾列出的开工前契约补丁与歧义裁决。

### MVVM 与 UDF

- [ ] 实现/复用 `BaseViewModel`、Per-Screen State、`updateState`、`stateChanged` 和 `observe`。
- [ ] View 只渲染 State 并报告用户意图；UseCase 结果由 ViewModel 合并。
- [ ] 一次性导航、文件选择、Toast/InfoBar 使用事件 Signal，不塞入持久 State。
- [ ] State 保存加载、空、成功、失败、取消、Dirty 和选择状态。
- [ ] 禁止新增 Redux Store、Reducer 或全局可变 UI State。

### 应用与页面

- [ ] 实现 NavigationWindow、项目中心、新建项目/导入、编辑器和设置 Surface。
- [ ] 同一个导航控件根据 Surface 显示不同目的地；进入设置时不重复显示设置入口。
- [ ] 实现编辑器页面列表、命令区、漫画画布、Inspector 和状态反馈。
- [ ] 页面列表按需请求异步生成的 PNG 缩略图；最长边 256 px、保持宽高比，生成或写盘失败时回退到占位状态且不影响图片导入。
- [ ] 挂载全局 `AppStatusTaskStrip` 处理跨页面后台进度；进入设置或切导航时保留原 ViewModel 视图草稿。
- [ ] 使用 FluentQt 公共 C++ API 和现有语义 Token。
- [ ] 不添加自定义 QProxyStyle、全局 QSS、`ui/style` 或 themes 目录。

### QGraphicsView 漫画画布

- [ ] 源图像像素坐标作为 Scene 权威坐标，不把 View/设备坐标持久化。
- [ ] 实现图片加载、适应窗口、平移和缩放中心行为。明确全局统一视口，切页视角不跳跃，V0.1 无页面重排。
- [ ] 缩放范围严格限制在 5%-1600%；处理最小尺寸、页面边界、缩放无关命中范围和选择手柄。
- [ ] 实现矩形 Region 创建、选择、移动、八方向缩放和删除。
- [ ] 补充 `RegionOverlay` 的纯键盘无障碍操作（Tab遍历、方向键移动、Shift+方向键缩放）。
- [ ] `QGraphicsItem` 只投影 State，不保存权威 Region 数据。
- [ ] 选择使用稳定 Region ID，不使用列表下标作为身份。

### 编辑与 Undo/Redo

- [ ] Inspector 编辑原文、译文、字体、字号、方向、对齐、行距和边距。
- [ ] 实现原图、清除、译文和双视图同步只读对比预览（V0.1 禁止对比模式下的单画布擦拭与工具操作）。
- [ ] Region 创建/删除/几何、文字、排版修改使用命令对象。
- [ ] 连续拖动合并为一个命令；连续文本输入按目标和时间窗口合并。
- [ ] 基于 `QUndoStack` 接线 Dirty 状态与 `Ctrl+S` 保存链路；关闭项目前进行未保存拦截确认。
- [ ] 明确分离“撤销/重做”与“项目崩溃恢复(Recover)”，后者在主页打开遇到残留 `.bak` 时弹窗触发。
- [ ] OCR/翻译结果只在用户采用时进入 Undo 栈。
- [ ] 提供 OCR、翻译、清除、排版、导出对话框（单页/全本参数）、取消和恢复入口。

### 响应与无障碍

- [ ] 覆盖 Light/Dark、5档宽度(含极窄<720px)、2档高度(含极限扁平<560px)、高 DPI、高对比度模式和关闭动效支持。
- [ ] 定义加载、空、错误、取消、字体缺失、溢出和模型不可用状态。
- [ ] 完成键盘焦点顺序、快捷键、Tooltip、可访问名称和 IME 行为。

## 测试与视觉验收

- [ ] `observe` 初始回放、相等 State 抑制和预期状态序列。
- [ ] 自动化测试使用 `QT_QPA_PLATFORM=offscreen` 验证 ViewModel 状态轮转，摒弃依赖物理坐标的测试。
- [ ] 性能门控：500页项目、单页300 Region负载下，平移缩放 >30fps，切页响应 <300ms。
- [ ] 页面切换、View 销毁和 generation 变化后的过期结果。
- [ ] 缩略图迟到结果不绑定错误页面，失败时页面列表保持可用。
- [ ] Scene/View/源图坐标往返与不同缩放级别命中。
- [ ] Region 命令、文本合并和完整 Undo/Redo。
- [ ] Light/Dark、正常/窄宽度和关键空/错误状态视觉检查。
- [ ] 键盘、IME、Esc 取消和屏幕阅读器名称检查。

## 退出条件

- 用户不依赖自动检测即可创建并完整编辑矩形 Region。
- View 不直接访问 Repository、网络客户端、ONNX Session 或 Credential Manager。
- 窄窗口及扁平窗口下页面、画布、Inspector 和主要操作仍可访问。

## 注意事项

- 文本 UI 规范优先于草稿图片。
- 不在 Widget 中复制 Domain 校验或 UseCase 编排。
- 不为视觉常量恢复全局 Metrics/Theme 层，**P5 严禁新建私有数值体系**，线宽、动效时间一切参照规格文档或底层 API。

---

## 设计规格参考

### 开工前契约补丁

- **撤销栈双层架构**：项目级 `QUndoStack` 归属 `EditorViewModel`。纯文本由原生栈处理，`FocusOut` 时提交 `EditTextCommand` 到全局栈。跨页 Undo 自动触发视图切页。
- **应用级持久化**：新增 `application/ports/IApplicationSettingsStore` 与 `IRecentProjectStore`，用 `QSettings` 存入系统 AppData，与项目数据物理隔离。
- **异步生命周期**：`EditorViewModel` 持有单页与全局 `OperationContext`。切页取消单页任务，关项目取消全量。后台回传节流遵循 P4 契约的进度发布频率（最多每 50ms 一次），不得另立 100ms 等私有数值。
- **缩略图归属**：由 `ThumbnailWorker` 懒生成至 `assets/derived/<uuid>/thumb_256.jpg`，**不写 `project.json`**。首图供首页封面借用。
- **Dirty 与保存接线**：对比 `undoStack->index()` 与 `m_savedUndoIndex` 判定 Dirty。`Ctrl+S` 调用 `project->markCleanIfRevision` 并对齐状态。
- **错误映射规范**：Critical 弹 `ContentDialog`（如 `persistence.locked` 强制只读）；Warning 弹 `InfoBar`；Transient 弹 `Toast`。禁止 Diagnostic 上 UI，同一错误拦截并发轰炸。
- **P3 导入空项目漏洞补丁 (应用层实施)**：因 P3 计划已封板，其遗留的“导入取消产生空项目”漏洞在应用层调度时修复：`ImportImagesUseCase` 必须在临时系统沙箱 (`.temp_import/`) 执行文件流复制与校验，仅全部就绪且落盘 `project.json` 成功后才重命名/落地，取消或零有效图时静默清除沙箱。
- **外壳导航规则补丁**：主导航强制保留 4 项（首页/编辑器/批量/术语库）。V0.1 未实现的批量和术语库点击呈现“🚧 建设中”的规范空白占位页。

### FluentQt 控件选型与实测映射

基于 `FluentQt::FluentQt` 实际代码库（`build/local-debug/_deps/fluentqt-src`），Phase 5 编辑器各区域的控件映射与命名空间如下：
- **顶层外壳与全局导航**：直接复用 ForgeAI 项目已有的 `NavigationWindow`。它继承自 `fluent::windowing::Window` (支持 Mica/Acrylic)，并在内部正确编排了 `fluent::navigation::NavigationView` 作为外壳容器：
  - **组件化面板**：将自定义的 `NavigationPanel` 设为 `NavigationView` 的 `mainChromeWidget`，承载汉堡按钮、主导航树与 Footer，并由 `NavigationWindow` 自动管理响应式折叠断点 (`DisplayMode`) 与侧滑展开状态。
  - **路由控制中心**：集成 `NavigationController` 管理栈式导航 (`NavigationHistory`)，保障渲染事务原子性。
  - **模块化注册**：实现 `INavigationRegistrar`，允许各特性模块通过 `NavigationItemDescriptor` (包含层级路由、图标与动画等) 解耦注入导航树与多 Surface 面板。
- **顶部命令条**：`fluent::menus_toolbars::CommandBar`，浮动预览切换使用 `fluent::navigation::SelectorBar`。
- **侧栏列表**：`fluent::collections::ListView`。加载骨架屏在 Delegate 中使用 `fluent::status_info::ShimmerPainter` 绘制（严禁为每行实例化 QWidget）。
- **属性检查器 (Inspector)**：`fluent::layout::Card` / `Expander` / `Field`。输入类使用 `TextEdit`、`NumberBox`、`ComboBox`、`ColorPicker`、`ToggleSwitch`。
- **反馈对话框**：`InfoBar` (行内警告)、`Toast` (轻量通知)、`ContentDialog` (模态弹窗)。

### 应用程序与主题初始化流

在 `src/app/main.cpp` 中严格顺序：
1. `fluent::prepareHighDpiApplication();` (在 QApplication 前配置 High-DPI 行为)
2. `fluent::initializeResources();`
3. 设置全局字体 `app.setFont(Typography::fontStyle(Typography::FontRole::Body).toQFont());`
4. 创建 `Window` 并 `show()`。

### EditorState 与 MVVM

- **State 结构**：维护单向数据流纯数据的值类型快照 `EditorState`（包含 `CanvasTool`、`PreviewMode`、Zoom、选中的 `RegionId` 集合等）。
- **防腐 Generation**：切页时 `stateGeneration++`。异步结果回传 GUI 时必须核对 `requestGeneration == currentState.stateGeneration`。
- **对比模式 (Side-by-Side) 行为**：双视图同步滚动且只读。禁用单画布擦拭与任何工具操作。

### QGraphicsScene 漫画画布规范

- **Z-Order 体系**：Background(0) -> PageImage(10) -> CleaningLayer(20) -> TextLayer(30) -> RegionBorder(100) -> SelectionHandles(110) -> CreationOverlay(200)。
- **手柄与零拷贝绘制**：高频热路径（如 `SelectionHandleItem::paint`）必须使用零拷贝引用 `const auto& colors = themeColorsRef();`，严禁调用按值返回巨大结构体的 `themeColors()`。手柄尺寸基于 `levelOfDetailFromTransform` 反向缩放以保持固定屏幕像素大小。
- **自定义视口控件**：继承 `fluent::FluentElement` 混入类并覆盖无参的 `onThemeUpdated()`。局部冻结主题通过设置 `fluentThemeOverride` 动态属性。

### Undo/Redo 合并策略

- **新建/缩放/排版样式**：操作释放时提交单条命令。
- **删除选区**：支持批量操作，单次提交。
- **移动选区**：连续拖动在 `mouseRelease` 处生成单条命令（或在 300ms 窗口内合并）。
- **文本输入**：针对同一 Region 同一字段，失去焦点或停顿时合并为单条最终的 `EditTextCommand`。
- **AI 结果采用**：产生独立的撤销原子命令。
