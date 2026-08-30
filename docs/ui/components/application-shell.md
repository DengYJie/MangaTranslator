# ApplicationShell 与 NavigationSurfaceHost

## 1. 控件性质

- `TitleBar`：WinUI 3 参考控件。
- `NavigationView`、`NavigationViewItem`、`InfoBadge`：WinUI 3 参考控件。
- `NavigationSurfaceHost`：MangaTranslator 自定义导航协调控件。

该外壳参考 ForgeAI `NavigationWindow` 的思路：应用只有一个窗口、一个 NavigationView 语义控件和一个共享内容宿主，但导航面板可以在不同 Surface 之间切换。它不是两个叠加侧栏，也不是进入设置后创建第二个窗口。

## 2. 区域解剖

自上而下：

1. 32 px `TitleBar`。
2. 主体：导航 Surface + 共享内容宿主。
3. 32 px `AppStatusTaskStrip`。

标题栏从左到右严格为：

1. Back Button，仅历史可返回时显示。
2. Pane Toggle Button，在左侧导航模式下显示。
3. 应用图标。
4. 应用标题 `MangaTranslator`。
5. 可选全局内容区；首版为空。
6. 最小拖拽区域。
7. 系统最小化、最大化/还原、关闭按钮。

页面标题不放入标题栏。`AI 翻译`、`术语库`、`批量翻译` 等标题属于页面内容。标题栏不得出现 `设置 · AI 翻译` 之类重复层级。

