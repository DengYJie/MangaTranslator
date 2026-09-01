# 漫画编辑器 UI 设计

## 1. 页面目标

编辑器是 MangaTranslator 的核心工作区。用户在同一视觉上下文中选择页面、观察漫画、修正文本区域、执行 AI 流水线并完成排版。界面必须优先保护画布面积、输入焦点和人工修改。

Qt/FluentQt 实现边界、渲染缓存、generation、Undo/Dirty、缩略图和凭据契约见 [P5 编辑器补充规格](../../plan/v0.1/phase-5-ui-editor-supplement.md)；本文仍是用户可观察行为与信息架构的权威来源。

WinUI 3 参考控件：

- `CommandBar`、`AppBarButton`、`AppBarToggleButton`、`AppBarSeparator`、`SplitButton`。
- `ListView`、`AutoSuggestBox`。
- `SelectorBar`。
- `Expander`。
- `TextBox`、`ComboBox`、`NumberBox`、`Slider`、`ToggleSwitch`、`Button`。
- `ProgressBar`、`ProgressRing`、`InfoBar`、`InfoBadge`。
- `MenuFlyout`、`Flyout`、`ContentDialog`、`TeachingTip`、`ToolTip`。

自定义控件：

- [MangaCanvas](../components/manga-canvas.md)
- [RegionOverlay](../components/region-overlay.md)
- [PageThumbnailList](../components/page-thumbnail-list.md)
- [AppStatusTaskStrip](../components/status-task-strip.md)

WinUI 3 没有可直接等同于三栏可拖动桌面编辑器的控件。左右 Pane 与分隔条是 MangaTranslator 自定义布局行为，不把它称为 WinUI 3 `SplitView`。

## 2. 页面解剖

编辑器主体从左到右：

1. `PageThumbnailList`。
2. Canvas Workspace。
3. Inspector。

Canvas Workspace 从上到下：

1. `CommandBar`。
2. Canvas viewport。

`CommandBar` 使用 `bgLayer`，Canvas viewport 使用 `bgCanvas`。命令栏按钮均为 IconOnly，通过 Tooltip、无障碍名称和快捷键提示表达完整语义；按钮按历史、视图、工具、预览、处理和面板分组，组间使用 1 px 分隔线。

Preview `SelectorBar` 固定嵌入 `CommandBar` 的视图命令区，不在画布上另建浮动层。窄宽时整组进入 CommandBar 的可展开区域，仍保持当前模式可见。

窗口最底部为全局 `AppStatusTaskStrip`，不属于编辑器页面内部。

## 3. Pane 尺寸与分隔条

### 3.1 默认尺寸

- 页面 Pane：220–248 px；Wide（`>=1440`）默认 232 px，Standard（`1200–1439`）默认 220 px。
- Inspector：320–380 px，默认 352 px。
- 分隔条视觉宽度 1 px，指针命中宽度 8 px。
- Canvas 最小可用宽度建议 480 px。

### 3.2 拖动

- Hover 分隔条时显示左右调整光标和轻微 Accent 高亮。
- 拖动实时调整，不播放缓动动画。
- 双击分隔条恢复当前响应式档位的默认宽度；页面 Pane 在 Wide 恢复 232 px，在 Standard 恢复 220 px。
- 宽度保存到用户偏好；不同 DPI/窗口大小恢复时需 Clamp。
- Pane 被折叠后，分隔条也隐藏，不能留下空白命中区。

### 3.3 折叠顺序

当空间不足时：

1. 页面 Pane 先从完整行切换为紧凑缩略图栏。
2. 页面 Pane 再折叠为 Overlay。
3. Inspector 折叠为右侧 Overlay。
4. Canvas 始终保留；不通过把它压缩到不可编辑宽度来维持左右 Pane。

用户可以通过 CommandBar 中的“页面”与“属性”按钮手动切换 Pane。按钮使用 `AppBarToggleButton` 语义，并与 Pane 可见状态双向同步。

