# WinUI 3 控件参考表

## 1. 使用方式

本表中的控件名仅表示 WinUI 3 的设计与行为参照。原型与最终产品应保持相同的可观察语义、视觉状态、键盘交互和响应行为，而不是机械复制控件名称。

官方总目录：[Windows 控件和模式](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/)。

## 2. 应用外壳与导航

| 场景 | WinUI 3 参考 | MangaTranslator 用法 |
| --- | --- | --- |
| 窗口标题栏 | `TitleBar` | 32 px 标准标题栏；返回、Pane Toggle、图标、应用标题和系统 Caption Buttons |
| 一级导航 | `NavigationView` | 首页、编辑器、批处理、术语库；设置入口固定在主 Surface 底部 |
| 导航项 | `NavigationViewItem` | 图标、文本、选中指示器、可选 `InfoBadge` |
| 少量互斥视图 | `SelectorBar` | 画布的原图、清理、翻译、对比模式 |
| 用户管理文档 | `TabView` | 当前版本不使用；未来只有多项目/多文档可关闭、重排时才采用 |
| 深层路径 | `BreadcrumbBar` | 当前页面层级浅，不使用；未来模型存储路径浏览可考虑 |

`NavigationSurfaceHost` 是自定义行为：它在同一个应用外壳内替换主导航 Surface。详见 [application-shell.md](components/application-shell.md)。

## 3. 命令和菜单

| 场景 | WinUI 3 参考 | 使用规则 |
| --- | --- | --- |
| 编辑器主工具栏 | `CommandBar` | 常用命令进入 `PrimaryCommands`，低频命令进入 Overflow |
| 普通命令 | `AppBarButton` | 图标与标签；紧凑时可只显示图标，但必须有 ToolTip |
| 工具模式 | `AppBarToggleButton` | 选择、平移等互斥模式只能有一个选中 |
| 命令分组 | `AppBarSeparator` | 仅分隔语义组，不按每两个按钮插入 |
| 默认动作 + 选项 | `SplitButton` | 智能翻译、导出；主侧执行最近选项，箭头侧打开选项 |
| 仅打开选项 | `DropDownButton` | Provider、页面范围、排序方式等 |
| 上下文命令 | `MenuFlyout` | 页面行、Region、术语行的右键菜单 |
| 任意轻量内容 | `Flyout` | 颜色、排版预览、简短配置 |

官方参考：[CommandBar](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/command-bar)、[Buttons](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/buttons)、[MenuFlyout](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/menus)。

## 4. 集合和滚动

| 场景 | WinUI 3 参考 | 使用规则 |
| --- | --- | --- |
| 最近项目 | `GridView` | 同质项目卡片，支持键盘选择与响应列数 |
| 页面缩略图 | `ListView` | 单选、虚拟化、键盘上下移动、进入当前页 |
| 术语表 | 无第一方直接对应物 | 使用自定义 `GlossaryTableView`；提供稳定列、排序、整行选择、键盘导航和表格无障碍语义 |
| 高度自定义虚拟集合 | `ItemsRepeater` | 只作为自定义控件构建块；选择、焦点和可访问性由控件补齐 |
| 页面/设置滚动 | `ScrollView` / `ScrollViewer` | 仅在内容溢出时显示滚动条；保留键盘滚动 |

官方参考：[ListView 和 GridView](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/listview-and-gridview)、[ItemsRepeater](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/items-repeater)、[ScrollView](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/scroll-controls)。

## 5. 文本、搜索与表单

| 场景 | WinUI 3 参考 | 使用规则 |
| --- | --- | --- |
| 普通单行/多行文本 | `TextBox` | OCR 和译文编辑；错误与帮助文本在输入下方 |
| 密钥 | `PasswordBox` | 永不回显完整密钥；允许替换和清除 |
| 搜索与建议 | `AutoSuggestBox` | 项目、页面和术语搜索；提交、选择建议和清空行为分开 |
| 长列表选择 | `ComboBox` | Provider、模型、语言、字体 |
| 二元设置 | `ToggleSwitch` | 上下文、术语库、自动保存 |
| 2–4 个互斥选项 | `RadioButtons` | 阅读方向、主题等需要始终看见选项的场景 |
| 连续范围 | `Slider` | 画布缩放、阈值、质量；必须同时显示数值 |
| 精确数字 | `NumberBox` | 字号、X/Y/W/H、超时、并发度 |
| 次级选项折叠 | `Expander` | Inspector 的排版、区域和清除分组 |

官方参考：[AutoSuggestBox](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/auto-suggest-box)、[Forms](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/forms)、[NumberBox](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/number-box)、[Expander](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/expander)。

## 6. 状态、通知与模态层

| 场景 | WinUI 3 参考 | 使用规则 |
| --- | --- | --- |
| 已知进度 | `ProgressBar` | 显示百分比或 `已完成/总数` |
| 未知、局部等待 | `ProgressRing` | 控制在所属 Surface 内；不冻结窗口 |
| 持久非模态信息 | `InfoBar` | 模型缺失、保存失败、离线、源文件移动 |
| 非阻塞提示数量 | `InfoBadge` | 导航中显示失败页数或待校对数，有清除规则 |
| 新功能引导 | `TeachingTip` | 首次使用画布、手工框选或批处理时低频显示 |
| 必须确认 | `ContentDialog` | 删除项目、覆盖导出、放弃未保存修改 |
| 辅助说明 | `ToolTip` | 图标含义和快捷键；不能放交互控件 |

官方参考：[Progress controls](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/progress-controls)、[InfoBar](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/infobar)、[InfoBadge](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/info-badge)、[TeachingTip](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/dialogs-and-flyouts/teaching-tip)、[Dialogs and Flyouts](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/dialogs-and-flyouts/)、[ToolTip](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/tooltips)。

## 7. 明确不存在直接 WinUI 3 对应物的控件

| MangaTranslator 控件 | 为什么不能只引用现有 WinUI 3 控件 |
| --- | --- |
| `NavigationSurfaceHost` | `NavigationView` 支持显示模式和层级导航，但本项目需要替换整套导航项且保持同一 Content Host 与历史 |
| `MangaCanvas` | `ScrollView` 可参考平移缩放，但没有 Scene、图层、Region 编辑和源图坐标模型 |
| `RegionOverlay` | 没有领域特定的多边形、选择柄、OCR/翻译状态覆盖层 |
| `TaskStageGrid` | WinUI 3 本体不包含 DataGrid；普通 `ListView` 不定义流水线单元格交互 |
| `AppStatusTaskStrip` | WinUI 3 没有持续占据窗口底边的桌面状态栏控件 |
| `SettingsGroup` / `SettingsRow` | `SettingsCard`、`SettingsExpander` 属于 Windows Community Toolkit，不是 WinUI 3 本体 |
| `PageThumbnailList` | `ListView` 提供集合基础，但项目需规定缩略图加载、页状态、画布联动和批处理更新 |
| `GlossaryTableView` | 术语数据需要稳定列、排序、列宽、表格键盘导航和大数据虚拟化；WinUI 3 本体没有第一方 DataGrid |

这些控件必须以本目录的自定义控件文档为设计和验收依据。
