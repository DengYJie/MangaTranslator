# P5：UI、MVVM 与漫画画布

> 状态：Planned
> 前置阶段：P1、P2 契约修订，P3 图片加载与派生资产路径规则，P4 共享渲染核心与处理 Port
> 可与 P4 Adapter 实现并行，但不得绕过未落地的前置契约
> 输出：可完成人工 Region 编辑、文本校对、基础排版和处理结果采用的桌面工作台

## 必读规范

实现前依次阅读：

1. [`src/ui/AGENTS.md`](../../../src/ui/AGENTS.md)
2. [`src/ui/manga/AGENTS.md`](../../../src/ui/manga/AGENTS.md)
3. [`docs/ui/README.md`](../../ui/README.md)
4. UI README 指定的 foundations、control reference、相关页面和控件文档
5. [`phase-5-ui-editor-supplement.md`](phase-5-ui-editor-supplement.md)

补充规格负责冻结依赖边界、状态生命周期、渲染缓存、Undo/Dirty、缩略图、凭据与错误恢复；本文件只维护实施清单和验收门槛。

## TODO

### 开工前契约

- [x] 完成 P1 `TextLayout` 样式字段在 V1 Schema 的序列化与校验，更新 Domain/Schema 测试。
- [x] 完成 P2 分范围 generation、`IThumbnailStore`、凭据状态 UseCase、应用设置 Port 和 Clear 两阶段采用协议。
- [x] 作为 P5 跨层前置补丁，在 Data 层实现 `IThumbnailStore` 和应用设置/最近项目 Port Adapter；UI 不直接承担文件或 QSettings 读写。
- [x] 建立 `manga_rendering` target，由 UI 图层快照生产和排版 Adapter 共享测量/栅格化实现；Data Exporter 只合成传入的不可变图层，不依赖 Rendering。

### 产品与视觉设计门

- [x] 输出一句话 Design Intent、核心对象、时间模型、Hero Surface、密度和响应式策略。
- [x] 三套可比较编辑器概念的评审与产品负责人选择：2026-08-30 基于 `docs/ui/drafts/` 现有草稿和结构对比选定“画布优先 + Inspector 稳定右栏”，未另行制作高保真概念图；保留/拒绝理由见补充规格 §1.2。
- [x] 在 UI foundations 和画布规格中固定 Light/Dark 语义色、文字层级、间距、圆角、动效和覆盖层规则；生产控件不得散落私有视觉常量。
- [x] ForgeAI 仅作为布局与 MVVM 参考，不建立源码或二进制依赖，不复制其产品身份。

### MVVM 与 UDF

- [ ] 实现/复用 `BaseViewModel`、Per-Screen State、`updateState`、`stateChanged` 和 `observe`。
- [ ] View 只渲染 State 并报告用户意图；UseCase 结果由 ViewModel 合并。
- [ ] 一次性导航、文件选择、Toast/InfoBar 使用事件 Signal，不塞入持久 State。
- [ ] State 使用稳定 `PageId`/`RegionId`/`OperationId`，不保存 Domain 指针、列表下标、Widget 或 `QPixmap`。
- [ ] 实现 `viewGeneration` 与 `projectSessionGeneration`；后台处理按 operationId 和目标身份更新，不以“当前页面”作为有效性条件。
- [ ] 禁止新增 Redux Store、Reducer、全局可变 UI State 或 View 直连 Repository/Adapter。

### 应用外壳与页面

- [ ] 实现项目中心、新建/打开/导入、编辑器和设置 Surface；导航只展示 V0.1 已实现且可完成的目的地。
- [ ] 实现编辑器页面列表、命令区、漫画画布、Inspector、状态反馈和全局 `AppStatusTaskStrip`。
- [ ] 进入设置或切换 Surface 时保留编辑器 ViewModel 草稿与项目级后台任务；关闭项目时统一取消并释放。
- [ ] 设置页通过凭据状态/配置/清除 UseCase 展示 Provider 状态，不读取或回显 API Key。
- [ ] 通过 `IAppSettingsStore` 持久化并安全恢复窗口 normal geometry、最大化状态和桌面三栏 Pane 宽度；离屏值必须 Clamp，Overlay 宽度不回写桌面偏好。
- [ ] 使用 FluentQt 公共 C++ API 和既有语义 Token；不添加全局 QSS、自定义 `QProxyStyle` 或第二套主题系统。

### 页面列表与缩略图

- [ ] `QListView + QAbstractListModel + QStyledItemDelegate` 只为可视行和小范围预取请求缩略图，不为每行创建 QWidget。
- [ ] 通过 `LoadOrGenerateThumbnailUseCase` 读取或生成固定 PNG；UI 不拼磁盘路径、不写文件、不修改项目 Dirty。
- [ ] 工作线程只产生 `QImage`，GUI 线程转换 `QPixmap`；缓存按像素字节成本限制，不按“50 张”粗略计数。
- [ ] Delegate 用 `ShimmerPainter` 呈现加载态，成功交叉淡入，失败显示可重试占位；迟到结果按 PageId 和 generation 校验。

### QGraphicsView 漫画画布

- [ ] 源图像像素坐标作为 Scene 权威坐标，不把 View/设备坐标持久化。
- [ ] 实现图片加载、适应窗口、平移和以指针/视口中心为锚的缩放；缩放范围限制在 5%–1600%。
- [ ] 固定创建阈值 8×8 source px、拖拽阈值 4 device px、离散缩放档位和低于 25% 隐藏 24 source px 网格。
- [ ] 实现矩形 Region 创建、稳定 ID 选择、移动、八方向缩放、批量选择和删除。
- [ ] Region item 只投影 State；手柄是独立子 Item 并忽略变换，父 Region 不设置 `ItemIgnoresTransformations`。
- [ ] 原图、清除、译文和人工覆盖遵循固定 Z-Order；排版与图层栅格化在 `paint()` 外完成，热路径只绘制缓存。
- [ ] 提供原图/处理结果双视图同步只读对比；V0.1 不实现单画布 Split Curtain。
- [ ] 补充纯键盘 Region 操作、可访问名称、焦点顺序和缩放无关命中范围。

