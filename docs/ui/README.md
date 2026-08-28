# MangaTranslator UI 设计文字稿

> 状态：设计基线草稿
> 设计语言：Windows 11 Fluent Design；以 WinUI 3 官方控件和模式作为行为参考

## 1. 文档目标

本目录描述 MangaTranslator 的界面结构、响应式行为、视觉状态、动效、键盘交互、错误反馈和无障碍要求。它是实现前的产品级 UI 合同，不是 WinUI 3 或 XAML 实现方案。

文档中的 WinUI 3 控件名只用于表达用户可观察的交互语义和视觉基准，例如 `NavigationView`、`CommandBar`、`InfoBar`。没有直接对应物的产品能力使用 MangaTranslator 自定义控件，并由本目录单独定义其内容、样式、功能和行为。

## 2. 严格的控件引用规则

文档只允许以下两种表述：

1. **WinUI 3 参考控件**：Microsoft 官方 WinUI 3 中确实存在的控件或平台 UI，例如 `TitleBar`、`NavigationView`、`CommandBar`、`ListView`、`ProgressBar`。
2. **MangaTranslator 自定义控件**：WinUI 3 没有直接对应物，或现有控件无法满足漫画编辑语义时，单独给出完整设计合同。

不得把 Windows Community Toolkit、第三方库或概念性布局名称写成 WinUI 3 原生控件。例如 `SettingsCard` 和 `SettingsExpander` 属于 Windows Community Toolkit，不作为本项目的 WinUI 3 参考控件；设置页面使用自定义 `SettingsGroup` 与 `SettingsRow`，详见对应控件文档。

## 3. 阅读顺序

1. [V0.1 技术与产品决策](../technical-decisions.md)；设计 V0.2 功能时同时阅读 [V0.2 技术决策](../v0.2-technical-decisions.md)
2. [视觉与交互基础](foundations.md)
3. [WinUI 3 控件参考表](control-reference.md)
4. [应用外壳与多 Surface 导航](components/application-shell.md)
5. 具体页面文档
6. 页面引用的自定义控件文档

## 4. 页面文档

| 页面 | 文件 | 核心任务 |
| --- | --- | --- |
| 首页 / 项目中心 | [pages/home.md](pages/home.md) | 创建、打开和恢复项目 |
| 新建项目 / 导入 | [pages/import.md](pages/import.md) | 导入当前版本支持的来源并建立项目；V0.1 为图片和文件夹 |
| 漫画编辑器 | [pages/editor.md](pages/editor.md) | 检测、OCR、翻译、清除、排版和人工校对 |
| 批量翻译 | [pages/batch.md](pages/batch.md) | 运行、观察、暂停、恢复和重试整章流水线 |
| 术语库 | [pages/glossary.md](pages/glossary.md) | 管理全局、当前项目及最终有效术语 |
| 设置 | [pages/settings.md](pages/settings.md) | 配置引擎、模型、网络、排版、外观、性能和隐私 |

## 5. 自定义控件文档

| 控件 | 文件 | 原因 |
| --- | --- | --- |
| `NavigationSurfaceHost` | [components/application-shell.md](components/application-shell.md) | WinUI 3 没有“同一个 NavigationView 在多个导航 Surface 间替换导航面板”的现成控件 |
| `MangaCanvas` | [components/manga-canvas.md](components/manga-canvas.md) | 漫画页面、非破坏性图层、缩放和平移需要专用二维编辑画布 |
| `RegionOverlay` | [components/region-overlay.md](components/region-overlay.md) | 文本区域的矩形边界、选择柄、置信度和处理状态是产品特有交互；多边形属于后续版本 |
| `PageThumbnailList` | [components/page-thumbnail-list.md](components/page-thumbnail-list.md) | 基于 `ListView` 语义，但增加页面处理状态、惰性缩略图和编辑器定位行为 |
| `TaskStageGrid` | [components/task-stage-grid.md](components/task-stage-grid.md) | WinUI 3 本体没有适合流水线阶段矩阵的 DataGrid 控件 |
| `AppStatusTaskStrip` | [components/status-task-strip.md](components/status-task-strip.md) | WinUI 3 没有桌面工作台式持久状态栏控件 |
| `SettingsGroup` / `SettingsRow` | [components/settings-controls.md](components/settings-controls.md) | WinUI 3 本体没有 SettingsCard/SettingsExpander |
| `GlossaryTableView` | [components/glossary-table-view.md](components/glossary-table-view.md) | 术语需要多列、排序、整行选择、虚拟化和表格无障碍语义，WinUI 3 没有第一方 DataGrid |

## 6. 设计稿

- [可交互 HTML 草稿](drafts/ui-drafts.html)
- [全部页面概览](drafts/overview.jpg)
- [设置 Surface 草稿](drafts/settings.jpg)

草稿只表达信息架构和密度，不是像素级实现依据。本文档中的响应规则、状态和无障碍要求优先于静态图片。

## 7. 全局产品原则

- MangaTranslator 是可编辑的 AI 辅助工作台，不把自动结果表现为不可修改的最终答案。
- 页面切换不能中断可安全后台运行的任务；任务状态必须在离开页面后仍可发现。
- 页面返回后仍需恢复的信息必须保持稳定；Toast、文件选择器和焦点转移等一次性反馈不能伪装成持久页面内容。
- 用户修改优先于后台自动结果；发生冲突时提示并保留双方版本。
- 同一可见区域只突出一个主操作。危险操作不得与主操作使用相同强调色。
- 颜色不是唯一状态编码；状态同时使用文本、图标或形状。
- 所有长任务均可取消；若某阶段技术上不能立即中止，界面应显示“正在停止”，而不是假装已停止。

## 8. 官方参考

- [Windows 控件和模式](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/)
- [NavigationView](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/navigationview)
- [TitleBar](https://learn.microsoft.com/en-us/windows/apps/design/controls/title-bar)
- [响应式设计技术](https://learn.microsoft.com/zh-cn/windows/apps/design/layout/responsive-design)
- [屏幕大小和断点](https://learn.microsoft.com/en-us/windows/apps/design/layout/screen-sizes-and-breakpoints-for-responsive-design)
- [材料](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/materials)
- [辅助功能概述](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessibility-overview)
