# MangaTranslator Phase 5 全量动作清单 (Action Inventory)

## 1. 概述与动作分类 (Overview & Taxonomy)

本清单完整审计了 MangaTranslator 工作台（Phase 5-D）在所有交互界面上暴露的用户操作、快捷键绑定、响应式断点可用性、无障碍名称以及所属的架构调用路径（UDF / ViewModel Intent / Port / UseCase）。

所有动作严格遵循单向数据流 (UDF)，视图 (View) 仅收集意图并派发至 ViewModel，不直接操纵领域对象或后台推理引擎。

---

## 2. 全局与窗口层级动作 (Global Chrome & Navigation Actions)

| 动作标识 (Action ID) | 中文名称 | 快捷键 | 所在控件 | 视觉表现 | 目标 / 路由 (Route / Intent) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `nav.navigate.home` | 导航至首页 | `Alt+1` | `NavigationView` 菜单项 | 图标 + “首页” | `NavigationController::navigateTo("home")` |
| `nav.navigate.editor` | 导航至编辑器 | `Alt+2` | `NavigationView` 菜单项 | 图标 + “编辑器” | `NavigationController::navigateTo("editor")` |
| `nav.navigate.import` | 导航至导入 | `Alt+3` | `NavigationView` 菜单项 | 图标 + “导入” | `NavigationController::navigateTo("import")` |
| `nav.navigate.settings` | 导航至设置 | `Alt+4` / 底部 | `NavigationView` 菜单项 | 齿轮图标 + “设置” | `NavigationController::navigateTo("settings")` |
| `app.toggle_theme` | 切换界面主题 | — | `SettingsPage` 组合框 | 浅色 / 深色 / 跟随系统 | `SettingsViewModel::setTheme()` |
| `app.escape_global` | 全局逐层退出 | `Escape` | 顶层键盘事件过滤器 | 无视觉实体 | IME -> 文本编辑 -> 画布手势 -> Flyout -> 抽屉 -> 导航 |

---

## 3. 首页动作清单 (Home Surface Actions)

| 动作标识 (Action ID) | 中文名称 | 快捷键 | 所在控件 | 触发条件 | 目标方法 / 意图 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `home.open_project` | 打开项目 | `Ctrl+O` | `HomePage` 顶部按钮 | 常驻启用 | `HomeViewModel::requestOpenProjectDialog()` |
| `home.import_new` | 新建项目/导入 | — | `HomePage` 顶部 Accent 按钮 | 常驻启用 | `HomePage::importRequested` -> 导航至 `import` |
| `home.open_recent` | 打开最近项目 | 双击 / 单击 | `HomePage` 卡片网格 | 卡片存在 | `HomeViewModel::openProjectPath(path)` |
| `home.show_in_folder`| 在文件夹中显示 | 右键上下文菜单 | 最近项目卡片右键 | 卡片存在 | `QDesktopServices::openUrl(path)` |
| `home.remove_recent` | 从最近列表移除 | 右键上下文菜单 | 最近项目卡片右键 | 卡片存在 | `HomeViewModel::removeRecent(path)` |
| `home.retry_notice` | 重试打开/恢复 | — | `InfoBar` 操作按钮 | 存在可恢复错误 | `RecoveryPresenter` 绑定恢复路由 |

---

## 4. 导入向导动作清单 (Import Surface Actions)

