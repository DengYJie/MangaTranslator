# P5 补充：编辑器架构与交互契约

> 状态：Design baseline
> 适用范围：P5 UI、MVVM、漫画画布，以及为其服务的 P1–P4 窄契约修订
> 权威关系：本文件补充 `phase-5-ui-editor.md`；Domain、Application 和处理算法冲突时，以所属阶段已同步的底层契约为准

## 1. 产品立场

### Design Intent

让用户始终围绕“当前漫画页”工作，在一个连续工作台内完成区域定位、文字校对、翻译采用和排版检查；系统能力退居辅助层，不把编辑器变成任务面板集合。

- **核心对象**：漫画页及其 Region，而不是聊天、作业队列或文件树。
- **时间模型**：当前项目 revision 与可撤销命令历史；后台 operation 是附属于页面/Region 的暂态工作。
- **Hero Surface**：居中的漫画画布。页面列表、命令区和 Inspector 都服务于画布上的直接操作。
- **密度**：工具区紧凑，Inspector 中等密度，画布留白最大化。
- **品牌锚点**：页面纸张、选区描边、处理中微光和清晰的原图/译文对比；避免通用仪表盘观感。

ForgeAI 可作为 Navigation、MVVM 和响应式布局的结构参考，但 MangaTranslator 必须自行通过 FluentQt 公共 API实现，不建立源码、头文件或二进制依赖，也不继承其产品命名与视觉身份。

### 开工前视觉选择门

进入生产 Widget 实现前，必须用相同真实数据和相同窗口尺寸制作三套高保真概念：

1. 画布优先，Inspector 为稳定右栏。
2. 画布优先，Inspector 为上下文浮层/抽屉。
3. 校对优先，文本与画布形成主次双栏。

三套概念都需包含 Light/Dark、单选、多选、加载、错误和窄窗口状态。评审记录每套方案保留与拒绝理由，由产品负责人选定一个方向后才能落生产 UI；不得在实现中拼接三套方案。

### 视觉选择结论（2026-08-30，产品负责人）

评审基于 `docs/ui/drafts/` 现有草稿与结构对比完成，未另行制作高保真概念图。选定方案一“画布优先，Inspector 为稳定右栏”：

- 方案一（保留）：直接复用既定三栏、响应式抽屉和单一 `EditorViewModel`；Inspector 与画布 Region 选择的同步关系常驻可见；状态分支最少。
- 方案二（拒绝）：上下文浮层/抽屉 Inspector 在正常宽度引入额外显隐状态，与窄宽抽屉模式职责重叠；Inspector 与 Region 选择的同步可见性不稳定。
- 方案三（拒绝）：校对优先双栏把文本列表抬升为主对象，偏离 §1.1 的 Hero Surface 定位，把编辑器推向任务面板集合。

实现不得拼接三套方案；最终实现与方案一的偏差在 P5 视觉验收证据中说明。

## 2. 依赖与所有权

```text
ui ───────────────→ application ───────────────→ domain
 │                       ↑
 └────→ rendering ───────┘
          ↑
services ─┘

data/network ─────→ application ports + domain
app ──────────────→ all layers, construction only
```

### `manga_rendering` target

`manga_rendering` 是唯一新增的共享功能层，负责确定性的文本测量和栅格化：

- 只依赖 `manga_domain`、`Qt6::Core` 和 `Qt6::Gui`。
- 不依赖 Widgets、FluentQt、Application Port、文件系统、网络或 composition root。
- 提供 `TextLayoutEngine`、`TextRasterizer` 及不可变结果类型。
- `services::QtTextLayouter` 使用它产生 `LayoutResponse`。
- UI 使用它预计算活动页 Region 的渲染缓存。
- UI 的图层快照生产使用它生成 translation/manualOverride layer；Exporter 只合成调用方提供的不可变图层。

UI 不依赖 `services`。`QGraphicsItem::paint()` 不调用 Layout Port，不运行换行算法，不读取 Domain，也不触发整页异步重渲染。

## 3. TextLayout 前置契约

P5 暴露完整排版控件前，P1 必须把 `TextLayout` 扩展为：

```cpp
struct TextLayout {
    std::string fontFamily;
    int fontWeight;               // 100..900，步长 100
    double letterSpacingEm;       // -0.5..2.0
    double fontSizePt;
    double minFontSizePt;
    double maxFontSizePt;
    double lineSpacing;
    bool autoFit;
    bool wordWrap;
    TextOrientation orientation;
    TextAlignment alignment;
    Margins margins;
    std::string textColorHex;     // #RRGGBB
    std::string strokeColorHex;   // #RRGGBB
    double strokeWidthPx;         // 0..32，源图像像素
    double rotationDeg;           // V0.1 固定为 0
};
```

