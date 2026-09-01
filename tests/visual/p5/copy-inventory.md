# MangaTranslator Phase 5 产品文案审计清单 (Copy Inventory)

## 1. 目标受众、语调与本地化规范

### 1.1 目标受众与语调原则
- **目标受众 (Audience)**: 漫画汉化/翻译者、嵌字编辑、修图人员与本地化工作室 (`manga translators/editors`)。
- **语调与风格 (Voice & Tone)**: `direct, calm, technical`（直接、沉稳、专业技术导向）。
  - 直奔主题，清晰说明系统状态、对象与影响，避免繁冗修辞。
  - 保持工具软件的客观与冷静，不使用夸张的情感化表述。
  - 精确使用术语（例如“文本区域”、“字号 (pt)”、“行距”、“描边”、“图层”、“ONNX 模型”、“API 密钥”）。

### 1.2 禁用与规范模式 (Prohibited vs Preferred Patterns)
- **严禁 AI 拟人化/过度叙事 (No AI Narration)**:
  - ❌ 禁用: “AI 正在努力思考中...”, “小助手已为您完成翻译！”, “哇，出错了，请稍候重试~”
  - ✅ 推荐: “第 1 页 · 正在翻译”, “翻译已完成”, “请求超时，请检查网络连接”
- **严禁裸协议/内部异常暴露 (No Protocol / Exception Dumps on Primary UI)**:
  - ❌ 禁用: “HTTP 500 Internal Server Error”, “std::bad_alloc at 0x7FFF”, “CURLcode 28”
  - ✅ 推荐: “服务商返回服务器错误，请稍后重试”, “内存不足，请关闭部分页面后重试”, “请求超时（超过配置的超时时限）”
  - 诊断详情 (diagnosticDetails) 仅限写入诊断日志文件，严禁作为 safeMessage 显示在常规 InfoBar / Toast 上。

### 1.3 稳定状态词汇契约 (Stable Terminology Contract)

| 状态语义 (Domain State) | 英文术语 | 中文标准词 (zh-CN) | 说明 |
| :--- | :--- | :--- | :--- |
| **Queued** | Queued | **排队中** | 任务已加入调度队列，等待执行 |
| **Running** | Processing / Running | **处理中** | 任务正在执行计算或网络请求 |
| **Cancelling** | Cancelling | **正在停止** | 收到用户取消请求，正在安全终止 |
| **Completed** | Completed | **已完成** | 任务成功完成全部阶段 |
| **Failed** | Failed | **失败** | 任务因错误终止，可查看原因与恢复 |
| **Cancelled** | Cancelled | **已取消** | 任务已被用户或冲突请求取消，静默处理 |
| **Dirty** | Unsaved / Dirty | **未保存** | 项目存在未持久化到磁盘的修改 |
| **Saving** | Saving | **正在保存** | 正在写入 project.json 与资产文件 |
| **Saved** | Saved | **已保存** | 所有更改已安全持久化到磁盘 |

---

## 2. 空状态审计清单 (Empty States Audit)

| 场景 / 界面 | 呈现位置 | 文案内容 | 辅助说明 / 操作建议 | 动作按钮 |
| :--- | :--- | :--- | :--- | :--- |
| **首页无最近项目** | `HomePage` 中心区域 | “暂无最近项目” | “打开现有项目文件夹或从图片新建项目开始翻译。” | [新建项目/导入] [打开项目] |
| **编辑器无打开项目** | `EditorPage / MangaCanvas` | “打开或创建项目以开始编辑” | 工作区展示占位符与引导 | [打开项目] |
| **项目内无任何页面** | `EditorPage / MangaCanvas` | “项目中还没有页面” | 提示通过导入向导添加漫画图像 | [导入页面] |
| **清理图层未生成** | `MangaCanvas` 清理视图 | “尚未生成清理图层” | 提示该区域或页面尚未运行涂抹清除阶段 | [立即清除] (或运行流水线) |
| **Inspector 无选择区域** | `InspectorPanel` Page 模式 | “未选择文本区域” | “单击画布中的文本区域以查看与编辑属性，或按 R 键绘制新区域。” | — |
| **无候选译文** | `InspectorPanel` 候选列表 | “暂无备选译文” | 提示当前未请求或无多模型候选结果 | — |

---

## 3. 加载与进度状态审计清单 (Loading & Progress States Audit)

