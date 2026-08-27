# SettingsGroup 与 SettingsRow 自定义控件设计

## 1. 控件定位

WinUI 3 本体没有 `SettingsCard` / `SettingsExpander`；它们属于 Windows Community Toolkit。本项目定义：

- `SettingsGroup`：相关设置的长期 Surface 容器。
- `SettingsRow`：一个设置的标题、说明、状态和编辑控件布局。
- `SettingsExpandableRow`：需要按需显示子选项的行，展开行为参考 WinUI 3 `Expander`。

## 2. SettingsGroup

解剖：

1. 可选组标题，位于 Surface 外上方。
2. 可选组说明。
3. Group Surface。
4. 一个或多个 SettingsRow。

视觉：

- Surface 圆角 8 px。
- 1 px 语义边界或层级色差。
- 行之间使用内部细分隔线；第一/最后行继承外圆角。
- 不给每一行单独完整卡片和阴影，避免“卡片套卡片”。
- Group 间距 16–24 px。

## 3. SettingsRow 解剖

桌面横向布局：

- 左区域：可选图标、标题、说明、错误/状态文本。
- 弹性间距。
- 右区域：单个主编辑控件或紧密组合。

建议尺寸：

- 最小高度 64–72 px。
- 水平内边距 16–20 px。
- 垂直内边距 12 px。
- 标题 14 px Semibold。
- 说明 12–13 px 次级文字。
- 编辑区 220–320 px；ToggleSwitch 自适应内容。

一个 SettingsRow 只表达一个主要决策。多个高度相关的精确数值可使用紧密子布局，但不能把整个复杂表单塞进一行。

## 4. 行类型

### Value Row

右侧为 `ComboBox`、`TextBox`、`PasswordBox`、`NumberBox`、`Slider` 等。

### Toggle Row

右侧 `ToggleSwitch`。整行默认不可点击，避免用户选择说明文字时误切换；若产品明确允许整行点击，Hover 必须表达并保证可访问 Role 正确。

### Action Row

右侧 `Button`，例如测试连接、浏览路径、清理缓存。

### Navigation Row

右侧 Chevron，整行 Button 语义，进入更深页面。当前设置层级较浅，应谨慎使用。

### Status Row

显示只读状态和操作，例如模型版本 + `检查更新`。

### Expandable Row

Header 始终显示，子选项展开。行为参考 WinUI 3 `Expander`：Chevron、Enter/Space 切换、内容从布局中展开而不是浮层覆盖。

## 5. 状态

### Rest

基础 Surface，标题主前景，说明次级前景。

### Hover

仅整行可点击的 Navigation/Toggle Row 显示低强度 Hover Fill。普通含输入控件的行不因指针经过空白处整体高亮。

### Focus Within

行本身不额外画大焦点框；具体输入控件显示标准焦点。若整行是 Button，则行显示完整焦点框。

### Dirty

- 标题旁小型圆点或 `已修改` 次级状态。
- 不能只改背景色。
- Dirty 指示不进入每行 Tab 顺序。

### Saving

- 对应控件暂时禁用或保持可编辑取决于保存模型。
- 行末小型 ProgressRing；组级保存时优先在页面动作区显示，不让每行都转圈。

### Error

- 控件边界 Critical。
- 左侧说明下显示具体错误。
- 行高允许增加；不得把错误覆盖下一行。
- Group 顶部可有汇总 InfoBar，但具体行仍显示错误。

### Disabled by dependency

- 控件禁用，说明文字仍可读。
- 增加原因，例如 `开启页面上下文后可用`。

## 6. 动效

- Hover/Pressed 80–120 ms。
- Error 出现 120 ms 淡入，不摇晃。
- Expandable 内容 180–240 ms 高度/透明度过渡；收起时焦点若在内容内，先移回 Header。
- 控件因依赖启用/禁用使用 120 ms 透明度过渡。
- 主题预览颜色可 150–200 ms 过渡；减少动画时即时。

布局高度动画必须保持平稳；无法保证稳定时，使用内容 Cross-fade 配合即时布局变化。

## 7. 响应式

### Horizontal mode

适用于可用内容宽度 `>=680`：

- 左描述占剩余宽度。
- 右控件固定/限制宽度。
- 长说明换行，控件顶对齐或垂直居中由行内容决定。

### Vertical mode

适用于 `<680` 或文本缩放导致空间不足：

- 标题与说明在上。
- 控件在下，顶部间距 12 px。
- 输入、ComboBox 和 Button 可占满宽度。
- ToggleSwitch 左对齐，不拉伸。
- 行最小高度取消，按内容增长。

模式切换不丢输入焦点和编辑文本。

## 8. 编辑提交

- TextBox：IME composition 期间不提交；失焦、Enter 或防抖后更新当前草稿。
- ComboBox/ToggleSwitch：选择立即更新当前草稿。
- NumberBox：保留非法编辑文本并显示错误；只有合法值更新草稿。
- Slider + NumberBox：拖动 Slider 节流预览，松手提交；NumberBox 精确输入同步 Slider。
- PasswordBox：只产生“替换密钥草稿”，不包含旧真实值。

所有设置修改先进入可撤销的页面草稿；只有明确保存后才成为持久设置，标记为立即生效的安全预览项除外。

## 9. 无障碍

- 标题作为输入的 Name，说明作为 HelpText/Description，错误作为错误描述。
- 行装饰图标不进入 Tab 顺序。
- Expandable Header 暴露 expanded/collapsed State。
- Dirty、Error、Disabled Reason 被屏幕阅读器读出。
- Vertical/Horizontal 模式保持相同焦点顺序。
- 高对比度下 Group 与输入边界使用系统语义色。

## 10. 验收清单

- 标题一行/三行、说明无/多行、错误出现时行不重叠。
- ComboBox、PasswordBox、NumberBox、ToggleSwitch 和 Button 对齐一致。
- 680 px 附近切换布局不丢焦点。
- 200% 文本缩放后页面可滚动，控件不裁切。
- Dirty、Saving、Error 和依赖禁用都有文本语义。
- SettingsGroup 不形成卡片套卡片和过度阴影。