项目格式在 V0.1 继续使用 `schemaVersion = 1`。上述字段已经属于冻结的 V1 Schema，加载时必须完整存在并通过严格校验；不得再为同一 V0.1 格式虚构 schema 2 迁移。

渲染缓存 key 至少包含：RegionId、文本来源、`contentRevision`、完整 `TextLayout`、Region 源像素 bounds、目标 DPI 契约和 renderer version。任一字段改变只失效对应 Region；字体数据库或 renderer version 改变失效活动页全部文字缓存。

## 4. 状态与异步生命周期

### EditorState

```cpp
struct EditorState {
    domain::ProjectId projectId;
    domain::PageId currentPageId;
    double zoomFactor{1.0};
    CanvasTool activeTool{CanvasTool::Select};
    PreviewMode previewMode{PreviewMode::Translated};
    QSet<domain::RegionId> selectedRegionIds;
    std::optional<domain::RegionId> primaryRegionId;
    quint64 viewGeneration{0};
    quint64 projectSessionGeneration{0};
    bool isPageLoading{false};
    bool operator==(const EditorState&) const = default;
};
```

State 是值类型投影，不保存 `ComicPage*`、`TextRegion*`、列表下标、Widget、Scene Item、`QImage` 或 `QPixmap`。图像和渲染缓存属于 View/专用 cache owner，并用稳定 ID 关联。

### 两种 generation

| 范围 | 典型任务 | 失效时机 | 迟到结果 |
|---|---|---|---|
| `viewGeneration` | 当前页原图加载、当前预览合成、可视缩略图绑定 | 切页、重建 View、关闭项目 | 静默丢弃，不更新当前画布 |
| `projectSessionGeneration` | OCR、翻译、清除、排版、导出、后台缩略图生成 | 切换/关闭项目、应用退出 | 仍按原目标采用；会话不一致则丢弃 |

所有后台处理还携带 `operationId + projectId + pageId + regionId/contentRevision`。页面切换不是后台业务结果的失效条件；同一目标被新操作替代时取消旧 operationId。`AppStatusTaskStrip` 按 operationId 展示进度，点击任务可导航到其 PageId，但任务不得因导航状态变化而改绑。

### GUI 线程边界

- ViewModel 在 GUI 线程创建并持有 `QFutureWatcher`。
- Watcher 回调先核对生命周期、generation 和稳定目标身份，再更新 State 或构造命令。
- 工作线程可以创建和传递隐式共享 `QImage`；进入工作线程后不得修改仍被 GUI 使用的同一实例。
- `QPixmap`、Widget、Scene、ViewModel、UndoStack 和 FluentQt 对象只在 GUI 线程创建、访问和销毁。

## 5. 画布、图层与主题

### Z-Order

```cpp
namespace manga::ui::canvas {
constexpr qreal kZBackground = 0.0;
constexpr qreal kZPageOriginal = 10.0;
constexpr qreal kZCleaningLayer = 20.0;
constexpr qreal kZTextLayer = 30.0;
constexpr qreal kZRegionNormal = 100.0;
constexpr qreal kZSelectionHandles = 110.0;
constexpr qreal kZToolCreation = 200.0;
}
```

V0.1 的对比模式使用两个只读 `QGraphicsView`，共享同一 scene-coordinate viewport state，原图在左、处理结果在右。它同步滚动、缩放和当前页面，但不允许选择、创建、拖拽或 Inspector 编辑。Split Curtain 不在 V0.1 范围内，因此不预留 `kZSplitCurtain`，也不让底图 `paint()` 感知裁切位置。

### Region 与手柄

- `RegionOverlayItem` 的 rect 使用源图像像素，pen 使用 cosmetic width 保持屏幕线宽稳定。
- 八个 `SelectionHandleItem` 是 Region 的子 Item，只给手柄设置 `ItemIgnoresTransformations`；父 Region 不设置该 flag，否则几何和图像坐标会失真。
- 手柄视觉尺寸为 8 device px，命中范围至少 16 device px。`boundingRect()`/`shape()` 必须覆盖命中范围，不能只在 `paint()` 中反算 LOD。
- MousePress 保存起始几何，MouseMove 只更新 View 临时投影，MouseRelease 才提交 Domain 命令；Esc 恢复起始几何。
- Tab 在 Region 间移动焦点；方向键移动 1 source px，Shift+方向键调整主手柄 1 source px。未来若加入网格，步长必须进入设置而非硬编码第二套行为。