| 阶段 / 任务 | 呈现位置 | 标准文案格式 | 进度表达 | 取消入口 |
| :--- | :--- | :--- | :--- | :--- |
| **页面大图加载** | `MangaCanvas` 视图 | “正在加载” / “{页面文件名} · 正在加载” | 居中微型环形进度 | 无 (瞬间本地加载) |
| **OCR 文字识别** | `AppStatusTaskStrip` / CommandBar | “第 {N} 页 · OCR · 处理中” | 局部微调指示 / 阶段图标 | [取消处理] / 任务栏取消 |
| **AI 台词翻译** | `AppStatusTaskStrip` / CommandBar | “第 {N} 页 · 翻译 · 处理中 · {百分比}%” | 百分比与阶段展示 | [取消处理] / 任务栏取消 |
| **背景清除涂抹** | `AppStatusTaskStrip` / CommandBar | “第 {N} 页 · 清除 · 处理中” | 阶段图标 | 任务栏取消 |
| **自动文字排版** | `AppStatusTaskStrip` / CommandBar | “第 {N} 页 · 排版 · 处理中” | 阶段图标 | 任务栏取消 |
| **智能全流程翻译** | `AppStatusTaskStrip` / CommandBar | “第 {N} 页 · 智能翻译 · 处理中 (另有 M 个任务)” | 组合流水线进度 | [取消处理] |
| **批量导入页面** | `ImportPage` 提交按钮 & 进度条 | “正在导入 {M}/{Total}” | 水平 ProgressBar + 计数 | [取消导入] |
| **保存设置中** | `SettingsPage` 保存按钮 | “正在保存...” | 按钮 Disabled + 进度态 | — |

---

## 4. 错误与恢复状态审计清单 (Error & Recovery Audit)

| 错误代码 / 场景 | 呈现控件 | 用户安全文案 (Safe Message) | 可执行恢复动作 (Action) | 动作文案 (Action Text) |
| :--- | :--- | :--- | :--- | :--- |
| **persistence.locked** | `InfoBar` (Home/Editor) | “项目正被另一个 MangaTranslator 实例打开，已锁定以保护数据。” | Retry | “重试” |
| **persistence.not_found** | `InfoBar` (Home) | “未找到项目文件，项目目录可能已被移动或删除。” | ChoosePath | “选择路径” |
| **persistence.permission_denied** | `InfoBar` (Home/Import) | “无法访问项目目录，请检查文件读写权限。” | ChoosePath | “选择路径” |
| **import.target_not_empty** | `InfoBar` (Import) | “目标保存文件夹不为空，请选择一个空文件夹作为项目根目录。” | ChoosePath | “选择其他文件夹” |
| **import.disk_full** | `InfoBar` (Import) | “磁盘空间不足，无法完成导入，请释放空间后重试。” | FreeDiskSpace | “查看保存位置” |
| **ocr.model_missing** | `InfoBar` (Settings/Editor) | “未找到有效的 Baberu OCR 模型文件，请检查模型目录配置。” | LocateModel | “配置模型目录” |
| **llm.unauthenticated** | `InfoBar` (Settings/Editor) | “AI 服务商 API 密钥未配置或已失效，请配置有效密钥。” | Reauthenticate | “配置 API 密钥” |
| **llm.timeout** | `InfoBar` (Editor) | “AI 翻译请求超时，请检查网络连接或增加超时时间设置。” | Retry / OpenSettings | “重试” / “检查设置” |
| **llm.rate_limited** | `InfoBar` (Editor) | “AI 请求频率超出服务商限制，已自动暂停，请稍后重试。” | Retry | “稍后重试” |
| **editor.page_load_failed** | `MangaCanvas` (Editor) | “页面图像加载失败，原图像文件可能已丢失或损坏。” | Retry | “重试加载” |
| **settings.save_failed** | `InfoBar` (Settings) | “保存设置失败，请检查配置目录读写权限。” | Retry | “重试” |

---

## 5. 完成与确认文案审计清单 (Completion & Confirmation Audit)