| 动作标识 (Action ID) | 中文名称 | 快捷键 | 所在控件 | 触发条件 | 目标方法 / 意图 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `import.drop_files` | 拖放图片/文件夹 | 鼠标拖拽 | `FileDropZone` | 导入未进行 | `ImportViewModel::addSourcePaths(paths)` |
| `import.browse_files` | 浏览图片文件 | — | `FileDropZone` 链接 | 导入未进行 | `ImportViewModel::requestBrowseFiles()` |
| `import.browse_folder`| 浏览图片文件夹 | — | `FileDropZone` 链接 | 导入未进行 | `ImportViewModel::requestBrowseFolder()` |
| `import.remove_source`| 移除待导入源项 | — | 待导入列表项右侧图标 | 列表非空 | `ImportViewModel::removeSource(path)` |
| `import.edit_name` | 编辑项目名称 | — | `m_projectNameEdit` | 导入未进行 | `ImportViewModel::setProjectName(text)` |
| `import.browse_save` | 浏览保存位置 | — | `m_browseButton` | 导入未进行 | `ImportViewModel::requestBrowseLocation()` |
| `import.set_direction`| 设置阅读方向 | — | 单选按钮 (RTL / LTR) | 导入未进行 | `ImportViewModel::setReadingDirection(dir)` |
| `import.set_source_lang`| 选择源语言 | — | `m_sourceLangCombo` | 导入未进行 | `ImportViewModel::setSourceLanguageCode(code)` |
| `import.set_target_lang`| 选择目标语言 | — | `m_targetLangCombo` | 导入未进行 | `ImportViewModel::setTargetLanguageCode(code)` |
| `import.submit` | 创建并导入 | `Enter` / 按钮 | `m_submitButton` (Accent) | 表单验证通过 | `ImportViewModel::submit()` |
| `import.cancel` | 取消导入 | — | `m_cancelImportButton` | 导入执行中 | `ImportViewModel::cancelImport()` |

---

## 5. 编辑器动作清单 (Editor Surface Actions)

### 5.1 命令栏动作 (CommandBar Actions)

| 动作标识 (Action ID) | 中文名称 | 快捷键 | 控件类型 | 状态规则 / Enabled 条件 | 目标方法 / 意图 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `editor.undo` | 撤销 | `Ctrl+Z` | `AppBarButton` | `state.canUndo` | `EditorViewModel::undo()` |
| `editor.redo` | 重做 | `Ctrl+Y` | `AppBarButton` | `state.canRedo` | `EditorViewModel::redo()` |
| `editor.zoom_out` | 缩小 | `-` | `AppBarButton` | 有活动页面 | `MangaCanvas::zoomOut()` |
| `editor.zoom_in` | 放大 | `+`, `=` | `AppBarButton` | 有活动页面 | `MangaCanvas::zoomIn()` |
| `editor.fit_page` | 适应页面 | `0` | `AppBarButton` | 有活动页面 | `MangaCanvas::fitToPage()` |
| `editor.actual_size` | 实际大小 | `1` | `AppBarButton` | 有活动页面 | `MangaCanvas::setZoomFactor(1.0)` |
| `editor.tool.select` | 选择工具 | `V` | `AppBarToggleButton` | 有活动页面 | `EditorViewModel::setActiveTool(Select)` |
| `editor.tool.pan` | 平移工具 | `H`, `Space` 按住 | `AppBarToggleButton` | 有活动页面 | `EditorViewModel::setActiveTool(Pan)` |
| `editor.tool.create_region` | 创建文本区域 | `R` | `AppBarToggleButton` | 有活动页面 | `EditorViewModel::setActiveTool(CreateRegion)` |
| `editor.toggle_preview` | 切换原图/译文预览 | `Space` 单击 | `AppBarButton` | 有活动页面 | `EditorViewModel::setPreviewMode()` |
| `editor.smart_translate` | 智能翻译 / 取消 | `Ctrl+T` | `Button` (Accent) | 运行中可取消；未运行时需有区域 | `EditorProcessingController::smartTranslatePage()` |
| `editor.adopt_proposal`| 采纳建议 | — | `Button` (Accent) | `hasProposal=true` | `EditorProcessingController::adoptProposal()` |
| `editor.reject_proposal`| 拒绝建议 | — | `Button` (Subtle) | `hasProposal=true` | `EditorProcessingController::rejectProposal()` |
| `editor.toggle_page_pane`| 切换页面列表 | — | `AppBarToggleButton` | 常驻可用 | `EditorPage::togglePagePane()` |
| `editor.toggle_inspector`| 切换属性面板 | — | `AppBarToggleButton` | 常驻可用 | `EditorPage::toggleInspectorPane()` |
| `editor.save_project` | 保存项目 | `Ctrl+S` | `AppBarButton` | `isDirty && !isSaving` | `EditorViewModel::saveProject()` |

### 5.2 画布直接交互动作 (MangaCanvas Direct Actions)