### Inspector 与编辑

- [ ] 空选显示页面工作流，单选显示文本、候选和完整排版样式，多选只显示可安全批量修改的交集属性。
- [ ] Inspector 支持原文、译文、字体、字重、字距、字号、方向、对齐、行距、边距、文字色、描边色和描边宽度。
- [ ] 候选列表放在 `Expander`；采用候选产生独立 Undo 命令。
- [ ] 人工文本写入 `TranslationEntry::manualText`，后续 OCR/翻译批量采用默认跳过人工覆盖，除非用户显式确认替换。
- [ ] 为加载、空、错误、取消、字体缺失、缺字、溢出和模型不可用提供稳定状态。

### Undo/Redo、Dirty 与保存

- [ ] 每个打开项目只有一个由 `EditorViewModel` 持有的 `QUndoStack`；命令记录 PageId，跨页 Undo 先导航再刷新投影。
- [ ] 拖拽仅在 MouseRelease 提交一条命令；同一 Region/字段/编辑会话的文本在 500ms 内合并，FocusOut 或目标变化封口。
- [ ] 清除结果完成 prepare 后才构造并 push 命令；`redo()`/`undo()` 只切换已存在的 Domain 引用，不做 I/O。
- [ ] `Ctrl+S` 先提交已完成的编辑会话；`ComicProject::isDirty()` 是关闭拦截权威，保存成功且 revision 未变化时调用 `markCleanIfRevision(savedRevision)` 与 `undoStack.setClean()`。
- [ ] OCR、翻译和排版结果只在用户采用时进 Undo；保存不清空命令历史，崩溃恢复不进入 Undo。

### 错误恢复与响应式

- [ ] 按补充规格实现 `RecoveryAction → UI 入口` 映射；Diagnostic 永不上屏，去重并节流同类错误。
- [ ] 覆盖 Light/Dark、5 档宽度（含 <720 px）、2 档高度（含 <560 px）、高 DPI、高对比度和关闭动效。
- [ ] 窄宽下 Inspector 转抽屉，扁平高度下命令区压缩但主任务入口仍可见；画布始终保留主要空间。
- [ ] 完成快捷键、Tooltip、屏幕阅读器名称、IME 组合态、Esc 取消和危险操作确认。
- [ ] V0.1 固定 `Ctrl+Shift+T`，不提供快捷键配置入口；Escape 按 IME/文本编辑、手势、Overlay、导航逐层消费。

### V0.1 范围锁定

- [x] 自动文字检测属于 V0.2。P5 不引入 `ITextDetector` 的空契约，不显示检测命令或检测设置；V0.1 智能翻译从已有手工 Region 的 OCR 开始。
- [x] Preview Selector 固定嵌入 CommandBar；V0.1 不增加画布浮动 Selector 层。
- [x] 每页独立保存 zoom + center；V0.1 不提供“统一适应页面”全局偏好。
- [x] 对比模式固定显示只读 Region 轮廓，不提供显示开关。

## 测试与视觉验收

- [ ] `observe` 初始回放、相等 State 抑制、一次性事件和预期状态序列。
- [ ] 页面切换、View 销毁、项目关闭、同目标替换操作和两种 generation 的迟到结果。
- [ ] 500 页模型保持惰性加载；可视行外不生成 QWidget，缩略图和活动页缓存保持在文档预算内。
- [ ] 单页 300 Region 时平移缩放稳定 >30 fps；交互中的 `paint()` 不触发排版、文件 I/O 或 Domain 查询。
- [ ] 热缓存切页目标 <300 ms；冷加载显示即时占位且 GUI 线程不阻塞。wall-clock 只在固定测试机作为发布门，不用于普通跨机器单元 CI。
- [ ] Scene/View/源图坐标往返、5%/100%/1600% 命中、八方向手柄、键盘移动和批量选择。
- [ ] 文本合并、IME、跨页 Undo、Clear prepare/redo/undo、保存竞争和 Dirty/clean-point。
- [ ] 凭据状态不读取 Secret；每个 `RecoveryAction` 都有可执行 UI 路径。
- [ ] 按 full-profile 视觉证据矩阵检查 Light/Dark、正常/窄/扁平窗口，以及加载、空、错误、溢出和模型缺失状态。

## 退出条件

- 用户不依赖自动检测即可创建并完整编辑矩形 Region、文本与排版样式。
- View 不直接访问 Repository、网络客户端、ONNX Session、文件系统、QSettings 或 Credential Manager。
- UI 预览与导出请求中的文字图层由同一 `manga_rendering` 结果生成，Data Exporter 只合成快照，关键版式和 overflow 判定一致。
- 切页不污染当前视图，也不误取消用户启动的项目级后台任务。
- 窄窗口和扁平窗口下页面、画布、Inspector 与主要操作仍可访问。

## 注意事项

- 文本 UI 规范优先于草稿图片。
- 不在 Widget 中复制 Domain 校验、UseCase 编排或排版算法。
- 不暴露未实现的批量、术语库等占位导航；功能进入对应版本后再加入信息架构。
- 本阶段不实现 Split Curtain；未来若引入，应作为独立交互提案重新验收可访问性和渲染成本。
