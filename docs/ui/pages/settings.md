# 设置 UI 设计

## 1. 页面目标

设置页配置应用级能力，同时保持与主窗口相同的导航外壳。设置是独立 Navigation Surface，不是新窗口，也不是主导航旁的第二层侧栏。

WinUI 3 参考控件：`NavigationView`、`TitleBar`、`ScrollView`、`ComboBox`、`TextBox`、`PasswordBox`、`NumberBox`、`Slider`、`ToggleSwitch`、`RadioButtons`、`Button`、`Expander`、`InfoBar`、`ProgressBar`、`ProgressRing`、`ContentDialog`、`Flyout`、`ToolTip`。

自定义控件：[SettingsGroup 与 SettingsRow](../components/settings-controls.md)。

注意：Microsoft 设置指南提到的 `SettingsCard`、`SettingsExpander` 来自 Windows Community Toolkit，不属于 WinUI 3 本体。因此本项目不把它们列为 WinUI 3 控件，只参考设置页的信息层级，行为由自定义控件文档定义。

## 2. 导航 Surface

进入设置后：

- 左侧只显示设置分类。
- 顶部没有齿轮和 `设置` 标题。
- 分类列表中没有 `设置` 项。
- 底部没有 `返回主界面` 项。
- TitleBar 显示 Back Button；返回恢复进入设置前的 Main Surface 页面。
- TitleBar 只显示应用标题，不显示 `设置 · AI 翻译`。

分类：

### 应用设置

- 常规。
- OCR。
- AI 翻译。
- 模型管理。

### 体验

- 排版与字体。
- 外观。
- 性能。
- 隐私。

分组标题不是可点击项。设置分类可按版本增减，但顺序稳定，不按使用频率重排。

## 3. 内容页公共结构

每个分类是独立设置页面，结构为：

1. H1 分类标题。
2. 一句说明。
3. 可选全宽 `InfoBar`。
4. 一个或多个 `SettingsGroup`。
5. 可选连接测试/模型状态区域。
6. 底部动作区。

设置内容列在共享 Content Host 的可用区域内水平居中，最大宽度建议 1000–1200 px。居中的是包含 H1、说明、`InfoBar`、`SettingsGroup` 和底部动作区的整体内容容器；容器内部的标题、说明和分组仍保持左对齐。不要让内容紧贴设置导航，也不要在宽窗中无限拉伸。设置行宽度过大时，标签与控件之间会产生难以扫视的空洞，因此必须限制内容宽度。

内容列左右使用对称页面边距：宽屏建议 32–36 px，空间收窄时可降至 24 px。最大宽度之外的剩余空间平均分布在内容列两侧；该规则以设置内容区为坐标范围，不包含左侧 Navigation Surface。

设置页面必须垂直滚动；切换分类时：

- 首次进入滚到顶部。
- 同会话返回之前分类可恢复滚动位置。
- 若因验证错误导航到分类，滚动并聚焦第一个错误行。

## 4. 保存模型

首版采用即时持久化：

- 用户修改通过设置 ViewModel 直接更新持久状态并写入设置存储。
- 页面不显示 `保存设置` 按钮、Dirty 状态或离开确认。
- 写入失败时停留在当前页面，并通过 `InfoBar` 显示错误。
- `恢复默认` 仍保留，点击后立即写入全部默认值。
- 主题和 Accent 即时预览，并与持久化写入保持同一次用户操作的一致性。

## 5. 常规

建议设置：

- 启动时恢复上次项目：`ToggleSwitch`。
- 自动保存：`ToggleSwitch`。
- 自动保存间隔：`NumberBox`，只在自动保存开启时启用。
- 默认项目位置：只读 `TextBox` + `浏览` Button。
- 最近项目数量：`NumberBox`。
- 检查更新：`ToggleSwitch` 或 `ComboBox`。
- 语言：`ComboBox`；若需要重启，行内说明并显示 InfoBar。

自动保存关闭时显示 Caution 文案，但不每次弹 Dialog。

