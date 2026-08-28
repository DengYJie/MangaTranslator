# P2：Application 契约与 UseCase

> 状态：Planned  
> 前置阶段：P1  
> 输出：可由 Fake Port 完整驱动的 V0.1 业务流程

## 目标

固定应用能力、错误、异步和结果采用边界，使数据层、处理适配器和 UI 能独立开发。

## TODO

### Ports 与公共结果

- [ ] 定义 `IProjectRepository`、`IComicImporter`、`IOcrEngine`、`ITranslator`、`IInpainter`、`ITextLayouter`、`IComicExporter`。
- [ ] 定义结构化错误类别、用户安全摘要和可恢复建议。
- [ ] 定义取消令牌、进度单位、不可变输入快照和请求 generation。
- [ ] 定义任务身份中的 `projectId`、`pageId`、Region ID 集合和操作 ID。
- [ ] 定义 Port DTO 与 Domain 的显式映射，禁止第三方类型穿透接口。

### UseCases

- [ ] 项目：创建、打开、保存、另存、关闭和恢复。
- [ ] 导入：图片/文件夹选择、校验、排序和加入项目。
- [ ] Region：创建、删除、移动、缩放和顺序维护。
- [ ] 文本：编辑原文、译文和候选采用。
- [ ] 处理：单 Region/单页 OCR、单页翻译、清除和排版。
- [ ] 导出：单页、选中页和整个项目。
- [ ] 定义提交异步结果前的项目、页面、generation、输入版本和人工优先检查。

### 翻译协议

- [ ] 定义单页请求、稳定 Region ID、语言、阅读方向和有效项目术语输入。
- [ ] 定义非流式结构化响应 Schema。
- [ ] 缺失 ID 只使对应 Region 失败；重复 ID 使整次响应失败；未知 ID 忽略并形成安全诊断。
- [ ] 禁止按响应数组位置匹配 Region。

## 测试

- [ ] 每个 UseCase 的成功、输入错误、Port 失败、取消和过期结果。
- [ ] 关闭项目、切换页面及 generation 变化后的迟到结果。
- [ ] 人工译文、几何和排版不被后台结果覆盖。
- [ ] 翻译缺失、重复、未知 Region ID 和部分成功。
- [ ] Fake Port 不访问文件系统、网络、凭据或 GUI。
- [ ] 取消传播：`request_stop` 后任务在下一检查点返回 `Cancelled`，不提交部分结果。
- [ ] `Cancelled` 与 `Failed` 的结果区分；取消不进入错误路径和恢复建议。
- [ ] 五项结果提交检查各自失败时的拒绝行为，含 `contentRevision` 变化。
- [ ] 确定进度与不确定进度（`total = 0`）两种回调形态。
- [ ] 非幂等操作（翻译）超时后不自动重试；限流按服务端指示有限重试。
- [ ] 第三方异常在适配器内映射为 `ApplicationError`，不穿越 Port 边界。
- [ ] Fake Port 以已完成或延迟完成的 `QFuture` 驱动测试，不需要真实线程。
- [ ] sourceText 与请求不符时对应 Region 失败，其余 Region 正常采用。
- [ ] 后台结果未 Apply 前不写 Domain；Apply 后进入 Undo 且可撤销。
- [ ] 导入与导出文件名冲突追加序号，绝不覆盖既有文件。

## 退出条件

- P3、P4、P5 只依赖稳定 Port 和 Domain 即可工作。
- 所有长操作都有可取消、可报告进度、可拒绝过期结果的契约。
- Application 不依赖 QWidget、FluentQt、HTTP、ONNX Runtime 或具体存储实现。

## 注意事项

- UseCase 负责业务协调，不承担页面状态渲染或底层格式解析。
- 不为单一调用创建跨层 Manager；优先保留明确 Port 与 UseCase。
- 保存不是 Undo 命令，异步结果只有用户明确采用时才进入 Undo 历史。

## 设计规格参考

### 错误与 Result

