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

1. [技术决策](../technical-decisions.md) 与 [功能状态与清单](../feature-status.md)
2. [视觉与交互基础](foundations.md)
3. 具体页面文档（`pages/`）
4. 自定义控件文档（`components/`）

## 4. 页面文档

| 页面 | 文件 | 核心任务 |
| --- | --- | --- |
| 首页 / 项目中心 | [pages/home.md](pages/home.md) | 创建、打开和恢复项目 |
| 新建项目 / 导入 | [pages/import.md](pages/import.md) | 导入当前版本支持的来源并建立项目（支持图片、文件夹、CBZ、PDF） |
| 漫画编辑器 | [pages/editor.md](pages/editor.md) | 文本选区（矩形/多边形）、OCR、翻译、清除、排版、人工校对与标题栏轻量批量处理菜单 |
| 术语库 | [pages/glossary.md](pages/glossary.md) | 管理全局、当前项目及最终有效术语 |
| 设置 | [pages/settings.md](pages/settings.md) | 配置引擎、模型、网络、排版、外观、性能和隐私 |

## 5. 自定义控件文档

| 控件 | 文件 | 说明 |
| --- | --- | --- |
| `MangaCanvas` | [components/manga-canvas.md](components/manga-canvas.md) | 漫画页面、非破坏性图层、缩放和平移专用二维编辑画布 |
| `RegionOverlay` | [components/region-overlay.md](components/region-overlay.md) | 文本区域的矩形与多边形边界、选择句柄、置信度和处理状态交互 |
| `PageThumbnailList` | [components/page-thumbnail-list.md](components/page-thumbnail-list.md) | 页面缩略图列表、惰性加载与编辑器联动定位 |
| `AppStatusTaskStrip` | [components/status-task-strip.md](components/status-task-strip.md) | 底部全局与后台异步任务状态条 |
| `SettingsGroup` / `SettingsRow` | [components/settings-controls.md](components/settings-controls.md) | Fluent 风格设置卡片与分组行控件 |
| `GlossaryTableView` | [components/glossary-table-view.md](components/glossary-table-view.md) | 术语表格多列、排序与编辑 |

## 6. 全局产品原则

- MangaTranslator 是可编辑的 AI 辅助工作台，不把自动结果表现为不可修改的最终答案。
- 页面切换不能中断可安全后台运行的任务；任务状态必须在离开页面后仍可发现。
- 页面返回后仍需恢复的信息必须保持稳定；Toast、文件选择器和焦点转移等一次性反馈不能伪装成持久页面内容。
- 用户修改优先于后台自动结果；发生冲突时提示并保留双方版本。
- 同一可见区域只突出一个主操作。危险操作不得与主操作使用相同强调色。
- 颜色不是唯一状态编码；状态同时使用文本、图标或形状。
- 所有长任务均可取消；若某阶段技术上不能立即中止，界面应显示“正在停止”，而不是假装已停止。

## 7. 官方参考

- [Windows 控件和模式](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/)
- [NavigationView](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/controls/navigationview)
- [TitleBar](https://learn.microsoft.com/en-us/windows/apps/design/controls/title-bar)
- [响应式设计技术](https://learn.microsoft.com/zh-cn/windows/apps/design/layout/responsive-design)
- [屏幕大小和断点](https://learn.microsoft.com/en-us/windows/apps/design/layout/screen-sizes-and-breakpoints-for-responsive-design)
- [材料](https://learn.microsoft.com/zh-cn/windows/apps/develop/ui/materials)
- [辅助功能概述](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessibility-overview)