## 4. PageThumbnailList

详细合同见 [page-thumbnail-list.md](../components/page-thumbnail-list.md)。页面级要求：

- Pane Header 显示 `页面`、页数、搜索按钮和 Overflow。
- 搜索激活后，Header 变为 `AutoSuggestBox`；Escape 清空并返回标题模式。
- 普通单击切换唯一当前编辑页；Shift/Ctrl 多选只改变批量选择集合，不切换当前编辑页。
- 每行显示缩略图、页码和状态：未处理、处理中、已完成、需要检查、失败。
- 当前页有明确选中背板与 Accent 指示器；正在后台处理的其他页用小型进度或状态图标表达。
- 多页更新不得导致当前页行跳动或滚动位置变化。

页面行右键菜单：

- 打开。
- 重新 OCR。
- 重新翻译。
- 标记为已校对 / 取消标记。
- 导出此页。
- 从项目移除；危险操作放在分隔符后。

## 5. CommandBar

### 5.1 命令分组

Primary Commands 从左到右：

1. 历史：Undo、Redo。
2. 视图：缩小、缩放值、放大、适应页面、100%。
3. 工具：选择、平移、创建文字区域。
4. 自动处理：OCR、智能翻译。
5. 输出：导出。

Secondary Commands / Overflow：

- 清除原文。
- 仅排版。
- 重新生成 Mask。
- 页面排序。
- 显示辅助线、Region ID、置信度。
- 页面 Pane / Inspector 可见性。
- 页面诊断和处理日志。

### 5.2 控件选择

- Undo/Redo、OCR、导出：`AppBarButton`。
- 选择、平移、创建 Region：互斥 `AppBarToggleButton`。
- 智能翻译：`SplitButton`。主侧执行最近的页面流水线配置；箭头侧选择阶段组合和上下文范围。
- 缩放值：可点击 `DropDownButton`，菜单含 25%、50%、100%、200%、适应页面、适应宽度。
- Overflow 使用 CommandBar 自带 More 语义，不另放第二个三点菜单。

### 5.3 响应式优先级

空间不足时依次进入 Overflow：

1. 辅助视图命令。
2. 单独排版/清除等低频阶段。
3. 100% 和适应宽度等缩放预设。
4. OCR 可以缩为图标，但智能翻译、Undo/Redo 和当前工具必须可见。

任何宽度下 CommandBar 保持单行。禁止通过换行把画布顶部变成两层工具栏。

### 5.4 Enabled 规则

- 无项目：所有编辑命令禁用，仅保留打开项目入口。
- 无当前页：页面相关命令禁用。
- 当前工具手势进行中：破坏当前手势的导航/处理命令禁用，Escape 可取消。
- 当前页某阶段运行中：该阶段禁止重复提交；相互独立命令是否可用由数据锁定范围决定。
- 有文本输入焦点：`Ctrl+Z/Ctrl+Y` 仍路由到项目级 `QUndoStack`；文本控件关闭独立 Undo 历史，已 commit 的文本变化通过 `EditTextCommand` 合并，IME preedit 不进入命令栈。采用单一历史是为了避免控件值与 Domain、Dirty、画布预览和导出快照分叉，完整理由见 [P5 编辑器补充规格](../../plan/v0.1/phase-5-ui-editor-supplement.md)。

## 6. Canvas Workspace

详细合同见 [manga-canvas.md](../components/manga-canvas.md)。页面级要求：

- 画布工作区使用 FluentQt `bgCanvas` 主题色作为中性背景，并支持细网格/无网格偏好。画布背景不得使用硬编码颜色。
- 未打开项目时，画布中心显示轻量状态卡，仅包含文件夹图标、`尚未打开项目`、简短说明和 `打开项目` 主按钮，不使用全屏遮罩。
- 页面居中显示，周围保持至少 24 px 安全空间。
- V0.1 固定为每页独立记住缩放和中心位置；切回页面时恢复该状态。未访问页面首次打开使用适应页面。V0.1 不提供“统一适应页面”全局偏好。
- Region 选择与 Inspector 选择使用稳定 Region ID 同步。
- 异步图层到达时只更新对应图层，不重置缩放、平移或选择。