```cpp
template<class T>
using Result = std::expected<T, ApplicationError>;

enum class ErrorCategory {
    Input,
    Project,
    Model,
    Network,
    Protocol,
    Inference,
    Persistence,
    Export,
    Cancelled,
    Internal
};

struct ApplicationError {
    ErrorCategory category;
    QString code;           // 稳定错误码，如 "network.timeout"
    QString safeMessage;    // 可展示给用户的安全摘要
    QString diagnostic;     // 日志诊断，不含密钥与完整用户内容
    RecoveryAction recovery;
    bool retryable = false;
};
```

- 所有 Port 共用一个 `ApplicationError`，各 Port 不发明独立错误枚举。
- `ErrorCategory::Cancelled` 表达取消：取消不是失败，不进入错误恢复建议，`retryable = false`。
- `RecoveryAction` 是 Application 定义的有限枚举（如 `None`、`Retry`、`OpenSettings`、`LocateModel`、`ChoosePath`），只描述可执行动作，不生成 UI 文案。
- 第三方异常（ONNX、HTTP、JSON、文件系统）在适配器内捕获并映射为 `ApplicationError`，不得穿越 Port 边界。

### Port 异步契约形态

```cpp
virtual QFuture<Result<OcrResponse>> recognize(const OcrRequest& request,
                                               const OperationContext& context) = 0;
```

- 所有可能耗时或失败的 Port 操作统一返回 `QFuture<Result<T>>`；纯内存同步查询不强制。
- `QFuture` / `QPromise` / `QFutureWatcher` 属 QtCore；不引入 QtConcurrent，不使用 `std::future`。
- Port 接口不继承 `QObject`、不定义 signal；`QObject`、`QNetworkAccessManager` 等只出现在适配器内部。
- Port 方法立即返回，绝不阻塞调用线程；适配器把工作提交给有界执行器（P4）并用 `QPromise` 桥接 `QFuture`。
- 不使用 `QFuture::cancel()` 作为取消通道；取消统一走 `stop_token`，避免双机制。
- GUI 线程禁止调用 `QFuture::result()` / `waitForFinished()`；结果与进度经 GUI 线程持有的 `QFutureWatcher` 投递。
- 不在签名中堆 `successCallback` / `errorCallback` / `progressCallback`。
- Fake Port 返回已完成或延迟完成的 `QFuture`，测试不需要真实线程与事件循环。

### 取消、进度与请求 Generation

#### OperationContext

```cpp
struct OperationContext {
    OperationId operationId;   // UUID v4
    quint64 generation;
    std::stop_token stopToken;
};
```

- 取消使用标准 `std::stop_source` / `std::stop_token`，不包装、不引入 Qt 取消句柄。
- UseCase 为每次操作创建 `std::stop_source`，并维护在途操作表（operationId → 请求身份、generation 与输入版本）；取消入口（如 `cancelOcr(operationId)`）查表转发 `request_stop()`。
- 合作式取消，检查点约定：
  - OCR：每个 Region 处理前后。
  - 翻译：发送请求前；等待期间中止底层请求（如 `QNetworkReply::abort()`）。
  - 导入/导出：每个文件或页面处理前。
- 关闭页面、项目或应用时执行双保险：`request_stop()` + generation 递增。
- 取消结果为 `ErrorCategory::Cancelled`，不提交部分结果；翻译等已计费调用在 UI 说明无法保证服务端停止执行或计费。

#### operationId 与 generation 的分工

- `operationId`（UUID）用于日志、Trace 与流水线关联；一个工作流（OCR → 翻译 → 排版）共用同一 `operationId`。
- `generation`（quint64）只负责过期判断，由 ViewModel 维护：`beginRequest()` 递增并返回当前值、`isRequestCurrent()` 校验、`invalidateRequests()` 使全部在途结果过期。
- 两者不相等、不互换：切换页面只递增 generation，不产生新 `operationId`。
- 触发 invalidate 的时机：切换页面、切换或关闭项目、应用退出、用户对同一目标发起新操作替代旧操作。