## 6. OCR

设置组：

### 默认引擎

- OCR 引擎：`ComboBox`。
- 默认语言：`ComboBox`。
- 自动识别横排/竖排：`ToggleSwitch`。

### 识别参数

- 最长边预处理尺寸和语言提示使用受支持的引擎选项；高级参数默认收起。
- 修改只影响新 OCR 请求，不自动重跑已处理 Region。

V0.1 不提供自动文字检测，因此本页不显示检测器、检测阈值、Mask 扩张或合并文本行设置。自动检测进入 V0.2 时，须与 `ITextDetector`、UseCase、Adapter 和编辑器命令一并交付，不能先放不可执行的设置项。

### 状态

- 当前模型版本、大小、校验状态。
- 模型缺失时使用 Caution `InfoBar`，动作 `打开模型管理`。
- 修改影响新任务，不自动重跑已处理页面；说明必须可见。

## 7. AI 翻译

### Provider group

- Provider：`ComboBox`。
- API Endpoint：`TextBox`。
- API 密钥：`PasswordBox` 或掩码状态 + `更新密钥` Button。
- 默认模型：依 Provider 动态加载的 `ComboBox`。
- 超时：`NumberBox`。
- 重试次数：`NumberBox`。

密钥行为：

- 已保存时只显示 `已配置` 或固定掩码，不把真实密钥填回 UI。
- 配置状态通过 `CheckCredentialStatusUseCase` 查询，底层只调用 `hasApiKey`，不得读取后丢弃明文；替换和删除分别走 `SetCredentialUseCase`、`ClearCredentialUseCase`。
- `更新密钥` 后进入替换输入；取消不清除原密钥。
- 提供 `移除密钥` 危险次要动作，使用 `ContentDialog` 确认影响。
- 复制、选择全部和可访问 Value 不得暴露真实密钥。

### Context group

- 页面上下文：`ToggleSwitch`。
- 前后页上下文数量：`NumberBox`，只在页面上下文开启时可用。
- 使用术语库：`ToggleSwitch`。
- 默认翻译风格：`ComboBox`。
- 自定义系统提示：多行 `TextBox`，高级设置 `Expander` 内。

### Connection test

- Button：`测试连接`。
- 运行时局部 `ProgressRing`，按钮文案 `正在测试`。
- 成功：Success 状态、模型名与响应时间；结果自动过期，不长期显示“在线”。
- 失败：Critical `InfoBar`，区分认证、网络、限流、模型不存在和响应格式错误。
- 测试不发送漫画正文；说明明确可见。

## 8. 模型管理

列表参考 `ListView`：

- 模型名、用途、版本、大小、位置、状态。
- 状态：未安装、正在下载、可用、需要更新、校验失败、不兼容。
- 操作：下载、暂停、继续、重试、删除、打开位置。

下载使用 `ProgressBar`，每个模型独立。删除模型使用 `ContentDialog`，说明哪些功能受影响。模型正在被任务使用时禁止删除，并提供查看任务。

模型存储位置修改后不立即移动现有模型，除非用户选择迁移。迁移必须有进度、取消和磁盘空间检查。

## 9. 排版与字体

- 默认字体：`ComboBox`，支持搜索或字体预览。
- 备用字体列表：`ListView`，支持重排。
- 最小/最大字号：`NumberBox`。
- 默认方向：`RadioButtons`，自动/横排/竖排。
- 默认对齐：三项互斥 Toggle。
- 行距、字距、边距：`NumberBox`。
- 自动适配：`ToggleSwitch`。
- 缺失字体替代策略：`ComboBox`。

右侧或组底部可显示短文本排版预览。预览是自定义绘制 Surface，但不需要成为公共控件；它不得加载漫画内容。

## 10. 外观

