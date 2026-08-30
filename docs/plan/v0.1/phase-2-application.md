# P2：Application 契约与 UseCase

> 状态：Complete
> P5 前置修订：Complete（generation、缩略图、凭据状态与 Clear Undo 契约均已落地）
> 前置阶段：P1
> 输出：可由 Fake Port 完整驱动的 V0.1 业务流程

## 目标

固定应用能力、错误、异步和结果采用边界，使数据层、处理适配器和 UI 能独立开发。

## TODO

### Ports 与公共结果

- [x] 定义 `IProjectRepository`、`IComicImporter`、`IPageImageLoader`、`IOcrEngine`、`ITranslator`、`IInpainter`、`ITextLayouter`、`IComicExporter`。
- [x] 定义结构化错误类别、用户安全摘要和可恢复建议。
- [x] 定义取消令牌、进度单位、不可变输入快照和请求 generation。
- [x] 定义任务身份中的 `projectId`、`pageId`、Region ID 集合和操作 ID。
- [x] 定义 Port DTO 与 Domain 的显式映射，禁止第三方类型穿透接口。

### UseCases

- [x] 项目：创建、打开、保存、另存、关闭和恢复。
- [x] 导入：校验并规范化文件列表、执行确定性自然排序，并调用导入 Port。
- [x] Region：创建、删除、移动、缩放和顺序维护。
- [x] 文本：编辑原文、译文和候选采用。
- [x] 处理：单 Region/单页 OCR、单页翻译、清除和排版。
- [x] 导出：单页、选中页和整个项目。
- [x] 定义提交异步结果前的项目、页面、generation、输入版本和人工优先检查。

### 翻译协议

- [x] 定义单页请求、稳定 Region ID、语言、阅读方向和有效项目术语输入。
- [x] 定义非流式结构化响应 Schema。
- [x] 缺失 ID 只使对应 Region 失败；重复 ID 使整次响应失败；未知 ID 忽略并形成安全诊断。
- [x] 禁止按响应数组位置匹配 Region。

## 测试

- [x] 所有公开 UseCase 均有成功路径，输入校验按共享规则和能力边界覆盖。
- [x] 异步 Port 的失败与取消结果经 UseCase 原样传播，不发生同步阻塞。
- [x] 翻译缺失、重复、未知 Region ID 和部分成功。
- [x] Fake Port 不访问文件系统、网络、凭据或 GUI。
- [x] 取消传播：`request_stop` 后任务在下一检查点返回 `Cancelled`，不提交部分结果。
- [x] `Cancelled` 与 `Failed` 的结果区分；取消不进入错误路径和恢复建议。
- [x] 五项结果提交检查各自失败时的拒绝行为，含 `contentRevision` 变化。
- [x] Fake Port 以已完成或延迟完成的 `QFuture` 驱动测试，不需要真实线程。
- [x] sourceText 与请求不符时对应 Region 失败，其余 Region 正常采用。
- [x] 处理 UseCase 只产生结果 DTO；只有独立 Apply UseCase 能在版本和人工优先检查后修改 Domain。
- [x] 在途操作完成后自动注销；取消后不再保留可取消注册项。

## 后续阶段验收提示

以下项目不计入 P2 完成度。P2 只冻结其契约，实际行为由对应阶段实现并验收：

- P3：文件夹展开、文件可读性、格式、解码、尺寸、EXIF/ICC、资产命名冲突和部分导入失败。
- P3/P4：文件系统、ONNX、HTTP 和第三方库异常在 Adapter 内映射为 `ApplicationError`。
- P4：确定与不确定进度、处理检查点取消、翻译超时、限流退避和非幂等重试边界。
- P5：`QFutureWatcher` GUI 线程投递、视图/项目会话 generation 分流、Clear prepare 后的同步 Apply 命令进入 Undo/Redo。
- P6：关闭项目、快速切页和 View 销毁后的迟到结果，以及人工译文、几何和排版冲突的端到端验收。
- P6：导出文件名冲突追加序号且绝不覆盖既有文件。

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
- `RecoveryAction` 是 Application 定义的有限枚举，只描述可执行动作，不生成 UI 文案：