官方 TitleBar 参考：[TitleBar 控件](https://learn.microsoft.com/en-us/windows/apps/design/controls/title-bar)、[标题栏设计](https://learn.microsoft.com/zh-cn/windows/apps/design/basics/titlebar-design)。

## 3. Surface 定义

### 3.1 Main Surface

主导航项：

- 首页 / 项目中心。
- 漫画编辑器。
- 批量翻译。
- 术语库。

底部固定项：

- 设置。

设置项只存在于 Main Surface 底部。选中设置后，切换到 Settings Surface；它不继续保留在设置分类导航中。

### 3.2 Settings Surface

设置分类示例：

- 分组 `应用设置`：常规、OCR、AI 翻译、模型管理。
- 分组 `体验`：排版与字体、外观、性能、隐私。

Settings Surface 顶部不显示齿轮和“设置”标题，也不包含“设置”导航项。第一个可见元素直接是分组标题或第一条分类项。

Settings Surface 底部不显示“返回主界面”项。返回由共享 TitleBar 的 Back Button 和导航历史负责。

## 4. Surface 切换行为

进入设置时：

1. 记录当前 Main Surface 路由、内容页、选中项和必要滚动位置。
2. 将当前导航面板从共享 NavigationView Chrome 中取出但不销毁。
3. 装入 Settings Surface。
4. 切换共享内容宿主到目标设置分类。
5. 导航历史增加一项，TitleBar Back Button 出现。
6. 焦点进入设置内容页的 H1；若切换由键盘触发，屏幕阅读器播报页面标题。

返回时：

1. 弹出导航历史。
2. 恢复 Main Surface 和原选中项。
3. 恢复之前内容页，而不是强制回首页。
4. 焦点返回原设置入口或原先合理焦点目标。
5. 历史为空后隐藏 Back Button；隐藏时不保留空白按钮命中区。

Surface 切换不得：

- 同时显示主导航与设置导航。
- 新建第二个 NavigationView 造成两个 Pane Toggle。
- 在设置导航顶部额外添加“设置”。
- 让内容页直接改变外壳导航结构或窗口级状态。

## 5. 动效

### 5.1 Surface 进入

- Settings Surface 从导航侧以 16 px 位移配合 0→1 淡入，200 ms。
- 旧 Surface 同时 1→0 淡出并移动 8 px，120 ms；旧 Surface 在淡出完成前不可接收输入。
- 内容页使用独立短进入动画，不与导航面板做整窗横向滑动。
- Back Button 以淡入和 4 px 位移出现，避免导致标题文字突跳；标题起点通过布局一次性更新。

### 5.2 返回

- 动效方向与进入相反，但总时长不超过 220 ms。
- 若启用减少动画，Surface 和内容在同一帧切换，仅保留 80 ms 透明度过渡或完全无动画。

## 6. NavigationView 响应行为

### 6.1 Large / Standard

- Main Surface 默认 Left Compact，约 48–68 px，优先保留编辑空间。
- 用户点击 Pane Toggle 后临时展开；展开宽度建议 240–280 px。
- Settings Surface 默认 Left Expanded，约 220–260 px，保证分类名称完整。
- 用户显式调整或固定 Pane 的偏好可以持久化，但不得让 Settings Surface 收缩到只剩图标而失去分类可理解性。

### 6.2 Compact workbench

- Main Surface 保持 Compact。
- Settings Surface 可保持 220 px；内容区不足时设置行切为垂直布局。
- Pane 打开时内容可被压缩，不能覆盖系统 Caption Buttons。

### 6.3 Narrow / Minimum

- 导航进入 Overlay 模式。
- Pane 打开后使用轻度遮罩；点击内容、按 Escape 或完成导航后关闭。
- Settings Surface 的分类导航作为 Overlay Pane；选择分类后自动关闭，内容页顶部提供当前分类标题。
- 返回按钮仍在 TitleBar，不在 Pane 内复制。

## 7. 导航项视觉状态

每个导航项必须定义：

- Rest：透明或基础层背景，次级前景色。
- Hover：低强度 Fill，100 ms。
- Pressed：更深 Fill，图标/文字轻微降低透明度。
- Selected：选中背板 + 2–3 px Accent 指示条；文字为主前景色。
- Focused：双层焦点框，不能只靠 Accent 指示条。
- Disabled：降低对比度并停止 hover 动效。
- Attention：允许 `InfoBadge` 显示失败或待处理数量，但不能替代错误文案。

选中指示器在同 Surface 内移动时做 160–220 ms 的位置/尺寸过渡；跨 Surface 切换时不尝试让指示器穿越两个面板。

## 8. 键盘与历史

- `Alt+Left`：返回上一历史路由。
- `Alt+Right`：支持前进历史时进入下一路由，否则不执行动作。
- `Ctrl+Tab` 不用于一级导航，保留未来文档 TabView。
- Pane 内方向键移动焦点，`Enter` 或 `Space` 激活。
- Pane 打开后焦点不会被困住；`Tab` 可进入内容，`Escape` 关闭 Overlay Pane。
- 隐藏 Surface 及其子控件必须从 Tab 顺序和无障碍树移除。

## 9. 状态和错误

- 路由不存在：保持当前 Surface 和内容不变，在日志记录开发错误；发布版显示非模态 `InfoBar`，提供回首页动作。
- Surface 构造失败：回退 Main Surface，禁止留下空白导航区。
- 当前设置分类被功能开关移除：导航到同 Surface 的第一个可用分类。
- 页面有未保存表单且用户返回：若数据可自动保存则直接返回；否则显示 `ContentDialog`，按钮使用 `保存并返回`、`不保存`、`继续编辑`。

## 10. 可访问性

- TitleBar Back 与 Pane Toggle 是不同的可聚焦按钮，名称分别为“返回”和“导航菜单”。
- 导航项播报名称、选中状态、可用状态和 InfoBadge 状态。
- 分组标题不是可点击导航项，不进入 Tab 顺序。
- Surface 切换后发送页面标题变更通知，但不重复朗读所有导航项。
- 高对比度下禁用透明材质，保留清晰边界和选中指示器。

## 11. 验收清单

- 设置入口只在 Main Surface 底部出现一次。
- 进入设置后，导航顶部没有“设置”标题，导航列表中没有“设置”项。
- TitleBar 中没有页面标题副本；页面 H1 承担当前位置说明。
- 返回能恢复进入设置前的页面和选中状态。
- 主导航和设置导航共享同一个内容宿主，不叠加。
- 720、1008、1200、1440 px 附近缩放无重叠、无丢失返回入口。
- 键盘、鼠标、高对比度、减少动画和 200% 文本缩放均可完成导航。

## 12. 窗口几何持久化

窗口几何属于外壳内部偏好，不在设置页增加可见开关。外壳通过 `IAppSettingsStore` 保存和恢复：

- `ui/mainWindow/normalGeometry`：非最大化状态的窗口几何。
- `ui/mainWindow/maximized`：关闭前是否最大化；不恢复最小化状态。
- `ui/editor/pagePaneWidth/wide`、`ui/editor/pagePaneWidth/standard` 与 `ui/editor/inspectorWidth`：桌面三栏模式的逻辑像素宽度。

保存最大化窗口时同时保留最近一次 normal geometry。恢复时先根据当前屏幕集合 Clamp：窗口至少有标题栏和 64×64 px 内容落在某个可用屏幕内；完全离屏或尺寸无效时使用当前主屏幕居中默认值。Pane 宽度按当前断点和 Canvas 最小宽度 Clamp；Overlay/抽屉的临时宽度不得覆盖桌面三栏宽度偏好。
