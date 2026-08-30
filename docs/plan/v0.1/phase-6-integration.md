# P6：纵向切片集成

> 状态：Planned
> 前置阶段：P3、P4、P5 的退出条件均已满足
> 输出：使用真实项目仓储、图片加载、处理适配器和生产 Composition Root 的端到端 V0.1 主线

## 1. 阶段边界

P6 只连接和验证已经属于 V0.1 的能力。发现 Port、UseCase、Adapter、Domain 或 UI 契约缺口时，回到责任层做最小修正，再从受影响场景重新集成；不得把业务判断、路径规则或线程补丁塞进 `ApplicationContext`。

P6 不交付自动文字检测、CBZ/PDF、批处理页面、术语库、复杂 Inpaint、流式翻译或 Undo 历史持久化。V0.1 智能处理从用户已有的矩形 Region 开始。

开始 P6 前必须确认：

- P3 的项目锁、原子保存、恢复、导入暂存和相对路径规则可用。
- P4 的 OCR、翻译、清除、排版、导出 Adapter 及稳定错误映射可由 Fake/固定样本验证。
- P5 的 EditorViewModel、项目级 UndoStack、generation、画布和错误恢复入口已经落地。
- 生产 UI 只显示可完成的 V0.1 路由和命令。

## 2. Composition Root 设计

P5 已建立 `Application` 与 `ApplicationContext` 骨架。P6 扩展现有对象图，不创建第二个 Context、Service Locator 或全局单例。

### 2.1 生命周期分层

| 生命周期 | 所有者 | 内容 |
| --- | --- | --- |
| Process | `ApplicationContext` | 日志、应用设置、Credential Store、I/O 协调器、任务执行器、网络传输、Repository、Importer、处理 Adapter、UseCase factory/长期 UseCase |
| Project session | 项目会话控制器 | 当前 `ComicProject`、项目 lease、projectSessionGeneration、EditorViewModel、UndoStack、活动 operation 绑定 |
| View | 页面/ViewModel | viewGeneration、QFutureWatcher、当前页图像、QPixmap 与渲染缓存、临时手势 |
| Operation | OperationRegistry/执行器 | operationId、stop source、目标 ProjectId/PageId/RegionId、进度和完成状态 |

Process 依赖不得持有 Project 或 View 指针。项目关闭销毁整个 Project session；页面切换只更新 View 生命周期，不销毁项目级后台任务。

### 2.2 构造顺序

生产对象按以下顺序构造并通过构造函数注入：

1. 安全日志、已校验的 `IAppSettingsStore` 和主题配置。
2. `ProjectIoCoordinator`、Repository、Importer、PageImageLoader、DerivedAsset/Thumbnail Store、Exporter。
3. Credential Store、Provider 配置快照、HTTP transport 和 Translator factory。
4. 有界执行器、OCR Session、Inpainter、TextLayouter 等处理 Adapter。
5. OperationRegistry 和 Application UseCases。
6. 页面级 ViewModel、Navigation、MainWindow 与当前 Project session。

OCR 模型缺失、未配置 API Key 或网络离线不是应用启动失败；对应能力进入可恢复不可用状态，人工编辑、保存和导出继续可用。项目仓储或设置初始化失败若无法安全降级，必须在显示半初始化编辑器前给出安全错误并退出。

### 2.3 线程归属

- Window、ViewModel、UndoStack、QFutureWatcher、QPixmap 和所有 Widget 只在 GUI 线程使用。
- `QNetworkAccessManager` 固定在拥有事件循环的网络线程，并在该线程销毁。
- ONNX Session 和有界执行器按 P4 并发契约复用；不得因每个 Region 新建 Session。
- 工作线程只传递值 DTO、`QImage` 和 `Result<T>`，不访问 View、Domain 指针或 UndoStack。
- Domain 的最终采用与 Undo 命令 push 在 GUI 线程串行完成。

### 2.4 确定性关闭

关闭窗口、切换项目和进程退出使用同一套会话关闭协议：

1. 关闭门停止接受新操作，并结束当前文本编辑/IME commit 边界。
2. 若项目 Dirty，完成保存、放弃或取消关闭的用户决策；取消关闭不得继续 teardown。
3. 增加 projectSessionGeneration，向该会话所有 operation 请求停止，并让 UI 显示“正在停止”。
4. 等待 Future/网络回复/执行器进入完成或已取消状态；测试不得依赖无界 sleep。
5. 销毁 View 和 QFutureWatcher，销毁项目 UndoStack，再关闭 Repository project lease。
6. 保存非敏感窗口/Pane 设置，停止网络线程和执行器。
7. 按构造逆序销毁 Adapter、Store 和基础设施。