```cpp
enum class RecoveryAction {
    None,
    Retry,
    OpenSettings,
    LocateModel,
    ChoosePath,
    Reauthenticate,
    FreeDiskSpace
};

enum class OperationStage {
    Project,
    Import,
    LoadImage,
    Ocr,
    Translate,
    Clear,
    Layout,
    Export
};

enum class ExportFormat {
    Png,
    Jpeg
};

enum class RecoverySource {
    Current,
    Backup
};
```

- `RecoverySource` 属于 Application DTO（描述 Repository 本次打开方式），不是 Domain 业务事实。
- `OperationStage` 粒度止于 Application 公共契约；Adapter 内部诊断（如 `OcrPreprocess`/`OcrInference`）不进入此枚举。
- 第三方异常（ONNX、HTTP、JSON、文件系统）在适配器内捕获并映射为 `ApplicationError`，不得穿越 Port 边界。

### Port 异步契约形态

```cpp
virtual QFuture<Result<OcrResponse>> recognize(OcrRequest request,
                                               OperationContext context) = 0;
```

- 所有可能耗时或失败的 Port 操作统一返回 `QFuture<Result<T>>`；纯内存同步查询不强制。
- `QFuture` / `QPromise` / `QFutureWatcher` 属 QtCore；不引入 QtConcurrent，不使用 `std::future`。
- Port 接口不继承 `QObject`、不定义 signal；`QObject`、`QNetworkAccessManager` 等只出现在适配器内部。
- Port 方法立即返回，绝不阻塞调用线程；适配器把工作提交给有界执行器（P4）并用 `QPromise` 桥接 `QFuture`。
- 请求和 `OperationContext` 按值进入 Port；异步适配器持有它们直到返回的 Future 完成，禁止捕获调用方栈上的引用。
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
    std::shared_ptr<void> lifetime; // 在途注册的内部生命周期令牌
};
```

- 取消使用标准 `std::stop_source` / `std::stop_token`，不包装、不引入 Qt 取消句柄。
- UseCase 为每次操作创建 `std::stop_source`，并维护在途操作表（operationId → stop source）；取消入口（如 `cancelOcr(operationId)`）查表转发 `request_stop()`。请求身份和输入版本保存在结果采用请求的快照中，不在注册表中重复维护。
- `lifetime` 是不透明的内部令牌；最后一个 `OperationContext` 副本释放时自动移除注册项，避免已完成操作长期滞留。取消会立即从可取消集合中移除该项，适配器随后释放令牌是幂等的。
- 合作式取消，检查点约定：
  - OCR：每个 Region 处理前后。
  - 翻译：发送请求前；等待期间中止底层请求（如 `QNetworkReply::abort()`）。
  - 导入/导出：每个文件或页面处理前。
- 关闭项目或应用时执行双保险：`request_stop()` + 项目会话 generation 递增。切换页面只取消并失效当前视图的图像、预览等短任务；用户明确启动的 OCR、翻译、清除和批处理归项目会话所有，不因离开页面而取消。
- 取消结果为 `ErrorCategory::Cancelled`，不提交部分结果；翻译等已计费调用在 UI 说明无法保证服务端停止执行或计费。

#### operationId 与 generation 的分工

- `OperationId` 是 Application 层自有强类型，基于 `domain::Uuid` 构建；它不是 Domain 实体身份，不进入 `project.json`（`ProjectId`/`PageId`/`RegionId` 才是 Domain 持久化身份）。
- `operationId`（UUID）用于日志、Trace 与流水线关联；一个工作流（OCR → 翻译 → 排版）共用同一 `operationId`。
- `generation`（quint64）只负责所属生命周期范围内的过期判断，不是一个覆盖所有操作的全局时钟。P5 维护两个范围：`viewGeneration` 属当前页面视图，`projectSessionGeneration` 属打开的项目会话。
- 页面图像、缩略图绑定和预览请求捕获 `viewGeneration`；切页使这类 UI 结果失效。OCR、翻译、清除、排版和导出捕获 `projectSessionGeneration`，切页后可继续执行并按原 PageId 采用。
- `operationId` 负责一次工作流的追踪与显式取消；`generation` 负责生命周期隔离。两者不相等、不互换。
- 触发 invalidate 的时机：切页只递增 `viewGeneration`；切换/关闭项目或应用退出递增 `projectSessionGeneration`；同一目标的新操作替代旧操作时显式取消旧 `operationId`，不误伤其他页面任务。

#### 提交结果前的五项检查

UseCase 在提交异步结果前执行，任一失败即丢弃结果并按过期处理：

1. `projectId` 一致。
2. `pageId` 一致。
3. 请求所属 generation 一致：视图结果比较 `viewGeneration`，项目后台结果比较 `projectSessionGeneration`。
4. 输入版本一致：涉及 Region 的 `contentRevision` 未变化（该值由 P1 序列化器持久化，保存重开后仍可作为输入版本依据）。
5. 人工优先：`manualText` 有值时后台结果不得覆盖。

被拒绝的结果不写 Domain、不进 Undo 历史。视图过期结果静默释放；项目后台结果仍可更新任务状态，但不得错误绑定到当前页面。

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
- Domain 内部使用 `std::string`（UTF-8），Port DTO 使用 `QString`；两者转换在映射层完成，Domain 不依赖 QtCore 字符串类型。

#### P3 Port DTO 索引

以下 Port 的完整契约与 Adapter 语义由 P3 冻结，此处仅列 DTO 名称作为契约索引：

```text
IProjectRepository:
  CreateProjectRequest / OpenProjectRequest / SaveProjectRequest / SaveProjectResult
  SaveAsProjectRequest / SaveAsProjectResult / CloseProjectRequest
  ProjectDescriptor / OpenProjectResult (含 RecoverySource)