### FluentQt 主题边界

`MangaCanvas` 可以继承 `QGraphicsView` 与 `fluent::FluentElement`，在 `onThemeUpdated()` 中刷新背景和缓存。普通 `QGraphicsItem` 不依赖 QWidget 子树主题传播，不为每个 Item 混入 `FluentElement`。

Canvas 在一帧开始时解析只读主题调色板，并把小型不可变 palette view 提供给 overlay items。高频 `paint()` 路径使用 `themeColorsRef()` 或该 palette view，不调用按值复制整套颜色的 `themeColors()`。局部 `fluentThemeOverride` 只用于确有独立主题需求的 QWidget 子树；Inspector 默认跟随应用主题，不强制深色。

## 6. 渲染与缓存

### 活动页渲染

```text
Domain snapshot
→ manga_rendering measure/rasterize outside paint()
→ immutable QImage region result
→ GUI thread converts to QPixmap
→ cache by RenderKey
→ TextLayerItem::paint draws cached pixmap only
```

- Region 文字缓存预算 128 MiB，只覆盖当前活动页；按像素字节成本做 LRU。
- 修改一个 Region 只使该 Region 的 translation/manual cache 失效。改变整页 DPI/renderer version 才全页失效。
- 清除层和原图各只保留当前页的展示 QPixmap；切页交叉淡入结束后释放旧页。
- 低清预览可立即显示，完整缓存完成后原位替换；替换不得改变 Scene rect 或缩放中心。
- `paint()` 禁止磁盘 I/O、字体数据库扫描、Domain 查询、布局、整页合成、锁等待或同步 Future。

### 缩略图

```text
Delegate 请求 PageId
→ ViewModel 去重并限制并发
→ LoadOrGenerateThumbnailUseCase
→ IThumbnailStore 读取 thumbnail/v1.png
→ 缺失/损坏时 IPageImageLoader 解码并等比缩到最长边 256 px
→ IThumbnailStore 原子提交 PNG
→ Future 返回 QImage
→ GUI 线程按 PageId + viewGeneration 转 QPixmap 并刷新行
```

- 最多 4 个缩略图请求同时在途；请求范围为当前可视行加上下各 2 行，快速滚动时取消或放弃已远离窗口的低优先级绑定。
- `QCache` 使用 64 MiB 预算，cost 为估算像素字节数向上取 KiB；不能用固定 50 张替代内存约束。
- Model 只保存 PageId、纵横比、加载状态和错误摘要，不保存每页 QWidget。
- Delegate 使用 `fluent::status_info::ShimmerPainter` 绘制骨架；不得 `setIndexWidget()`。失败显示警告占位和重试动作，不阻塞列表滚动。
- 缩略图不是项目数据，不进入 project.json、不推动 revision/Dirty；首页封面可复用第一页同一固定 PNG。

## 7. Undo、Dirty 与不可变派生资产

### 单一命令历史

每个打开项目只有一个 `QUndoStack`，归 `EditorViewModel`。命令保存 `ProjectId`、`PageId`、目标 RegionId 和 before/after 值，不保存 Domain 指针或 Scene Item。跨页 Undo/Redo 先让 Application 执行命令，再导航到命令 PageId 并刷新 State；页面加载失败不回滚已成功的 Domain 命令。

#### 为什么文本编辑也使用项目命令栈

文本控件的显示值是 Domain State 的投影，不是第二份权威数据。如果同时启用 `QTextDocument`/输入控件自身的 Undo 与项目 `QUndoStack`，一次 `Ctrl+Z` 可能只把控件从 `ABC` 改回 `AB`，而 Domain、Dirty、画布排版预览和导出快照仍保留 `ABC`。下一次 State 刷新或切换 Region 时，控件又会跳回 `ABC`；反向同步两套历史还会产生重复命令、顺序错位和无法定义的保存点。

因此 V0.1 采用单一权威历史：

```text
普通输入或 IME commit
→ ViewModel 生成/合并 EditTextCommand
→ 项目 QUndoStack
→ Application/Domain
→ 新 State 投影回文本控件与画布预览
```