| 动作标识 (Action ID) | 中文名称 | 快捷键 / 手势 | 作用域 | 目标方法 / 意图 |
| :--- | :--- | :--- | :--- | :--- |
| `canvas.select_region` | 单击选择区域 | 鼠标左键单击 | Select 工具 | `MangaCanvas::regionSelectionRequested(Replace)` |
| `canvas.toggle_select` | 切换区域选择 | `Ctrl+单击` | Select 工具 | `MangaCanvas::regionSelectionRequested(Toggle)` |
| `canvas.range_select` | 范围选择区域 | `Shift+单击` | Select 工具 | `MangaCanvas::regionSelectionRequested(Range)` |
| `canvas.cycle_tab` | Tab 循环切换选中区域 | `Tab` / `Shift+Tab` | Select 工具 | `MangaCanvas::regionSelectionRequested` (下一个/上一个) |
| `canvas.move_region_drag`| 鼠标拖动移动区域 | 鼠标拖拽区域内部 | Select 工具 | `MangaCanvas::regionGeometryCommitted(Move)` |
| `canvas.move_region_keys`| 键盘方向键移动区域 | `Up/Down/Left/Right` | Select 工具 | 每次 1.0 px 步进移动并提交 |
| `canvas.resize_drag` | 鼠标拖动手柄缩放 | 拖拽 8 向手柄 | Select 工具 (已选中) | `MangaCanvas::regionGeometryCommitted(Resize)` |
| `canvas.resize_keys` | 键盘方向键缩放区域 | `Shift+方向键` | Select 工具 (已选中) | 每次 1.0 px 步进缩放并提交 |
| `canvas.create_drag` | 拖拽绘制新区域 | 鼠标左键拖拽 | CreateRegion 工具 | `MangaCanvas::regionCreateCommitted(geometry)` |
| `canvas.delete_region` | 删除选中区域 | `Delete` | 区域选中状态 | `MangaCanvas::regionDeleteRequested(id)` |
| `canvas.cancel_gesture`| 取消正在进行的手势 | `Escape` | 手势拖拽进行中 | `MangaCanvas::cancelGesture()` (恢复起始位置) |
| `canvas.pan_drag` | 平移画布 | 鼠标中键 / Space+拖拽 | 工作区 | 视口平移滚动 |
| `canvas.zoom_wheel` | 滚轮缩放画布 | `Ctrl+鼠标滚轮` | 工作区 | `MangaCanvas::applyZoom()` |

### 5.3 属性面板动作 (InspectorPanel Actions)

| 动作标识 (Action ID) | 中文名称 | 控件类型 | 作用域 | 目标方法 / 意图 |
| :--- | :--- | :--- | :--- | :--- |
| `inspector.edit_source` | 编辑识别原文 | `TextEdit` | 单选区域 | `InspectorPanel::emitFieldEdit(SourceText)` |
| `inspector.edit_trans` | 编辑译文 | `TextEdit` | 单选区域 | `InspectorPanel::emitFieldEdit(ManualTranslation)` |
| `inspector.adopt_candidate`| 采用候选译文 | 双击 / 按钮 | 单选区域 | `InspectorPanel::candidateAdoptionRequested` |
| `inspector.single_ocr` | 单区域 OCR 识别 | `Button` | 单选区域 | `InspectorPanel::ocrRequested` |
| `inspector.single_trans`| 单区域 AI 翻译 | `Button` | 单选区域 | `InspectorPanel::translateRequested` |
| `inspector.single_clear`| 单区域背景涂抹 | `Button` | 单选区域 | `InspectorPanel::clearRequested` |
| `inspector.single_layout`| 单区域文字排版 | `Button` | 单选区域 | `InspectorPanel::layoutRequested` |
| `inspector.edit_font` | 修改字体 | `ComboBox` | 单选 / 多选 | `InspectorPanel::emitFieldEdit(FontFamily)` |
| `inspector.edit_fontsize`| 修改字号 (pt) | `NumberBox` | 单选 / 多选 | `InspectorPanel::emitFieldEdit(FontSizePt)` |
| `inspector.edit_color` | 修改文字颜色 | `ColorSwatchButton` / Hex | 单选 / 多选 | `InspectorPanel::emitFieldEdit(TextColorHex)` |
| `inspector.edit_stroke` | 修改描边颜色与宽度 | `ColorSwatchButton` / Hex | 单选 / 多选 | `InspectorPanel::emitFieldEdit(StrokeColorHex / Width)` |
| `inspector.edit_align` | 修改对齐方式 | `ComboBox` | 单选 / 多选 | `InspectorPanel::emitFieldEdit(Horizontal / Vertical Alignment)` |
| `inspector.edit_orient` | 修改排版方向 (横/竖) | `ComboBox` | 单选 / 多选 | `InspectorPanel::emitFieldEdit(Orientation)` |
| `inspector.multi_ocr` | 批量 OCR 识别 | `Button` | 多选区域 | `InspectorPanel::ocrRequested(batch)` |
| `inspector.multi_trans`| 批量 AI 翻译 | `Button` | 多选区域 | `InspectorPanel::translateRequested(batch)` |
| `inspector.multi_clear`| 批量背景涂抹 | `Button` | 多选区域 | `InspectorPanel::clearRequested(batch)` |
| `inspector.multi_layout`| 批量应用排版 | `Button` | 多选区域 | `InspectorPanel::layoutRequested(batch)` |
| `inspector.multi_delete`| 批量删除选中区域 | `Button` (Critical) | 多选区域 | `InspectorPanel::deleteRegionsRequested(batch)` |