IComicImporter:
  ImportImagesRequest / ImportedPage / ImportFailure / ImportImagesResult (含 batchId)
  DiscardImportedAssetsRequest / AcknowledgeImportedAssetsRequest

IPageImageLoader:
  LoadPageImageRequest (含 expectedSize) → Result<QImage>
```

> 保存事务、文件锁、恢复优先级、资产规范化等 Adapter 级语义详见 P3 设计规格参考。

### IInpainter / ITextLayouter / IComicExporter 契约

#### IInpainter

```cpp
struct ClearRequest {
    ProjectId projectId;
    PageId pageId;
    RegionId regionId;
    QImage pageImage;           // 规范化整页快照
    RectGeometry region;
    ClearSettings settings;
    quint64 contentRevision;
};

struct ClearResponse {
    RegionId regionId;
    RectGeometry affectedBounds;
    QImage patch;               // 仅清除区域，非整页
};

virtual QFuture<Result<ClearResponse>> clear(ClearRequest, OperationContext) = 0;
```

- 传整页而非裁剪图：V0.1 纯色填充可用，V0.2 OpenCV/LaMa Inpaint 需要 Region 周围上下文时无需改 Port。
- 响应只返回 patch，由 UseCase/ViewModel 合成到图层。

#### ITextLayouter

```cpp
struct LayoutRequest {
    PageId pageId;
    RegionId regionId;
    QString text;
    RectGeometry bounds;
    TextLayout constraints;
    double logicalDpi = 96.0;
    quint64 contentRevision;
};

struct LayoutWarning {
    QString code;        // layout.font_missing / layout.missing_glyph / layout.vertical_fallback
    QString safeMessage;
};

struct LayoutResponse {
    RegionId regionId;
    TextLayout resolvedLayout;   // 含最终 fontSizePt
    bool overflow;
    QVector<LayoutWarning> warnings;
};