### 6.1 Preview SelectorBar

四种互斥视图：

- 原图。
- 清理。
- 翻译。
- 对比。

视图选择在离开局部控件后仍保持，不是仅在按下期间生效的临时 Toggle。键盘左右键切换，选择后焦点仍可返回画布。

`清理` 图层不存在时该项仍可选择，但画布显示内联空状态：`尚未生成清理图层` 与 `立即清除`，而不是禁用得无法理解原因。

### 6.2 对比模式

V0.1 使用并排比较：

- 左侧显示原图，右侧显示翻译结果，两侧标题始终可见。
- 两侧使用相同的缩放比例、页面中心和翻页状态；在任一侧平移或缩放，另一侧同步响应。
- 两侧页面保持相同显示尺寸，不允许因各自独立适应窗口而造成视觉错位。
- 分隔区域只用于区分两个视图，不提供擦拭 Handle。
- 不与 Region 编辑同时启用；进入对比模式时两侧固定显示只读 Region 轮廓，不显示 Handle、Hover 编辑状态或编辑入口，也不提供“显示 Region”开关。
- 滑块擦拭比较属于后续版本。

## 7. Inspector

Inspector 只编辑当前选择的一个 Region。多选时显示批量可安全修改的属性子集；无选择时显示页面摘要和引导。

### 7.1 Header

- 标题：`文本区域`。
- 次级文本：Region ID 或 `已选择 3 个区域`。
- Overflow：复制 Region ID、恢复自动结果、删除区域。

### 7.2 Translation section

默认展开，参考 `Expander`。

内容：

- 原文 `TextBox`，多行，显示 OCR 置信度与人工修改状态。
- 译文 `TextBox`，多行，支持 IME。
- 目标语言 `ComboBox`，通常继承项目设置。
- 风格 `DropDownButton`：自然、简短、口语、保留日式表达、自定义。
- `AI 重译` Button。
- 候选译文列表；使用紧凑 `ListView`，单击预览，双击或明确 `采用` 按钮提交。

编辑行为：

- 输入期间采用短延迟本地预览，但不对每个键触发远程翻译。
- 离开字段或短暂空闲后保留文本修改；IME composition 期间不得提交半成品。
- 人工修改后设置 `manualOverride=true`。重新 OCR/翻译前显示非模态冲突选项，不静默覆盖。
- 候选内容必须显示来源模型或风格，避免不明来源。

### 7.3 Typesetting section

默认展开。内容：

- 字体：`ComboBox`，项可有字体预览但必须保持列表性能。
- 字号：`NumberBox` + `自动适配` ToggleSwitch。
- 字重：`ComboBox`。
- 文本色与描边色：Button 打开颜色选择 `Flyout`；该 Flyout 显示当前颜色、推荐色、最近使用颜色和可访问的自定义颜色输入。
- 描边宽度：`NumberBox`，单位为源图像像素，范围 0–32。
- 对齐：三项互斥 Toggle。
- 方向：横排/竖排，使用两项 `RadioButtons` 或互斥 Toggle。
- 行距、字距、内边距：`NumberBox` 或 `Slider + NumberBox`，保证精确输入。
- 排版溢出状态：字段下方文本 + Caution 图标，画布 Region 同步显示。

自动适配开启时，字号输入显示计算结果但不可编辑；关闭后恢复用户最后手工字号。

### 7.4 Region section

默认收起。内容：

- X、Y、W、H：`NumberBox`，规范坐标为源图像像素。
- 锁定比例：`ToggleSwitch`。
- 阅读顺序：显示 `orderIndex` 的只读摘要；V0.1 不提供独立调整工具。
- `恢复自动区域` Button：仅存在自动结果且已人工修改时可用。