- 文本控件关闭自身独立 Undo/Redo，只保留选择、光标、preedit 和未提交输入的编辑能力。
- `Ctrl+Z/Ctrl+Y` 始终操作项目栈，使文本、Region 几何、候选采用和排版操作保持同一时间顺序，并支持跨页撤销。
- 执行 Undo/Redo、切换目标或保存前，ViewModel 先结束已完成的文本合并批次；尚未 commit 的 IME preedit 不写入 Domain，也不进入保存快照。
- State 回放文本后恢复可恢复的光标/选择位置；不能通过阻断 State 更新来保留控件内部旧值。

如果未来引入需要复杂局部历史的富文本编辑器，应把整个编辑会话建模为显式事务，在提交时生成一个项目命令；不得在没有事务边界的情况下并行维护两套 Undo 历史。

命令合并规则：

- Drag/resize：一次 press-drag-release 一条命令，不按像素写栈。
- Text：同 Project、Page、Region、字段、编辑会话且单调时钟间隔不超过 500ms 才能 `mergeWith()`。
- FocusOut、选择变化、页面切换、显式 Apply 或 Undo/Redo 封口。
- IME preedit 只更新编辑控件，不写 Domain、不产生命令；commit string 才进入合并窗口。
- 批量样式修改是一条宏命令，before/after 按 RegionId 保存；任一目标校验失败则整批不提交。

### Clear 两阶段命令

```text
ClearResponse
→ PrepareClearResultUseCase: validate + compose + atomic file commit
→ PreparedClearChange { beforeRef, afterRef, beforeState, afterState }
→ GUI thread push ClearResultCommand
→ first redo: ApplyPreparedClearChangeUseCase(after)
→ undo: ApplyPreparedClearChangeUseCase(before)
→ redo: ApplyPreparedClearChangeUseCase(after)
```

Command 的所有 redo/undo 都是同步 Domain 操作，不读写文件。命令仍在 UndoStack 时 before/after 不可变资产必须保留；V0.1 项目打开期间不做其垃圾回收。

`QUndoCommand::redo()` 没有失败返回通道，因此 ViewModel 必须在同一个 GUI 事件内完成最终 snapshot 校验、构造和 `push()`；Domain 只在 GUI 线程由命令串行修改。后续 redo/undo 的前置条件由栈顺序恢复，失败视为内部不变量破坏并停止继续重放，不能弹成可重试业务错误或让栈游标与 Domain 静默分叉。关闭项目先销毁该项目 UndoStack，再释放会话。

### Dirty 与保存

- `ComicProject::revision/isDirty()` 是“磁盘是否包含当前用户数据”的唯一权威。
- `QUndoStack::isClean()` 只表示命令游标是否回到最近成功保存点，用于菜单和辅助提示。
- 保存请求捕获 revision。Repository 成功返回 `savedRevision` 后，只有 `markCleanIfRevision(savedRevision)` 成功才调用 `undoStack.setClean()`。
- `Ctrl+S` 先提交当前控件已完成的 IME composition 和文本编辑会话，再捕获 revision；仍处于 preedit 的未确认组合文本不进入保存快照。
- 保存期间发生新编辑时 Domain 继续 Dirty，UndoStack 不设置 clean point。
- Undo 回到旧命令位置但 Domain revision 未与已保存快照等价时，关闭拦截仍以 Domain Dirty 为准。
- 保存、自动保存和崩溃恢复都不创建 Undo 命令，也不清空历史。

## 8. Inspector

Inspector 完全由 `selectedRegionIds` 驱动：

1. **Empty**：页码、分辨率、阅读方向、页面处理入口、快捷键提示。
2. **Single**：原文、人工译文、AI candidates、清除状态和完整 `TextLayout`。Candidates 放入 `fluent::layout::Expander`，采用候选产生一条命令。
3. **Multi**：显示选中数量和属性交集；仅允许批量字体、字重、字号、对齐、颜色、清除和删除。值不一致显示 mixed，不擅自选取第一个 Region 的值。

用户编辑的译文进入 `TranslationEntry::manualText`。批量 OCR/翻译采用遇到 manual override 默认跳过并汇总“已保留 N 项人工修改”；只有显式确认替换才能覆盖。

## 9. 凭据与设置

设置页只使用三条 Application 用例（同步返回 `Result`）：

- `CheckCredentialStatusUseCase(providerId) -> CredentialState{Configured, Missing, Unavailable}`
- `SetCredentialUseCase(providerId, SecretBytes)`
- `ClearCredentialUseCase(providerId)`