virtual QFuture<Result<LayoutResponse>> layout(LayoutRequest, OperationContext) = 0;
```

- 不传整个 `TextRegion`，只传排版所需数据。
- **排版逻辑 DPI 冻结为 96**：`fontSizePt` 在预览与导出间保持一致；若未来需要项目级 DPI 设置，再扩展 `logicalDpi` 字段来源。
- `overflow = true` 表示文本超出 bounds，UI 据此显示警告；排版器绝不自动改写译文。
- `warnings` 是成功结果的非阻塞告警；`layout.overflow` 由 `overflow` 表达，不同时作为失败返回。（P4 契约补丁）

#### IComicExporter

```cpp
struct ExportPageSnapshot {
    PageId pageId;
    QString outputStem;             // 由 ExportPagesUseCase 从项目 orderIndex 生成
    QImage sourceImage;
    QImage clearLayer;              // 可空透明层
    QImage translationLayer;        // 可空透明层
    QImage manualOverrideLayer;     // 可空透明层
};

struct ExportRequest {
    QVector<ExportPageSnapshot> pages;
    QString outputDir;
    ExportFormat format;       // Png / Jpeg
    int jpegQuality = 95;
};

struct ExportedPage {
    PageId pageId;
    QString path;
};

struct ExportFailure {
    PageId pageId;
    ApplicationError error;
};

struct ExportResponse {
    QVector<ExportedPage> exported;
    QVector<ExportFailure> failures;
};

virtual QFuture<Result<ExportResponse>> exportPages(ExportRequest, OperationContext) = 0;
```

- `ExportScope`（CurrentPage/SelectedPages/AllPages）属于 `ExportPagesUseCase`，Port 只接收已解析的具体页面快照。
- `ExportPagesUseCase` 在调用 Port 前解析并校验所有图层：source 与三个可选 layer 必须尺寸一致、RGBA8、straight alpha、sRGB；`outputStem` 匹配 `[A-Za-z0-9][A-Za-z0-9._-]{0,127}` 且不以点结尾。（P4 契约补丁）
- 冲突策略为追加序号、永不覆盖；失败逐页携带原因，成功页不影响失败页。

#### 输入快照共同规则

所有处理类 Port（OCR/翻译/清除/排版/导出）的 Worker 必须遵守：

- 不回读 `ComicProject` 或任何 Domain 聚合。
- 不持有 `TextRegion&` 或其他 Domain 引用。
- 不访问 Repository、ViewModel 或 Scene。
- 请求 DTO 即不可变快照；Worker 只做纯计算或纯 I/O。

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
    QVector<TranslationRegion> regions;    // 按 orderIndex 排序
    QVector<GlossaryEntry> glossary;       // 项目术语（P1 Domain 类型）
};

struct TranslationResponseItem {
    RegionId regionId;
    QString sourceText;
    QString translation;
    QVector<QString> candidates;
};

struct TranslationResponse {
    QString providerId;   // 本次实际使用的配置身份
    QVector<TranslationResponseItem> items;
};
```

- `providerId` 是本次实际使用的 Provider 配置身份；候选的 `provider` 字段取自 `TranslationResponse.providerId`，不再写死为 `"translator"`。（P4 契约补丁）
- 候选 ID 合成：线上响应只有候选文本，映射层为每个候选生成响应内稳定的 ID（形如 `<regionId>-<序号>`），Provider 未给候选置信度时写 `0.0`，其语义为“未提供”，不得伪造为 `1.0`；`selectedCandidateId` 的"必须存在"约束由 Domain 保证。
- 线上 JSON 形状由 P4 适配器依据技术决策 §5.1/§5.2 定义；P2 只冻结内部 DTO 与校验语义。
- 校验规则：
  - 重复 regionId → 整次响应为协议错误，不采用任何结果。
  - 未知 regionId → 忽略该条并记录安全诊断，同时提示响应异常。
  - 缺失 regionId → 仅对应 Region 失败，其余合法 Region 正常采用。
  - sourceText 与请求不符 → 对应 Region 失败；系统性错位会表现为全部不匹配。
  - 禁止按响应数组位置匹配 Region。
- 不采用整批严格拒绝模式：合法 Region 的已付费结果不应因个别缺失被丢弃；错位风险由 ID 匹配与 sourceText 校验承担。
- 术语注入：V0.1 使用项目术语 `GlossaryEntry`（P1 Domain 类型），不是第三份持久数据。`EffectiveGlossaryItem` 是 V0.3 随全局术语库与解析 UseCase 引入的只读解析结果，V0.1 不定义。V0.1 项目术语量小，注入全部启用项；按文本匹配注入随 V0.3 术语解析引入。