### 5.4 页面列表动作 (PageThumbnailList Actions)

| 动作标识 (Action ID) | 中文名称 | 快捷键 / 操作 | 目标方法 / 意图 |
| :--- | :--- | :--- | :--- |
| `pagelist.select_page` | 切换当前编辑页 | 单击缩略图行 | `EditorViewModel::selectPage(pageId)` |
| `pagelist.page_up` | 上一页 | `PageUp` | `EditorViewModel::selectAdjacentPage(-1)` |
| `pagelist.page_down` | 下一页 | `PageDown` | `EditorViewModel::selectAdjacentPage(1)` |

---

## 6. 设置页面动作清单 (Settings Surface Actions)

| 动作标识 (Action ID) | 中文名称 | 快捷键 | 控件类型 | 目标方法 / 意图 |
| :--- | :--- | :--- | :--- | :--- |
| `settings.restore_project`| 开机恢复上次项目 | — | `ToggleSwitch` | `SettingsViewModel::setRestoreLastProject(on)` |
| `settings.recent_limit` | 设置最近项目数量上限 | — | `NumberBox` (1–50) | `SettingsViewModel::setRecentProjectLimit(val)` |
| `settings.ocr_model_dir` | 输入/浏览 OCR 模型目录 | — | `LineEdit` + `m_ocrBrowseButton` | `SettingsViewModel::setOcrModelDirectory(path)` |
| `settings.ocr_lang` | 设置默认 OCR 语言 | — | `ComboBox` | `SettingsViewModel::setOcrLanguage(lang)` |
| `settings.llm_provider` | 选择 AI 翻译服务商 | — | `ComboBox` | `SettingsViewModel::setProviderId(id)` |
| `settings.llm_endpoint` | 设置 API 端点基础 URL | — | `LineEdit` | `SettingsViewModel::setEndpoint(url)` |
| `settings.llm_cred_save`| 保存 API 密钥 | — | `m_credentialSaveButton` | `SettingsViewModel::setCredential(secret)` |
| `settings.llm_cred_del` | 移除 API 密钥 | — | `m_credentialRemoveButton` | `SettingsViewModel::removeCredential()` |
| `settings.llm_model` | 设置默认模型标识符 | — | `LineEdit` | `SettingsViewModel::setModelName(name)` |
| `settings.llm_timeout` | 设置请求超时时长 | — | `NumberBox` (1–300s) | `SettingsViewModel::setTimeoutSeconds(sec)` |
| `settings.llm_retries` | 设置失败重试次数 | — | `NumberBox` (0–10) | `SettingsViewModel::setRetryCount(count)` |
| `settings.theme` | 设置应用主题模式 | — | `ComboBox` | `SettingsViewModel::setTheme(theme)` |
| `settings.density` | 设置界面显示密度 | — | `ComboBox` | `SettingsViewModel::setDensity(density)` |
| `settings.reduce_motion`| 开启/关闭减少动效 | — | `ToggleSwitch` | `SettingsViewModel::setReduceMotion(on)` |
| `settings.canvas_bg` | 自定义画布背景色 | — | `LineEdit` (Hex) | `SettingsViewModel::setCanvasBackground(hex)` |
| `settings.restore_defaults`| 恢复所有默认设置 | — | `Button` (Subtle) | `SettingsViewModel::restoreDefaults()` |
| `settings.save` | 保存并应用设置 | `Ctrl+S` | `Button` (Accent) | `SettingsViewModel::saveSettings()` |