- 应用主题：`RadioButtons` 或 `ComboBox`，系统/浅色/深色。
- Accent：Button 打开颜色 Flyout；提供 `使用系统 Accent`。
- 界面密度：舒适/紧凑，使用 `RadioButtons`。
- 文本缩放：遵循系统为默认；只在确有产品需求时提供应用级 `Slider`。
- 画布背景：深灰级别或自定义颜色。
- 动画：遵循系统减少动画；可以提供额外 `降低画布动效` Toggle，但不能覆盖系统无障碍偏好。

主题和 Accent 即时预览。切换时：

- 150–200 ms 颜色过渡，减少动画时即时。
- 所有 Flyout、画布 Overlay、原生 Qt 控件和 Caption Buttons 同步更新。
- 不允许出现一半旧主题、一半新主题的状态。

## 11. 性能

- 执行设备：`ComboBox`，自动/CPU/可用 GPU Provider。
- 性能档位：节能/平衡/性能，`RadioButtons`。
- 最大并发页：`NumberBox`。
- 图像缓存：`NumberBox` 或预设 `ComboBox`。
- 显存上限：`NumberBox`，仅支持时显示。
- 后台任务降速：`ToggleSwitch`。
- 清理缓存：Button，先计算大小，执行后显示结果。

不向普通用户暴露难以理解的线程细节。高级参数放在 `Expander`，并提供恢复默认。

## 12. 隐私

- 远程翻译前提示：`ToggleSwitch`。
- 日志包含文本内容：默认关闭，`ToggleSwitch`，开启时显示 Caution InfoBar。
- 保存历史翻译请求：默认关闭。
- 清除请求历史：危险 Button + `ContentDialog`。
- 清除日志：Button。
- 打开数据目录：Button。
- 隐私说明：可访问的 `HyperlinkButton` 或普通链接语义。

任何隐私开关都应说明影响范围、默认值和是否需要重启。

## 13. SettingsRow 响应布局

### `>=1008`

- 整体设置内容列在 Navigation Surface 右侧的 Content Host 内限宽居中。
- 行标题/说明在左，控件在右。
- 右侧控件宽度 220–320 px；ToggleSwitch 按内容宽度。
- Group 内行之间用细分隔线或间距，不把每行做独立厚卡片。

### `720–1007`

- 导航可为 Overlay。
- 内容列占满扣除对称页面边距后的可用宽度，不再为了维持最大宽度留出额外空白。
- 内容仍可使用左右行，但长输入占更大宽度。

### `<720`

- 内容列保持 16–24 px 对称边距，内部仍左对齐。
- SettingsRow 切为垂直：标题、说明、控件依次排列。
- 控件占满可用宽度；ToggleSwitch 除外。
- Button 区可换行，主按钮最后且最显著。

## 14. 切换分类与持久化错误

- 分类切换直接读取当前持久状态，不保留页面草稿，也不弹离开确认。
- 返回 Main Surface 直接执行导航；设置写入失败时停留在 Settings Surface 并显示 InfoBar。
- 外部配置变化通过重新加载设置刷新页面，当前输入不会作为未保存草稿保留。

## 15. 键盘与无障碍

- 设置分类 Pane 使用方向键导航；Enter 选择。
- 每个 SettingsRow 的标题、说明、控件和错误建立可访问关联。
- 依赖设置禁用时播报原因，例如 `页面上下文数量，不可用，因为页面上下文已关闭`。
- PasswordBox 不播报实际值。
- 主题切换不得丢失当前焦点。
- 高对比度模式下禁用 Mica/Acrylic，分组和输入保留边界。

## 16. 验收场景

- 从首页、编辑器、导入页进入设置并返回原页面。
- 设置导航顶部没有“设置”，TitleBar 没有重复页面标题。
- 宽屏设置内容列在 Content Host 内限宽居中；标题和 SettingsGroup 内部保持左对齐。
- Provider 切换后模型列表加载、失败和空状态。
- 更新/移除密钥不会回显旧值。
- 模型下载、暂停、校验失败、使用中删除。
- 修改主题后立即应用并持久化。
- 设置持久化失败时留在当前页面并显示错误。
- 720/1008 px、低高度滚动、200% 文本缩放、高对比度和仅键盘操作。