### UseCase 组织

UseCase 负责业务合法性校验与 Domain 修改；`QUndoCommand` 负责保存 before/after 状态并调用对应 UseCase。UseCase 不依赖 `QUndoStack`，避免与 Command 重复实现同一业务规则。

```text
project/
  CreateProjectUseCase
  OpenProjectUseCase
  SaveProjectUseCase
  SaveProjectAsUseCase
  CloseProjectUseCase
  RecoverProjectUseCase

import/
  ImportImagesUseCase
  LoadPageImageUseCase

region/
  CreateRegionUseCase
  DeleteRegionUseCase
  MoveRegionUseCase
  ResizeRegionUseCase

text/
  EditSourceTextUseCase
  EditTranslationUseCase
  SelectCandidateUseCase
  ApplyOcrResultUseCase
  ApplyTranslationResultUseCase

apply/
  PrepareClearResultUseCase
  ApplyPreparedClearChangeUseCase
  ApplyLayoutResultUseCase

processing/
  RecognizeRegionUseCase
  TranslatePageUseCase
  ClearRegionUseCase
  LayoutRegionUseCase

export/
  ExportPagesUseCase

thumbnail/
  LoadOrGenerateThumbnailUseCase

settings/
  QueryProviderCredentialStatusUseCase
  ConfigureProviderCredentialUseCase
  ClearProviderCredentialUseCase
```

- 不为小函数造 UseCase，也不把多阶段流水线塞进 ViewModel。
- 后台结果采用（`ApplyOcrResultUseCase` / `ApplyTranslationResultUseCase`）是独立用户行为，与产生 suggestion 的处理 UseCase 分离。

### P4/P5 契约补丁

P4/P5 实现前对 P2 契约的小范围扩展；这些类型仍是 Application 自有类型，不引入 ONNX、HTTP 或 UI 类型。详细行为语义见 P4、P5 设计规格参考。

- `ICredentialStore`（`application/ports`）：同步窄端口，`SecretBytes` 为 move-only 零化容器；提供 `readApiKey`/`writeApiKey`/`deleteApiKey`，并提供不返回明文的 `hasApiKey(providerId)`。UI 只能调用凭据状态、配置和清除 UseCase，不得直接读取 Secret。
- `IDerivedAssetStore`（`application/ports`）：仅负责读取和原子提交页面派生图层；请求携带 `ProjectId + projectRoot + PageId + layerKind + QImage`，返回项目根相对的内容寻址路径。
- `IThumbnailStore`（`application/ports`）：读取和原子提交固定版本的页面缩略图，不接受调用方自定义路径。`LoadOrGenerateThumbnailUseCase` 协调 `IPageImageLoader`、最长边 256 px 的等比缩放和该 Store；失败不修改 Domain。
- `IAppSettingsStore` 与 `IRecentProjectsStore`：保存非项目级应用设置和最近项目元数据；Data 层可用 `QSettings` 实现，绝不存储 API Key。
- `PrepareClearResultUseCase`：执行采用检查、合成 patch 并通过 `IDerivedAssetStore` 原子提交不可变内容，返回只含 before/after 引用与 Region 状态的 `PreparedClearChange`，不修改 Domain。
- `ApplyPreparedClearChangeUseCase`：仅同步切换已存在的 layer 引用与 Region `isCleared`；供 GUI 线程上的 Undo Command 的 `redo()`/`undo()` 调用，不执行 I/O。
- `ApplyLayoutResultUseCase`：通过 `applyPageRegionEdits` 原子写入 `resolvedLayout`。
- Domain 窄操作 `ComicProject::applyClearResultOnPage(PageId, RegionId, relativeLayerPath)`：一次校验、一次 markDirty，不向应用层暴露 `ComicPage*`。

#### P5 开工前必须落地的契约修订