V0.1 不显示旋转和多边形编辑控件。后续版本加入这些能力时，应在本分组中提供明确入口，并保留当前矩形编辑体验。

数值提交必须经过 Clamp 和验证。输入非法时保持编辑文本并显示错误，不把 Region 瞬间跳回 0。

### 7.5 Cleaning section

默认收起。V0.1 只提供与当前 Domain/Adapter 一致的显式纯色清除：颜色、透明度、`应用清除` 和 `恢复原文`。自动取色、Mask 扩张/羽化、OpenCV Inpaint 与高级模型没有已冻结契约，不显示禁用占位；在对应版本实现后再加入。

## 8. 选择状态

### 8.1 无选择

Inspector 显示当前页摘要：Region 数、OCR/翻译完成数和问题数。不要显示操作提示或一组全部禁用的表单。

页面模式使用紧凑的 Fluent 信息层级：顶部显示页码、图像尺寸和阅读方向；下方在统一卡片中以单列信息行显示文本区域、OCR、翻译和待检查数量。页面模式不展示 Region 编辑字段，也不重复提供打开项目、操作提示或选择工具按钮；项目入口只保留在 Canvas 空状态，工具切换只保留在 CommandBar。

### 8.2 单选

显示完整 Region 属性。焦点默认保留在画布，不自动跳到第一个字段；用户双击文字或按 Enter 才进入译文输入。

### 8.3 多选

只显示共同可编辑属性：字体、字重、颜色、对齐、方向、标记状态。不同值显示 `混合`。原文、译文和精确坐标不提供批量输入。

### 8.4 锁定/处理中

如果当前 Region 正在被某任务写入：

- 仍允许查看。
- 会冲突的字段只读并显示阶段与取消入口。
- 不冲突的排版预览可继续编辑时，必须通过版本合并保护人工修改。

## 9. 异步流水线体验

### 9.1 单阶段

点击 OCR/翻译后：

- 命令进入 Busy，图标旁显示局部进度。
- 画布对应 Region 显示非阻塞状态覆盖。
- AppStatusTaskStrip 显示页面、阶段和取消入口。
- 用户可切换页面；结果按项目、页面和请求 ID 合并。

### 9.2 智能翻译

流水线：OCR → 翻译 → 清除 → 排版。

V0.1 只处理用户已创建的矩形 Region。当前页没有 Region 时，智能翻译入口不启动空流水线，而是在画布内提示先创建 Region。自动文字检测属于 V0.2，届时必须把 `ITextDetector`、UseCase、Adapter 与 UI 命令作为同一纵向能力交付；P5 不显示无契约的检测按钮或占位入口。

- `SplitButton` 主侧启动前显示当前配置摘要 ToolTip。
- 第一次付费远程调用可显示 `ContentDialog` 或设置中已接受的费用策略提示；之后不重复打断。
- 启动后显示阶段式进度，允许取消剩余阶段。
- 阶段失败时默认停止该页后续依赖阶段，保留已成功结果；用户可从失败阶段重试。

### 9.3 过期和冲突

- 切换页面只失效当前页图像/预览的 `viewGeneration`，不自动取消可后台完成的页面任务。
- OCR、翻译、清除、排版与导出绑定 `projectSessionGeneration + operationId + ProjectId/PageId/RegionId`；只有关闭/切换项目、用户取消或同目标替换才使它们过期。
- 删除 Region、关闭项目或重新运行同阶段会使旧请求过期。
- 旧结果被拒绝时不弹错误 Toast；可在诊断日志记录。
- 若用户在翻译运行期间修改原文，结果返回后显示 `有新的 AI 译文可比较`，不能覆盖当前人工稿。

## 10. 响应式布局

### 10.1 Wide `>=1440`

- 三栏常驻。
- 页面 Pane 默认 232 px。
- Inspector 352–380 px。
- CommandBar 显示主要命令标签。

### 10.2 Standard `1200–1439`