#### 提交结果前的五项检查

UseCase 在提交异步结果前执行，任一失败即丢弃结果并按过期处理：

1. `projectId` 一致。
2. `pageId` 一致。
3. generation 一致。
4. 输入版本一致：涉及 Region 的 `contentRevision` 未变化。
5. 人工优先：`manualText` 有值时后台结果不得覆盖。

被拒绝的结果不写 Domain、不进 Undo 历史、不触发 `stateChanged`。

#### 进度

```cpp
struct Progress {
    OperationStage stage;   // scoped enum：Ocr / Translate / Import / Export ...
    qint64 completed;
    qint64 total;           // 0 表示不确定进度
};
```

- 百分比是派生值，不进入契约。
- 进度单位固定：OCR 按 Region、导入按文件、导出按页面；翻译按页面请求，`total = 0`，用递增活动计数表达进行中。
- 进度通过 `QPromise` 进度通道投递：范围映射 `total`、值映射 `completed`、`setProgressText` 携带 stage；不确定进度不设范围。
- 进度只作观察，不作为取消或超时依据；频率由适配器节流，UI 不依赖逐次回调。

#### 输入快照与图像载体

- 请求发出时复制所需 Region 几何、文本与图像，形成不可变快照；Worker 不回读 Domain、不持有 Domain 引用。
- 图像载荷以 `QImage` 值类型穿越 Port，且必须是已解码的工作图（RGBA8、straight alpha、sRGB、EXIF 已应用）。
- 不采用 `AssetRef + rect` 让适配器自行加载：工作图是导入管线在内存中的规范化产物，适配器重新解码会复制规范化逻辑并制造与 Domain 不一致的风险。适配器只负责计算，Region 裁剪在适配器内完成。

#### 超时与重试

- 超时是每操作可配置项；翻译的 endpoint、model、timeout 来自设置，不写死在适配器内。
- 自动重试仅限可安全重复的失败：连接建立失败、DNS 失败、429 限流（遵循服务端退避指示）。
- 请求超时后无法确定服务端是否已处理；LLM 翻译默认不自动重试，由用户手动重试。
- 自动重试使用有限次数指数退避；次数与上限随操作类型固定并纳入契约测试。

### Port DTO 与映射

```cpp
struct OcrRequest {
    PageId pageId;
    RegionId regionId;
    QImage image;            // 已解码工作图
    RectGeometry rect;
    LanguageCode language;
};

struct OcrResponse {
    RegionId regionId;
    QString text;
    double confidence;
};
```

- 明确的请求/响应 DTO：`OcrRequest/OcrResponse`、`TranslationRequest/TranslationResponse`、`ExportRequest/ExportResponse` 等。
- Port DTO 禁止：`cv::Mat`、`Ort::Value`、`QJsonObject`、HTTP 响应、数据库 Row、`QGraphicsItem`。
- `QImage` 是唯一图像载体（见输入快照）。
- DTO ↔ Domain 映射放在 `application/mapping/`，不塞进 Domain，也不散落在适配器。

### 翻译协议

内部 DTO：

```cpp
struct TranslationRegion {
    RegionId regionId;
    QString sourceText;
};

struct TranslationRequest {
    LanguageCode sourceLanguage;
    LanguageCode targetLanguage;
    ReadingDirection readingDirection;
    QVector<TranslationRegion> regions;        // 按 orderIndex 排序
    QVector<EffectiveGlossaryItem> glossary;   // Domain 类型
};
```

- 线上 JSON 形状由 P4 适配器依据技术决策 §5.1/§5.2 定义；P2 只冻结内部 DTO 与校验语义。
- 校验规则：
  - 重复 regionId → 整次响应为协议错误，不采用任何结果。
  - 未知 regionId → 忽略该条并记录安全诊断，同时提示响应异常。
  - 缺失 regionId → 仅对应 Region 失败，其余合法 Region 正常采用。
  - sourceText 与请求不符 → 对应 Region 失败；系统性错位会表现为全部不匹配。
  - 禁止按响应数组位置匹配 Region。