状态查询落到 `ICredentialStore::hasApiKey()`，不得 read-then-discard。输入控件不提供回显现有 key；已配置时只显示状态和“替换/删除”。提交成功后立即清空编辑缓冲。Provider endpoint、model name 等非 secret 设置通过 `IAppSettingsStore` 保存；API Key 永不进入 QSettings。

## 10. RecoveryAction 到 UI

| RecoveryAction | UI 入口 | 行为 |
|---|---|---|
| `Retry` | 行内“重试”或任务条按钮 | 使用同一业务目标创建新 operationId；不复用失败 Future |
| `OpenSettings` | InfoBar/空状态“打开设置” | 导航到对应 Provider/性能设置并聚焦字段 |
| `Reauthenticate` | “配置 API Key” | 打开凭据编辑，不展示旧 key |
| `LocateModel` | “选择模型目录” | 打开受限目录选择并重新校验 manifest |
| `ChoosePath` | “选择其他位置” | 重开文件/目录选择器，保留其余表单值 |
| `FreeDiskSpace` | “查看位置”与说明 | 展示安全目录名和所需空间，不显示完整敏感路径 |
| `None` | 无动作 | 只显示安全说明或静默记录 |

`safeMessage` 可展示；`diagnostic` 永不上屏。相同 code + target 在短时间内合并为一条 InfoBar/Toast，计数更新而不是并发轰炸。是否使用 ContentDialog 取决于是否必须阻断当前用户决策，不以 ErrorCategory 机械决定。

## 11. FluentQt 控件映射

- 外壳：基于 `fluent::windowing::Window` 与 `fluent::navigation::NavigationView` 建立项目本地 Shell。
- 命令区：`fluent::menus_toolbars::CommandBar`；预览模式可用 `fluent::navigation::SelectorBar`。
- 页面列表：Qt Model/View，Delegate 内使用 `fluent::status_info::ShimmerPainter`。
- Inspector：`fluent::layout::Card`、`Expander`、`Field`，结合 TextEdit、NumberBox、ComboBox、ColorPicker 和 ToggleSwitch。
- 反馈：InfoBar 用于可持续的页面内问题，Toast 用于已完成的轻量反馈，ContentDialog 只用于需要立即选择的阻塞决策。

应用初始化遵循 FluentQt 公开 API 的先后约束：在 `QApplication` 构造前准备 High DPI，随后初始化 FluentQt resources，再创建 Window。具体符号以锁定 revision 的公共头文件以及实际组件的编译和交互测试为准，不设置独立探针，也不从示例项目复制私有实现。

## 12. 响应式与可访问性

- 正常宽度：页面栏 + 画布 + Inspector 三栏。
- 中等宽度：页面栏收窄为缩略图，Inspector 保持。
- 窄于 720 px：页面栏和 Inspector 变为互斥抽屉，画布占主区域；主要保存、撤销、工具切换仍在命令区。
- 低于 560 px 高度：压缩命令区和状态条，Inspector 内容滚动，不能裁掉应用/取消按钮。
- Reduce Motion 下禁用非必要交叉淡入和 Shimmer 动画，保留静态加载占位。
- 所有颜色状态同时有形状、图标或文字；Region 选择不只依赖颜色。焦点描边在 Light/Dark、原图亮区和暗区都可辨识。

## 13. 性能与验收证据

可重复的结构门槛：

- 500 页项目初始化不解码所有原图或缩略图。
- 300 Region 页面中，平移/缩放期间不触发 layout、Domain 查询、磁盘读取或 Widget 创建。
- 缩略图最大并发 4，缓存 64 MiB；活动页文字缓存 128 MiB。
- Model/View 不用 `setIndexWidget()`，Scene 不为文字逐字符创建 Item。
- 所有异步回调都有 PageId/RegionId、operationId 和正确 generation 的测试。

固定测试机的发布门槛为 300 Region 平移/缩放 >30 fps、热缓存切页 <300ms；普通 CI 只验证没有同步阻塞和预算不变量，避免跨机器 wall-clock 波动。

视觉验收至少保留以下证据：

- Light/Dark × 正常/窄/扁平窗口。
- Empty/Loading/Single/Multi/Error/Overflow/ModelMissing。
- 100%、200% 高 DPI 与 Reduce Motion。
- §1.2 的概念对比与选择结论、最终实现截图和偏差说明。

## 14. 明确不在 P5

- Split Curtain、单画布擦拭对比。
- 全局术语库和批处理独立页面。
- 自定义文本编辑器、私有主题框架或 Redux 状态库。
- UndoStack 引用资产的在线垃圾回收。
- 为性能而复制排版算法或绕过 Application UseCase。
