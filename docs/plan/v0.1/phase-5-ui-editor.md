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
- [ ] 使用 FluentQt 公共 C++ API 和现有语义 Token。
- [ ] 不添加自定义 QProxyStyle、全局 QSS、`ui/style` 或 themes 目录。

### QGraphicsView 漫画画布

- [ ] 源图像像素坐标作为 Scene 权威坐标，不把 View/设备坐标持久化。
- [ ] 实现图片加载、适应窗口、缩放、平移和缩放中心行为。
- [ ] 实现矩形 Region 创建、选择、移动、八方向缩放和删除。
- [ ] 处理最小尺寸、页面边界、缩放无关命中范围和选择手柄。
- [ ] `QGraphicsItem` 只投影 State，不保存权威 Region 数据。
- [ ] 选择使用稳定 Region ID，不使用列表下标作为身份。

### 编辑与 Undo/Redo

- [ ] Inspector 编辑原文、译文、字体、字号、方向、对齐、行距和边距。
- [ ] 实现原图、清除、译文和并排对比预览。
- [ ] Region 创建/删除/几何、文字、排版修改使用命令对象。
- [ ] 连续拖动合并为一个命令；连续文本输入按目标和时间窗口合并。
- [ ] OCR/翻译结果只在用户采用时进入 Undo 栈。
- [ ] 提供 OCR、翻译、清除、排版、导出、取消和恢复入口。

### 响应与无障碍

- [ ] 覆盖 Light/Dark、正常/窄宽度、高 DPI 和文本缩放。
- [ ] 定义加载、空、错误、取消、字体缺失、溢出和模型不可用状态。
- [ ] 完成键盘焦点顺序、快捷键、Tooltip、可访问名称和 IME 行为。

## 测试与视觉验收

- [ ] `observe` 初始回放、相等 State 抑制和预期状态序列。
- [ ] 页面切换、View 销毁和 generation 变化后的过期结果。
- [ ] Scene/View/源图坐标往返与不同缩放级别命中。
- [ ] Region 命令、文本合并和完整 Undo/Redo。
- [ ] Light/Dark、正常/窄宽度和关键空/错误状态视觉检查。
- [ ] 键盘、IME、Esc 取消和屏幕阅读器名称检查。

## 退出条件

- 用户不依赖自动检测即可创建并完整编辑矩形 Region。
- View 不直接访问 Repository、网络客户端、ONNX Session 或 Credential Manager。
- 窄窗口下页面、画布、Inspector 和主要操作仍可访问。

## 注意事项

- 文本 UI 规范优先于草稿图片。
- 不在 Widget 中复制 Domain 校验或 UseCase 编排。
- 不为视觉常量恢复全局 Metrics/Theme 层；局部 Metrics 放在控件实现内部。