- 不采用整批严格拒绝模式：合法 Region 的已付费结果不应因个别缺失被丢弃；错位风险由 ID 匹配与 sourceText 校验承担。
- 术语注入：`EffectiveGlossaryItem` 是 Domain 类型，不是第三份持久数据。V0.1 解析来源仅为项目术语表（P1 `glossary`）；全局术语库、`IGlossaryRepository` 与解析 UseCase 属 V0.3。V0.1 项目术语量小，注入全部启用项；按文本匹配注入随 V0.3 术语解析引入。

### UseCase 组织

- 一个明确用户行为一个 UseCase 类：`CreateProjectUseCase`、`OpenProjectUseCase`、`SaveProjectUseCase`、`SaveProjectAsUseCase`、`CloseProjectUseCase`、`ImportImagesUseCase`、`RecognizeRegionUseCase`、`TranslatePageUseCase`、`ExportPagesUseCase` 等。
- 不为小函数造 UseCase，也不把多阶段流水线塞进 ViewModel。
- 跨阶段编排放 `application/workflow/`（如 `ProcessPageWorkflow`），协调多个 UseCase/Port，共用同一 `operationId`。

保存事务顺序（机制细节在 P3）：

```text
Domain 快照 → serialize → 校验 → 同文件系统临时文件 → 原子替换（保留 .bak）
```

任一步失败保持旧 `project.json` 不变；成功后 `dirty = false`，但不清空 UndoStack。

### 结果采用与 Undo 边界

后台结果不直接修改 Domain：

```text
OCR/翻译完成 → ViewModel 记录 suggestion（Inspector 展示）
            → 用户 Apply → ApplyOcrResultCommand / ApplyTranslationCommand
            → UndoStack
```

- 进入 Undo 的用户可见修改：Region 创建/删除/移动/缩放、原文编辑、译文编辑、候选采用、OCR/翻译结果采用、字体/字号/排版、清除设置。
- 拖动合并：MousePress 记录旧几何，MouseMove 只做 UI 预览，MouseRelease 生成一个命令。
- 文本输入合并：同一 Region 同一字段，约 500ms 时间窗口内合并为一个命令。
- 保存不产生命令，也不清空历史。

### 项目、导入与导出契约

- 打开顺序：文件存在 → JSON Schema 结构校验 → 版本检查/迁移 → 语义校验 → 资产校验 → 加载。
- 导入执行：文件可读 → 格式支持 → 可解码 → 尺寸合法 → 自然排序（NFKC + 数字自然序，`1, 2, 10` 而非 `1, 10, 2`）→ 复制进 `assets/original/`。
- 导入名称冲突自动生成新文件名，不覆盖既有文件。

```cpp
enum class ExportScope {
    CurrentPage,
    SelectedPages,
    AllPages
};
```

- 导出选项：outputDir、format（PNG/JPEG）、quality、scope。
- 命名模板 `page_0001_translated.png`；冲突追加序号 `page_0001_translated_001.png`。
- 永远没有 `overwrite = true`。

### 目录结构与冻结决策

```text
application/
├── error/        # ApplicationError、Result
├── operation/    # OperationContext、Progress
├── dto/          # OcrDto、TranslationDto、ExportDto ...
├── ports/        # IOcrEngine、ITranslator、IProjectRepository ...
├── mapping/      # DTO ↔ Domain 映射
├── usecase/
└── workflow/     # 跨阶段编排
```

冻结的五个决定：

1. Port 异步契约统一 `QFuture<std::expected<T, ApplicationError>>`。
2. 取消 = `std::stop_token` + generation 双保险。
3. `OperationId`（UUID）负责追踪，`generation`（quint64）负责过期判断。
4. Port 使用自有 DTO，第三方类型不穿透；`QImage` 为唯一图像载体。
5. 后台结果不直接写 Domain，用户 Apply 后才进入 Undo 历史。