- 三栏常驻。
- 页面 Pane 220 px，Inspector 328–352 px。
- CommandBar 次要标签隐藏，ToolTip 补充。

### 10.3 Compact `1008–1199`

- 页面 Pane 切为 80–96 px 紧凑缩略图栏或按上次偏好折叠。
- Inspector 320 px；必要时 Overlay。
- Preview SelectorBar 保持在 CommandBar 内；空间不足时整组进入可展开区域，不悬浮到画布。

### 10.4 Narrow `720–1007`

- Canvas 占满主体。
- 页面列表与 Inspector 都为 Overlay Pane，一次只打开一个。
- CommandBar 显示 Undo/Redo、当前工具、智能翻译和 Pane Toggle，其余进入 Overflow。
- Overlay 打开时画布维持位置，不重新适应页面；关闭后恢复全部宽度。

### 10.5 Minimum `<720`

- 只保证基础校对，不保证高效三栏编辑。
- CommandBar 使用图标和 Overflow；智能翻译仍保留可识别主入口。
- Inspector Overlay 占可用宽度的 85–100%，表单垂直排列。
- 软件不应阻止打开，但可通过 InfoBar 建议扩大窗口。

## 11. 快捷键

| 快捷键 | 行为 |
| --- | --- |
| `Ctrl+S` | 保存项目 |
| `Ctrl+Z` / `Ctrl+Y` | 上下文 Undo/Redo |
| `V` | 选择工具，文本输入未聚焦时 |
| `H` 或 Space 按住 | 平移工具 / 临时平移 |
| `R` | 创建 Region 工具 |
| `+` / `-` | 缩放 |
| `0` | 适应页面 |
| `1` | 100% |
| `Delete` | 删除选中 Region；文本编辑时删除文字 |
| `Enter` | 编辑选中 Region 的译文 |
| `Escape` | 按 IME/文本编辑、Overlay、页面导航的嵌套顺序逐层退出 |
| `PageUp` / `PageDown` | 上一页 / 下一页 |
| `Ctrl+Shift+T` | 对当前页执行智能翻译；V0.1 固定，不提供配置入口 |

快捷键在 ToolTip 和命令 Overflow 中显示。不能覆盖 IME 组合输入。

### 11.1 Escape 嵌套顺序

Escape 每次只处理最内层且消费一次按键，不允许同一次按键同时关闭多个层级：

1. 存在 IME preedit 时先取消当前组合串，不写 Domain、不生成 Undo Command。
2. 文本编辑会话活动时结束该会话并把焦点交还 Inspector/画布；已 commit 的文本保留在项目命令栈中。
3. 有画布创建、移动、缩放等临时手势时取消手势并恢复起始投影。
4. 关闭当前 Flyout；若无 Flyout，则关闭当前 Inspector/Page Overlay。
5. 以上状态均不存在时才执行页面级后退导航。

## 12. 无障碍

- 三个主要 Pane 各有可访问区域名称，并可通过 F6 或等价 Pane 循环快捷键切换。
- CommandBar 按视觉顺序聚焦；Overflow 中命令保持相同名称和快捷键。
- MangaCanvas 提供 Region 虚拟无障碍子项，详见控件文档。
- Inspector 标签与输入建立明确关联；错误消息被输入控件引用。
- 状态不只靠红/绿，必须播报 `失败`、`完成`、`需要检查`。
- 拖动分隔条提供键盘替代：聚焦后方向键 8 px 调整，Shift+方向键 1 px。

## 13. 验收场景

- 无 Region、1 个 Region、300 个 Region 的页面。
- 竖排、横排、长译文、空译文、低置信度和排版溢出。
- OCR/翻译运行时切页、关闭项目、删除 Region、编辑原文。
- 页面列表与 Inspector 各自折叠/Overlay，焦点不丢失。
- 720、1008、1200、1440 px 和低高度窗口。
- 5%–1600% 缩放、高 DPI、触控板和键盘完整操作。
- Light/Dark/高对比度/减少动画和 200% 文本缩放。