- [x] 把 generation 拆成视图范围与项目会话范围，并更新所有请求快照、采用检查和测试。
- [x] 增加 `IThumbnailStore`、`LoadOrGenerateThumbnailUseCase` 及固定路径语义（Port/UseCase 已实现，Data 层 Adapter 归 P5 跨层补丁）。
- [x] 增加不暴露 Secret 的凭据状态/配置/清除 UseCase。
- [x] 把 Clear 采用改为 `PrepareClearResultUseCase` + 同步 `ApplyPreparedClearChangeUseCase` 两阶段协议。
- [x] 增加应用设置与最近项目窄 Port，避免 ViewModel 直接使用 `QSettings`。

### Workflow 编排原则

跨阶段编排放 `application/workflow/`（如 `ProcessPageWorkflow`），协调多个 UseCase/Port，共用同一 `operationId`。V0.1 冻结以下原则，完整状态机在实现时再定：

1. **只编排现有 UseCase**，不直接调用 Adapter。
2. **阶段间传不可变内存结果**，不通过写 Domain 作为临时通信手段。例如 OCR suggestion 以快照传给 Translate，Translate 使用该快照而非从 Domain 读取。
3. **未经用户 Apply 的 OCR/翻译结果不得写 Domain**。Workflow 产生的中间 suggestion 只在内存中流转。
4. **某阶段失败默认停止依赖该结果的后续阶段**，但不回滚此前已由用户正式 Apply 的修改。

具体的阶段状态机、部分失败策略、重试和自动 Apply 等细节在实现 `ProcessPageWorkflow` 时确定。

保存事务顺序（机制细节在 P3）：

```text
Domain 快照 → serialize → 校验 → 同文件系统临时文件 → 原子替换（保留 .bak）
```

任一步失败保持旧 `project.json` 不变。成功后 Repository 返回实际落盘快照的 `savedRevision`；aggregate owner 仅在当前 revision 仍与其相等时调用 `markCleanIfRevision(savedRevision)`，因此保存期间产生的新修改仍保持 Dirty。保存不清空 UndoStack。

### 结果采用与 Undo 边界

后台结果不直接修改 Domain。普通 suggestion 由命令同步采用；涉及文件 I/O 的清除结果先在后台准备不可变资产，再由 GUI 线程推入命令：

```text
OCR/翻译完成 → ViewModel 记录 suggestion（Inspector 展示）
            → 用户 Apply → ApplyOcrResultCommand / ApplyTranslationCommand
            → UndoStack

清除完成 → PrepareClearResultUseCase 原子提交不可变资产，不改 Domain
        → GUI watcher 收到 PreparedClearChange
        → push ClearResultCommand
        → redo/undo 仅同步切换 Domain 引用，不做 I/O
```

- 进入 Undo 的用户可见修改：Region 创建/删除/移动/缩放、原文编辑、译文编辑、候选采用、OCR/翻译结果采用、字体/字号/排版、清除设置。
- 拖动合并：MousePress 记录旧几何，MouseMove 只做 UI 预览，MouseRelease 生成一个命令。
- 文本输入合并：同一 Region 同一字段，约 500ms 时间窗口内合并为一个命令。
- 保存不产生命令，也不清空历史。保存成功且 `savedRevision` 仍等于当前项目 revision 时，同时调用 `markCleanIfRevision(savedRevision)` 与 `undoStack.setClean()`。
- `ComicProject::revision/isDirty()` 是持久化 Dirty 的权威来源；`QUndoStack::isClean()` 只描述命令历史位置，UI 关闭拦截以 Domain Dirty 为准，不用手工保存 `savedUndoIndex`。
- Clear Command 引用的不可变派生资产在命令仍位于 UndoStack 时必须保留；V0.1 在项目打开期间不回收这些文件。项目关闭后的孤儿清理只能依据当前 Domain 引用和已释放的命令历史做可达性判断。

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
2. 取消 = `std::stop_token` + 分范围 generation 双保险。
3. `OperationId`（UUID）负责追踪；`viewGeneration` 与 `projectSessionGeneration` 分别负责视图和项目会话过期判断。
4. Port 使用自有 DTO，第三方类型不穿透；`QImage` 为唯一图像载体。
5. 后台结果不直接写 Domain，用户 Apply 后才进入 Undo 历史。