---

## 7. 全局状态任务栏动作清单 (AppStatusTaskStrip Actions)

| 动作标识 (Action ID) | 中文名称 | 操作手势 | 触发条件 | 目标方法 / 意图 |
| :--- | :--- | :--- | :--- | :--- |
| `taskstrip.focus_active` | 点击定位活动任务页面 | 单击活动任务摘要热区 | 有活动任务 | `AppStatusTaskStrip::operationActivated(id, proj, page)` |
| `taskstrip.open_flyout` | 打开后台任务详情列表 | 单击右侧列表菜单按钮 | 常驻可用 | `AppStatusTaskStrip::openTaskFlyout()` |
| `taskstrip.cancel_op` | 取消指定后台任务 | 点击 Flyout 内任务行取消图标 | 任务处于 Queued/Running | `AppStatusTaskStrip::cancelRequested(id)` |
| `taskstrip.save_detail` | 查看保存失败详情 | 单击保存失败警示图标 | `SaveFailed` 状态 | `AppStatusTaskStrip::saveFailureDetailsRequested()` |

---

## 8. 响应式断点与无障碍矩阵 (Breakpoints & Accessibility Matrix)

| 动作名称 | 快捷键 | Wide (>=1440) | Standard (1200-1439) | Compact (1008-1199) | Narrow (720-1007) | Minimum (<720) | Flat Height (<560) | Accessible Name | 归属与所有权 (Ownership) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- | :--- |
| **撤销 / 重做** | `Ctrl+Z` / `Ctrl+Y` | ✅ 直接可见 | ✅ 直接可见 | ✅ 直接可见 | ✅ 直接可见 | ✅ 直接可见 | ✅ 单行紧凑 | `撤销` / `重做` | `EditorViewModel` |
| **缩放 / 适应** | `+` `-` `0` `1` | ✅ 完整显示 | ✅ 完整显示 | ✅ 完整显示 | ✅ 完整显示 | ✅ 图标显示 | ✅ 单行紧凑 | `缩小` `放大` `适应页面` | `MangaCanvas` |
| **画布工具选择** | `V` `H` `R` | ✅ 三项常驻 | ✅ 三项常驻 | ✅ 三项常驻 | ✅ 三项常驻 | ✅ 三项常驻 | ✅ 单行紧凑 | `选择工具` `平移工具` `创建文本区域` | `EditorViewModel` |
| **智能翻译** | `Ctrl+T` | ✅ Accent 显示 | ✅ Accent 显示 | ✅ Accent 显示 | ✅ Accent 显示 | ✅ Accent 紧凑 | ✅ 保证可见 | `智能翻译` / `取消处理` | `ProcessingController` |
| **页面列表栏** | `F6` | ✅ 232px 常驻 | ✅ 220px 常驻 | ✅ 96px 紧凑 | 🔲 互斥抽屉 | 🔲 互斥抽屉 | ✅ 纵向自适应 | `页面列表` | `EditorPage` |
| **属性 Inspector** | `F6` | ✅ 352px 常驻 | ✅ 328px 常驻 | ✅ 320px 常驻 | 🔲 互斥抽屉 | 🔲 互斥抽屉 (85-100%) | ✅ 内部自滚动 | `属性面板` | `InspectorPanel` |
| **保存项目** | `Ctrl+S` | ✅ 右侧常驻 | ✅ 右侧常驻 | ✅ 右侧常驻 | ✅ 右侧常驻 | ✅ 右侧常驻 | ✅ 保证可见 | `保存项目` | `EditorViewModel` |
| **任务栏摘要** | — | ✅ 3 栏完整 | ✅ 2 栏 + 紧凑 | ✅ 2 栏 + 紧凑 | 🔲 仅最高优先级 | 🔲 仅最高优先级 | ✅ 28-32px 紧凑 | `应用状态与后台任务栏` | `AppStatusTaskStrip` |