不得在线程仍可能回调已销毁对象时释放依赖。不能中断的单次 ONNX 推理允许完成当前调用，但结果因 generation 失效不得采用；P7 记录其实际最坏退出时延。

## 3. V0.1 纵向场景

每个场景使用临时项目目录和已审查许可证的固定图片，断言 Domain、磁盘项目和导出结果，而不只断言界面显示“成功”。

### P6-E01 纯人工离线主线

创建项目 → 导入单图和图片文件夹 → 切页 → 创建/移动/缩放 Region → 输入原文/译文 → 调整基础排版 → 清除 → Undo/Redo → 保存 → 关闭 → 重开 → 导出 PNG/JPEG。

此场景不提供模型、API Key 或网络，必须完整通过，是 V0.1 的最低可靠基线。

### P6-E02 OCR 主线

用固定模型验收包和参考图片识别已有 Region，预览候选后由用户采用；断言未采用结果不修改 Domain，采用后产生一条可撤销命令，人工原文不会被迟到结果覆盖。

真实模型推理由独立的 `p4-model` 作业负责，普通 P6 CI 使用确定性 Adapter/Fake，避免下载大模型或把模型缺失当作跳过成功。

### P6-E03 翻译主线

人工原文 → 本地 mock HTTP server → OpenAI-compatible 协议解析 → 按 RegionId 对齐候选 → 用户采用 → 保存重开。

CI 不访问公共 Provider、不使用真实凭据或付费 API。真实 Provider 只作为显式人工验收边界，结果不得成为确定性发布门的唯一证据。

### P6-E04 完整处理主线

已有 Region → OCR → 翻译 → Prepare Clear → push Undo Command → 排版 → 保存 → 导出。断言 UI 预览和 ExportPageSnapshot 使用同一 rendering 结果，透明度、文字位置和图层顺序在规定容差内一致。

### P6-E05 页面范围导出

分别导出当前页、显式选中页和全部页面；保持项目页序，冲突文件追加序号且不覆盖旧文件。单页失败返回明确的页面级结果，不删除其他已成功输出，也不改变项目内容。

## 4. 状态、保存与冲突

- 页面快速切换只使旧 `viewGeneration` 图像结果失效；OCR/翻译等项目任务继续绑定原 PageId。
- 同一目标新操作替换旧 operationId；不同页面操作可以并存并在任务条被发现。
- 保存捕获 revision。Repository 返回 `savedRevision` 后，只在 `markCleanIfRevision(savedRevision)` 成功时设置 Undo clean point。
- 保存期间的新编辑保持 Dirty；保存成功不清空 Undo/Redo。
- 关闭并重开后 Domain 内容一致，Undo 历史为空，因为 V0.1 不持久化命令栈。
- manualText/manual geometry 优先于自动结果；冲突结果进入候选或被拒绝，不静默覆盖。
- 清除的 prepare 阶段先原子提交不可变资产；Undo/Redo 只切引用，不执行 I/O。

## 5. 故障与恢复矩阵

| 故障 | 必须保持 | 恢复入口 |
| --- | --- | --- |
| 图片缺失/损坏 | 当前项目和上一页选择；画布不得残留旧页图像 | 重新定位或重试加载 |
| OCR 模型缺失/损坏 | 人工原文编辑、保存、导出 | 打开模型设置/选择模型目录 |
| 凭据缺失、401 | 人工译文和既有候选 | 配置/替换 API Key |
| 429/超时/离线 | 已采用结果和人工修改 | 按 P4 策略重试或稍后重试 |
| 清除/排版失败 | 原图、已有派生层和 Domain 引用 | 修正输入后重试，不写半成品 |
| 保存失败/磁盘不足 | 最后有效项目文件与 Dirty 状态 | 释放空间、选择其他位置、再次保存 |
| 导出失败/只读目录 | 项目和既有导出文件 | 选择其他目录后重试 |
| 项目切换/应用退出 | 新项目不接收旧结果 | 取消旧会话并拒绝迟到完成 |

RecoveryAction 必须落到 P5 已定义的真实 UI 入口；`diagnostic` 只进入安全日志。

## 6. 集成测试结构

### 6.1 测试目标

- `manga_tests_integration`：真实 filesystem/Data/Rendering/Service 边界与 mock network，使用临时目录。
- `manga_tests_app_smoke`：offscreen 构造真实窗口树、进入事件循环并正常关闭。
- `manga_tests_ui` 相关目标：ViewModel 状态序列、generation、Undo、焦点和错误入口。
- `p4-model`：固定 Hash 的真实 OCR 模型推理，不并入普通离线测试。

当前 `tests/integration/integration_init_test.cpp` 仍是占位测试。P6 必须以按场景拆分的真实测试文件替换它；目标不得仅因成功链接而通过。