| 场景 | 呈现形式 | 文案内容 | 附带说明 |
| :--- | :--- | :--- | :--- |
| **全部后台任务完成** | `AppStatusTaskStrip` 摘要 | “已完成全部任务” | 绿色 Checkmark 强调，1.5 秒后平滑淡出 |
| **项目保存成功** | `AppStatusTaskStrip` 文档区 | “所有更改已保存” | 绿色 Checkmark，状态切为 Saved |
| **移除 API 密钥确认** | `ContentDialog` 对话框 | 标题：“确认移除 API 密钥？”<br>内容：“移除密钥后，该服务商的 AI 翻译功能将无法使用，直到重新配置新的密钥。” | 按钮：[移除] (Primary/Critical)、[取消] (Default) |
| **候选译文采用** | `InspectorPanel` 提示 | “已采用候选译文” | 自动同步填入译文编辑框并触发排版预览 |
| **多区域删除确认** | `InspectorPanel` / Shortcut | 按钮：“删除所选区域” (包含数量或快捷键 Delete 标注) | 红色 Hover 高亮，进入撤销栈 |

---

## 6. 控件提示与无障碍文案对照表 (Tooltips & Accessible Names Matrix)

| 控件对象名 | 视觉文字 / 图标 | ToolTip 文案 | Accessible Name |
| :--- | :--- | :--- | :--- |
| `undoButton` | 图标 (撤销) | 撤销 (Ctrl+Z) | 撤销 |
| `redoButton` | 图标 (重做) | 重做 (Ctrl+Y) | 重做 |
| `zoomOutButton` | 图标 (缩小) | 缩小 (-) | 缩小 |
| `zoomInButton` | 图标 (放大) | 放大 (+) | 放大 |
| `fitToPageButton` | 图标 (适应) | 适应页面 (0) | 适应页面 |
| `actualSizeButton` | 图标 (100%) | 实际大小 (1) | 实际大小 |
| `selectButton` | 图标 (选择) | 选择工具 (V) | 选择工具 |
| `panButton` | 图标 (平移) | 平移工具 (H) | 平移工具 |
| `createRegionButton` | 图标 (加号) | 创建文本区域 (R) | 创建文本区域 |
| `previewSelectorButton` | 图标 (图层) | 切换原图/译文预览 (Space) | 切换预览 |
| `smartTranslateButton` | 图标 (智能翻译) | 智能翻译 (Ctrl+T) / 取消处理 | 智能翻译 / 取消处理 |
| `adoptButton` | 采纳建议 | 采纳建议 | 采纳建议 |
| `rejectButton` | 拒绝建议 | 拒绝建议 | 拒绝建议 |
| `pagePaneToggleButton` | 图标 (侧栏) | 页面列表 | 页面列表 |
| `inspectorPaneToggleButton`| 图标 (属性) | 属性面板 | 属性面板 |
| `saveButton` | 图标 (磁盘) | 保存项目 (Ctrl+S) | 保存项目 |
| `inspectorSingleOcr` | OCR | 识别选中区域的文字 | OCR 识别 |
| `inspectorSingleTranslate` | 翻译 | 翻译选中区域的文本 | AI 翻译 |
| `inspectorSingleClear` | 涂抹 | 清除选中区域的背景文字 | 清除背景 |
| `inspectorSingleLayout` | 排版 | 对选中区域重新排版 | 应用排版 |
| `inspectorAdoptCandidate` | 采用候选 | 采用选中的候选译文 | 采用候选译文 |
| `inspectorMultiDelete` | 删除所选区域 | 删除所选的文本区域 (Delete) | 删除所选区域 |
| `m_openButton` (Home) | 打开项目 | 打开已有的 MangaTranslator 项目 | 打开项目 |
| `m_importButton` (Home)| 新建项目/导入 | 从漫画图片或文件夹新建项目 | 新建项目/导入 |
| `m_browseButton` (Import)| 浏览 | 选择项目保存文件夹 | 选择保存位置 |
| `m_submitButton` (Import)| 创建并导入 | 创建项目并导入所选页面 | 创建并导入 |
| `settingsRestoreDefaultsButton` | 恢复默认 | 恢复所有设置项为默认值 | 恢复默认设置 |
| `settingsSaveButton` | 保存设置 | 保存当前配置 (Ctrl+S) | 保存设置 |
| `ocrBrowseButton` | 浏览... | 选择 Baberu OCR 模型所在文件夹 | 选择 OCR 模型目录 |
| `credentialUpdateButton` | 更新密钥 | 输入并配置新的 API 密钥 | 更新 API 密钥 |
| `credentialRemoveButton` | 移除密钥 | 从本地安全存储中移除该密钥 | 移除 API 密钥 |