每个场景固定真实/替身边界，避免“全 Fake 不算集成”或“所有外部依赖都真实而不可重复”：

| 场景 | 必须使用真实实现 | 允许的确定性替身 |
| --- | --- | --- |
| P6-E01 | Repository、Importer、PageImageLoader、Rendering、Clear/Layout、Exporter | 无 OCR、无 Translator |
| P6-E02 | Repository、Apply UseCase、operation/generation 路径 | 普通 CI 使用 Fake OCR；`p4-model` 单独覆盖真实 ONNX Adapter |
| P6-E03 | `QtHttpTransport`、ChatCompletions 协议、RetryPolicy、Repository | loopback server、Fake Credential Store、固定 clock |
| P6-E04 | Repository、DerivedAssetStore、Inpainter、Rendering、Layouter、Exporter | OCR/Translator 返回固定 DTO，真实 Adapter 由 E02/E03 各自覆盖 |
| P6-E05 | Repository、Rendering snapshot、FileSystemComicExporter | 仅文件选择器/目标目录由测试注入 |

### 6.2 测试约束

- 所有异步等待使用 signal/event-loop 驱动和明确 timeout，不使用无条件 sleep。
- ID、clock、HTTP response、文件目录和图片 fixture 可控；不读取用户 QSettings、Credential Manager、模型缓存或文档。
- 固定图片必须有来源和许可证记录；禁止用户漫画或来源不明素材进入仓库。
- 测试失败保留临时日志/差异摘要，但不上传完整漫画内容、密钥或绝对用户路径。
- 像素和浮点比较使用文档化容差；JPEG 不做逐字节相等断言。

### 6.3 必测集成用例

- P6-E01 至 P6-E05。
- 保存竞争、Save As、`.bak` 恢复和损坏项目拒绝。
- 快速切页、View 销毁、项目切换、应用退出、取消和迟到结果。
- 人工文本/几何与 OCR/翻译/排版结果冲突。
- 原图哈希在完整主线前后不变。
- 窗口关闭取消时不销毁会话；确认关闭后无回调访问已销毁对象。

## 7. 完成证据

P6 记录：测试命令和结果、场景与测试名映射、固定 fixture 清单、失败恢复截图/日志摘要、仍需 P7 在真实系统验证的边界。性能 wall-clock、视觉最终批准、发布包部署和真实 Windows 版本兼容属于 P7，不在 P6 用开发机一次运行伪造结论。

## 8. 实施清单

### Composition 与会话

- [ ] 在现有 `ApplicationContext` 中接齐 P3–P5 生产 Adapter、UseCase 和 ViewModel，不创建第二套对象图。
- [ ] 实现 Process/Project session/View/Operation 四级生命周期和可注入测试构造。
- [ ] 实现停止接单、Dirty 决策、请求取消、Watcher/UndoStack/lease 销毁、网络线程与执行器停止的确定性关闭顺序。
- [ ] 验证模型、Credential、网络缺失不会阻止人工离线主线启动。

### 主线与故障

- [ ] 完成 P6-E01 至 P6-E05，并按 §6.1 固定真实实现与替身边界。
- [ ] 覆盖保存竞争、Save As、恢复、取消、项目切换、迟到结果和人工修改冲突。
- [ ] 将每个 P4 `RecoveryAction` 接到 P5 的实际入口，确认失败不写半成品或假成功。
- [ ] 验证完整主线前后源图 SHA-256 不变，保存重开内容一致，导出不覆盖。

### 自动化与证据

- [ ] 删除 integration placeholder，以按场景拆分的真实测试替换。
- [ ] 保持普通 CI 离线确定性；真实 OCR 继续由固定模型 `p4-model` 作业负责。
- [ ] 记录场景到测试名映射、fixture 来源/许可证、命令结果和 P7 未验证边界。

## 9. 退出条件

- P6-E01 至 P6-E05 在支持配置通过，且纯人工离线主线不依赖模型或网络。
- 生产 `ApplicationContext` 只构造一套对象图，项目会话与 View 生命周期可独立销毁。
- 保存重开后页面、Region、文字、排版、派生引用和人工修改一致；源图哈希不变。
- UI 预览与导出在冻结容差内一致，导出不覆盖既有文件。
- 取消、失败、项目切换和进程退出均不产生假成功、错误写回或 use-after-free。
- 自动能力不可用时，人工编辑、保存和导出仍成立。

## 10. 注意事项

- P6 以修复集成缺口为主，不新增 V0.2 功能。
- 不用演示脚本、真实付费调用或占位测试代替可重复验收。
- 发现跨层依赖倒置时回到对应层修正；Composition Root 只负责构造、注入和关闭。
