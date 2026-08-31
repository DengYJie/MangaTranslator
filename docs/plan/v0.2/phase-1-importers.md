# V2-P1 安全归档与 PDF 导入 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不覆盖输入文件、不暴露 Adapter 类型且 GUI 线程不执行长任务的前提下，将图片、CBZ/ZIP 和 PDF 统一导入为可采用或可丢弃的项目页面批次。

**Architecture:** 保留一个 Application 层 `IComicImporter`，请求通过不可变 tagged source 区分 `ImageFiles`、`ArchiveFile` 和 `PdfFile`。Data 层使用共享 `ImportPipeline`，格式 Decoder 只产生逻辑页面，`PageNormalizer` 统一验证，`PendingBatchWriter` 独占 staging、批次级原子资源晋升、丢弃、确认和恢复清理；资源晋升先于用户审阅，Domain 提交只消费已经准备好的相对引用，不再执行解码或文件 I/O。

**Tech Stack:** C++23、Qt 6.11 Core/Gui/Pdf、Qt Widgets/FluentQt 既有 UI、libarchive、CMake imported targets、Qt Test/CTest。

**Spec:** `docs/v0.2-technical-decisions.md` 第 4、5 节；`docs/plan/v0.2/phase-0-contracts.md`；`docs/architecture-design.md`。

## Global Constraints

- 状态：`Planned`；前置阶段是 V2-P0，P1-00 契约门未通过时不得开始生产 Adapter。
- 只支持一次请求中的一种来源：图片文件/文件夹列表、一个 CBZ/ZIP 或一个 PDF；不支持异构混合批次。
- 部分恢复只允许“采用所有成功页面”或“丢弃整个批次”；不提供逐页勾选、原地重试和恢复已取消 Decoder。
- ZIP/CBZ 上限固定为 10,000 个 Entry、单 Entry 256 MiB、累计 2 GiB、可信元数据下 100:1 压缩比、路径深度 32。
- PDF 基线固定为 300 DPI、最低 72 DPI、单页 32 MP、最长边 8,192 px、单页同时存活工作集 512 MiB；透明度合成到白色，输出为 sRGB 8-bit。
- CBR、CB7、加密归档、嵌套归档、PDF 文本抽取、矢量保真、缩略图、检测、OCR 和页面流水线不属于本阶段。
- Application/UI 不包含 libarchive 或 `QPdfDocument` 类型；`Qt6::Pdf` 和 `LibArchive::LibArchive` 只由 `manga_data` 私有链接。
- 所有磁盘写入都在 `ProjectIoCoordinator` 写票据内；输入文件只读，日志和 DTO 不包含完整用户路径或漫画内容。
- 使用 `Q_SIGNALS`、`Q_SLOTS` 和 `Q_EMIT`，不使用 `signals`、`slots`、`emit`；新 target 定义 `QT_NO_KEYWORDS`。
- 普通构建和测试必须离线；fixture 自制或具有可记录许可，不提交用户漫画。
- 本计划中的 Git 提交步骤只有在用户明确授权提交时执行；未授权时只暂存建议和报告验证结果。

---

## 1. 冻结的设计决策

### 1.1 单一应用端口

不新增 `IArchiveImporter` 或 `IPdfImporter`。V2-P0 必须先把现有 `ImportImagesRequest`/`ImportImagesResult` 泛化并冻结为以下合同：

```cpp
struct ImageFilesImportSource { QVector<QString> paths; };
struct ArchiveFileImportSource { QString path; };
struct PdfFileImportSource { QString path; };
using ImportSource = std::variant<ImageFilesImportSource,
                                  ArchiveFileImportSource,
                                  PdfFileImportSource>;

struct ImportPolicySnapshot {
    qsizetype maximumArchiveEntries{10'000};
    qint64 maximumArchiveEntryBytes{256LL * 1024 * 1024};
    qint64 maximumArchiveTotalBytes{2LL * 1024 * 1024 * 1024};
    int maximumCompressionRatio{100};
    int maximumArchivePathDepth{32};
    int requestedPdfDpi{300};
    int minimumPdfDpi{72};
    qint64 maximumPdfPixels{32'000'000};
    int maximumPdfEdgePixels{8'192};
    qint64 maximumPdfWorkingSetBytes{512LL * 1024 * 1024};
};

struct PrepareImportRequest {
    domain::ProjectId projectId;
    QString projectRoot;
    quint64 projectRevision{0};
    ImportSource source;
    ImportPolicySnapshot policy;
};

enum class ImportProgressStage { Validating, Scanning, Decoding, Rendering, Promoting };
struct ImportProgress {
    ImportProgressStage stage{ImportProgressStage::Validating};
    qint64 completedUnits{0};
    std::optional<qint64> totalUnits;
};
using ImportProgressCallback = std::function<void(ImportProgress)>;
```

`ImportPolicySnapshot` 在 UseCase 构造请求时完成范围校验。Adapter 只能读取快照，不能读取会变化的设置对象。

### 1.2 结果、诊断与恢复

```cpp
enum class PreparedImportState { Ready, ReviewRequired };
enum class ImportDiagnosticScope { Source, Page };
enum class ImportRecoveryAction {
    AcceptPreparedPages,
    DiscardBatch,
    RetryAcknowledgement,
    RetryCleanup,
};

struct ImportDiagnostic {
    ImportDiagnosticScope scope{ImportDiagnosticScope::Source};
    int sourceOrdinal{-1};
    QString safeLocator;
    ApplicationError error;
};

struct PreparedImportResult {
    OperationId batchId;
    PreparedImportState state{PreparedImportState::Ready};
    QVector<ImportedPage> pages;
    QVector<ImportDiagnostic> diagnostics;
    QVector<ImportRecoveryAction> actions;
    quint64 projectRevision{0};
    quint64 sessionGeneration{0};
};
```

- `Ready` 表示所有候选页准备成功。
- `ReviewRequired` 表示至少一页成功且至少一页发生可恢复失败。
- 零成功页返回 terminal `ApplicationError`，不发布 `batchId` 或相对资产引用。
- UI 只收到 `batchId`、安全 locator、页数、诊断和恢复动作，不收到 staging 路径。
- `DiscardImportedAssetsRequest` 只携带项目身份、项目根和 `batchId`，不再接受 UI 回传的 `assetPaths`。

### 1.3 两种提交必须分开命名

```text
private staging
  → asset promotion（Data：一次批目录原子 rename）
  → PreparedReady / PreparedReviewRequired（所有权交给调用方）
  → Domain apply（Application：同步、无 I/O）
  → project save（Repository：原子保存）
  → acknowledge（Data：移除 pending 所有权）
```

禁止把 asset promotion 和 Domain apply 都称为“commit”。采用部分结果不得重新解码、重新渲染、复制资源或第二次晋升文件。

### 1.4 生命周期与胜负边界

| 状态 | 可见/持久资产 | 合法后继 |
| --- | --- | --- |
| `Validating` | 无 | `Staging`、失败、取消 |
| `Staging` | 私有批次 staging | 继续或内部清理 |
| `Promoting` | 未发布事务 | `Prepared*` 或内部回滚 |
| `PreparedReady` | pending 相对资产和 manifest | apply 或 discard |
| `PreparedReviewRequired` | 同上并含有序诊断 | accept-all 或 discard |
| `Applying` | pending 资产；Domain 尚未成功 | `AdoptedPendingAck` 或 discard |
| `AdoptedPendingAck` | Domain/已保存项目引用资产 | acknowledge 或 retry acknowledge |
| `Acknowledged` | 资产已采用，pending 所有权移除 | 终态 |
| `Discarding` | Domain 不得引用该批次 | `Discarded` 或 retry cleanup |
| `Discarded` | 无批次拥有资产 | 终态 |

- `Promoting` 是短暂不可取消区间；rename 后立刻再次检查 stop token 和 session generation。
- 发布结果与取消使用一个 winner-selection；取消先赢则内部清理，发布先赢则后续取消必须显式 discard。
- Domain apply 前再次检查 project id、revision/session generation 和 `batchId`。
- Domain apply 失败时 Domain 恢复快照并自动 discard。
- 项目保存失败时 Domain 恢复快照并自动 discard。
- 保存成功但 acknowledge 失败时不得删除资产，进入 `AdoptedPendingAck` 并仅允许重试 acknowledge。
- apply、discard 和 acknowledge 对同一 `batchId` 必须幂等，重复采用不得重复追加页面。

### 1.5 磁盘布局与恢复

V2-P0 Schema 必须允许以下批次级相对路径：

```text
.mangatranslator-staging/import-<batch-id>/
  manifest.json.tmp
  payload/
assets/original/import-<batch-id>/
  manifest.json
  page_0001.png
  page_0002.jpg
```

- staging 和 `assets/original` 位于同一项目文件系统，asset promotion 是一次目录 rename。
- manifest 至少记录 `batchId`、project id、source kind、经脱敏的 source locator、页面 ordinal、相对资产路径、内容 SHA-256 和状态。
- 启动恢复只删除未发布 staging；pending 批次必须先与项目 JSON 引用集合核对。任何被项目引用的资产都视为 adopted，不得因 manifest 仍存在而删除。
- 若项目根无法保证同文件系统 rename，P1-00 直接失败；实现不得静默退化为逐文件复制。

### 1.6 错误分类矩阵

| 条件 | 作用域 | 结果与恢复 |
| --- | --- | --- |
| source kind/请求非法 | Request fatal | 无 staging |
| traversal、绝对/盘符/UNC、特殊 Entry、加密、嵌套、结构损坏、硬上限 | Batch fatal | 删除 staging，不发布批次 |
| 非图片普通 Entry | Source diagnostic | 计入 Entry/字节限制，跳过并继续 |
| 已批准图片 Entry 解码失败 | Page recoverable | 有成功页时 `ReviewRequired` |
| PDF 无法载入、需要密码、权限拒绝 | Batch fatal | 删除 staging |
| 单页 PDF 无法在预算内渲染 | Page recoverable | 记录页码并继续 |
| 归一化/编码失败 | Page recoverable | 存储完整性不确定时升级为 batch fatal |
| 零成功页 | Batch fatal | 删除 staging，不发布批次 |
| staging/promotion I/O 失败 | Batch fatal | 回滚批事务 |
| 发布前取消或 stale | Cancelled/stale | 内部清理，不发布引用 |
| apply/save 拒绝 | Domain 不变 | 自动 discard pending 批次 |
| save 后 acknowledge 失败 | 已采用 | 保留资产，重试 acknowledge |
| discard 失败 | Domain 不变 | 保留 cleanup obligation，重试 cleanup |

---

## 2. 文件结构

### Application 层

- Modify: `src/application/dto/ImportDto.h`：统一来源、策略快照、准备结果、诊断、恢复动作和 finalize DTO。
- Modify: `src/application/error/ApplicationError.h`：补充 P0 已冻结的 import recovery action；不加入 Adapter 文本。
- Modify: `src/application/ports/IComicImporter.h`：暴露 `prepareImport`、`discardPreparedImport`、`acknowledgePreparedImport`。
- Modify: `src/application/usecase/import/ImportUseCases.h`：请求验证、prepare、Domain apply、discard/ack 以及 save-before-ack 编排。
- Modify: `tests/unit/application/FakeComicImporter.h`：可注入 prepared、discard、ack 结果及调用计数。
- Modify: `tests/unit/application/ImportImagesUseCaseTest.*`：改为统一来源契约和最终化状态机测试。

### Data 层

- Modify: `src/data/image/ImageNormalization.*`：增加从内部 spool/QImage 归一化的共享入口，保持 V0.1 EXIF/ICC/RGBA8 语义。
- Create: `src/data/import/ImportTypes.h`：Data 内部 manifest、decoded page、normalized payload 值类型。
- Create: `src/data/import/IImportSourceDecoder.h`：只在 Data 内可见的 Decoder 协议。
- Create: `src/data/import/ImageFileSourceDecoder.*`：把 V0.1 文件/文件夹展开适配到公共 Decoder。
- Create: `src/data/import/ArchiveScanner.*`：归档结构、安全、配额、NFKC 冲突和排序。
- Create: `src/data/import/ArchivePageDecoder.*`：受限流式读取、生成 spool 名和图片解码。
- Create: `src/data/import/PdfPageRenderer.*`：`QPdfDocument` worker 所有权、尺寸预算和逐页渲染。
- Create: `src/data/import/PageNormalizer.*`：公共内容探测、归一化、输出编码和诊断映射。
- Create: `src/data/import/PendingBatchWriter.*`：staging、manifest、原子 asset promotion、幂等 discard/ack 和恢复。
- Create: `src/data/import/ImportPipeline.*`：选择 Decoder、转发稳定进度、检查取消/代际并发布批次。
- Modify: `src/data/import/FileSystemComicImporter.*`：缩减为异步调度、`ProjectIoCoordinator` 写票据、pipeline/finalization 入口。
- Modify: `src/data/project/ProjectLayout.h`：增加批次 staging/pending 目录常量和纯路径构造函数。
- Modify: `src/data/CMakeLists.txt`：列出新源文件并私有链接 Qt Pdf/libarchive。
- Modify: `CMakeLists.txt`：发现 Qt Pdf 与 LibArchive imported targets。

### UI 与组合根

- Modify: `src/ui/screen/import/ImportState.h`：source kind、阶段进度、partial review、恢复动作和 ack/cleanup 重试状态。
- Modify: `src/ui/screen/import/ImportViewModel.*`：统一 prepare 流程、accept-all/discard、save-before-ack 和 stale 防护。
- Modify: `src/ui/screen/import/ImportPage.*`：CBZ/ZIP/PDF 选择、部分失败摘要与明确采用/丢弃动作。
- Modify: `src/ui/screen/import/FileDropZone.*`：按一次请求一种来源验证拖放。
- Modify: `src/ui/screen/import/PendingSourcesModel.*`：展示来源类型和安全名称。
- Modify: `src/app/ApplicationContext.*`：构造共享 pipeline、decoder、writer 并保持唯一组合根。
- Modify: `tests/unit/ui/import_view_model_test.cpp`、`tests/unit/ui/import_page_test.cpp`：状态和视觉语义测试。

### 测试与 fixture

- Create: `tests/unit/data/ArchiveScannerTest.*`
- Create: `tests/unit/data/ArchivePageDecoderTest.*`
- Create: `tests/unit/data/PdfPageRendererTest.*`
- Create: `tests/unit/data/PendingBatchWriterTest.*`
- Create: `tests/unit/data/ImportPipelineTest.*`
- Modify: `tests/unit/data/FileSystemComicImporterTest.*`
- Modify: `tests/unit/data/CMakeLists.txt`
- Create: `tests/unit/data/fixtures/generate_import_fixtures.py`
- Create: `tests/unit/data/fixtures/import/README.md`
- Create: `tests/integration/ImporterIntegrationTest.cpp`
- Modify: `tests/integration/CMakeLists.txt`

---

## 3. 实施任务

### Task 1: P1-00 契约与 Schema 硬门

**Files:**
- Modify: `src/application/dto/ImportDto.h`
- Modify: `src/application/error/ApplicationError.h`
- Modify: `src/application/ports/IComicImporter.h`
- Modify: `src/application/usecase/import/ImportUseCases.h`
- Modify: `src/data/project/ProjectSchemaValidator.*`
- Test: `tests/unit/application/ImportImagesUseCaseTest.*`
- Test: `tests/unit/data/ProjectSerializerTest.cpp`

**Interfaces:**
- Consumes: V2-P0 的 tagged `ImportSource`、`ImportPolicySnapshot`、稳定错误码、批次相对路径和 provenance Schema。
- Produces: `IComicImporter::prepareImport(PrepareImportRequest, OperationContext, ImportProgressCallback)`、`discardPreparedImport(...)`、`acknowledgePreparedImport(...)`，以及 `PreparedImportResult`。

- [ ] **Step 1: 写编译失败的合同测试**

```cpp
void ImportImagesUseCaseTest::acceptsExactlyOneTaggedSource() {
    PrepareImportRequest request{validProjectId(), absoluteProjectRoot(),
                                 ArchiveFileImportSource{u"book.cbz"_s}, defaultImportPolicy()};
    QCOMPARE(sourceKind(request.source), ImportSourceKind::ArchiveFile);
    QVERIFY(validateImportPolicy(request.policy).has_value());
}
```

- [ ] **Step 2: 运行 Application 测试确认失败**

Run: `cmake --build --preset windows-msvc-test --target manga_tests_application`

Expected: FAIL，缺少统一 DTO 或 `prepareImport` 接口。

- [ ] **Step 3: 按第 1 节精确增加 DTO 与端口**

```cpp
class IComicImporter {
public:
    virtual ~IComicImporter() = default;
    virtual QFuture<Result<PreparedImportResult>>
    prepareImport(PrepareImportRequest request,
                  OperationContext context,
                  ImportProgressCallback progress) = 0;
    virtual QFuture<Result<void>>
    discardPreparedImport(DiscardPreparedImportRequest request, OperationContext context) = 0;
    virtual QFuture<Result<void>>
    acknowledgePreparedImport(AcknowledgePreparedImportRequest request, OperationContext context) = 0;
};
```

- [ ] **Step 4: 增加 Schema round-trip 与非法引用测试**

测试必须接受 `assets/original/import-<batch-id>/page_0001.png`，拒绝绝对路径、`..` 和指向其他 batch 的 manifest 页面。

- [ ] **Step 5: 运行窄测试**

Run: `ctest --preset windows-msvc-test -R "manga_tests_(application|data)"`

Expected: PASS；Application 头文件不需要 Qt Pdf/libarchive include。

- [ ] **Step 6: 授权后提交**

```powershell
git add src/application src/data/project tests/unit/application tests/unit/data/ProjectSerializerTest.cpp
git commit -m "feat(application): generalize prepared import contracts"
```

### Task 2: P1-01 依赖和 Data 内部协议

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/data/CMakeLists.txt`
- Create: `src/data/import/ImportTypes.h`
- Create: `src/data/import/IImportSourceDecoder.h`
- Test: `tests/unit/data/ImportPipelineTest.*`

**Interfaces:**
- Consumes: `PrepareImportRequest`、`ImportPolicySnapshot`、`OperationContext`。
- Produces: `IImportSourceDecoder::decode(const DecodeRequest&, PageSink)`；`DecodedImportPage` 只含 ordinal、安全 locator、spool/pixel payload 和诊断，不含最终项目路径。

- [ ] **Step 1: 写内部协议编译测试**

```cpp
static_assert(!std::is_copy_constructible_v<DecodeSession>);
static_assert(std::is_move_constructible_v<DecodedImportPage>);
```

- [ ] **Step 2: 构建并确认内部协议尚未实现**

Run: `cmake --build --preset windows-msvc-test --target manga_tests_data`

Expected: FAIL，缺少 `DecodeSession`、`DecodedImportPage` 和 `IImportSourceDecoder`。

- [ ] **Step 3: 使用 imported targets 接入依赖**

```cmake
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Network Sql Pdf)
find_package(LibArchive 3.7.4 REQUIRED)

target_link_libraries(manga_data
    PRIVATE Qt6::Pdf LibArchive::LibArchive
)
```

同时在配置阶段检查 `LibArchive_VERSION` 且用一个最小 ZIP fixture smoke test 证明 ZIP reader 可用；不恢复 vcpkg、不使用 `FetchContent`。

- [ ] **Step 4: 定义 Data-only Decoder 协议**

```cpp
class IImportSourceDecoder {
public:
    using PageSink = std::function<Result<void>(DecodedImportPage&&)>;
    virtual ~IImportSourceDecoder() = default;
    virtual Result<DecodeSummary> decode(const DecodeRequest& request,
                                         PageSink sink,
                                         std::stop_token stopToken) = 0;
};
```

- [ ] **Step 5: 构建并运行 smoke test**

Run: `cmake --build --preset windows-msvc-test --target manga_tests_data`

Expected: PASS；`dumpbin /DEPENDENTS` 或 CMake link line 只在 `manga_data`/其测试出现 Qt Pdf 和 libarchive。

- [ ] **Step 6: 授权后提交**

```powershell
git add CMakeLists.txt src/data/CMakeLists.txt src/data/import/ImportTypes.h src/data/import/IImportSourceDecoder.h tests/unit/data
git commit -m "build(data): add archive and pdf import dependencies"
```

### Task 3: P1-02 抽取共享图片归一化管线

**Files:**
- Create: `src/data/import/ImageFileSourceDecoder.*`
- Create: `src/data/import/PageNormalizer.*`
- Modify: `src/data/image/ImageNormalization.*`
- Modify: `src/data/import/FileSystemComicImporter.*`
- Test: `tests/unit/data/FileSystemComicImporterTest.*`
- Test: `tests/unit/data/ImportPipelineTest.*`

**Interfaces:**
- Consumes: `IImportSourceDecoder`、现有 magic-byte probe、EXIF/ICC/RGBA8 规则。
- Produces: `PageNormalizer::normalize(DecodedImportPage, ImportPolicySnapshot, stop_token)` 和与 V0.1 等价的图片来源 Decoder。

- [ ] **Step 1: 锁定 V0.1 回归行为**

测试逐项断言 `page1/page2/page10` 顺序、文件夹只展开一层、伪装扩展名、EXIF 尺寸、ICC/CMYK/animated warnings、相对路径、取消清理和源文件 SHA-256 不变。

- [ ] **Step 2: 运行回归测试记录当前通过基线**

Run: `ctest --preset windows-msvc-test -R "^manga_tests_data$"`

Expected: PASS。

- [ ] **Step 3: 抽取共享归一化入口**

```cpp
[[nodiscard]] Result<NormalizedPagePayload>
PageNormalizer::normalize(DecodedImportPage page,
                          const ImportPolicySnapshot& policy,
                          std::stop_token stopToken) const;
```

源文件/归档图片保留经过内容验证的原始 payload；PDF 页面编码为无损 PNG。Domain `imageSize` 始终来自 EXIF/ICC 处理后的 RGBA8 图像。

- [ ] **Step 4: 让图片 Decoder 使用公共 sink**

`ImageFileSourceDecoder` 保留现有自然排序和重复路径检查，失败以 page/source diagnostic 返回，不能直接写 `assets/original`。

- [ ] **Step 5: 运行回归和新管线测试**

Run: `ctest --preset windows-msvc-test -R "^manga_tests_data$"`

Expected: PASS，现有图片导入输出语义无变化。

- [ ] **Step 6: 授权后提交**

```powershell
git add src/data/image src/data/import tests/unit/data
git commit -m "refactor(data): share import normalization pipeline"
```

### Task 4: P1-03 批次事务与崩溃恢复

**Files:**
- Create: `src/data/import/PendingBatchWriter.*`
- Modify: `src/data/project/ProjectLayout.h`
- Modify: `src/data/import/FileSystemComicImporter.*`
- Test: `tests/unit/data/PendingBatchWriterTest.*`

**Interfaces:**
- Consumes: `NormalizedPagePayload`、project id/root、batch id、`ProjectIoCoordinator`。
- Produces: `stagePage`、`promoteBatch`、`discardBatch`、`acknowledgeBatch`、`reconcileProject`。

- [ ] **Step 1: 写 fault-injection 测试矩阵**

覆盖创建目录失败、第 N 页写失败、manifest flush 失败、rename 失败、rename 后取消、重复 discard、重复 acknowledge、已采用 manifest 恢复和未引用 pending 清理。

- [ ] **Step 2: 确认测试失败**

Run: `cmake --build --preset windows-msvc-test --target manga_tests_data`

Expected: FAIL，缺少 `PendingBatchWriter`。

- [ ] **Step 3: 实现批次级 writer**

```cpp
class PendingBatchWriter {
public:
    Result<StagedPage> stagePage(const NormalizedPagePayload& page);
    Result<PreparedBatch> promoteBatch();
    Result<void> discardBatch(OperationId batchId);
    Result<void> acknowledgeBatch(OperationId batchId);
    Result<ReconcileReport> reconcileProject(const QSet<QString>& referencedAssets);
};
```

`promoteBatch()` 先 flush/close payload 与 manifest，再从 `.mangatranslator-staging/import-<id>` 一次 rename 到 `assets/original/import-<id>`；不得逐页暴露最终路径。

- [ ] **Step 4: 实现幂等与恢复约束**

相同 batch 已被 discard/ack 时返回成功；batch 身份不匹配返回稳定 protocol error。恢复时“项目已引用”优先于 manifest pending 状态。

- [ ] **Step 5: 运行 fault-injection 测试**

Run: `ctest --preset windows-msvc-test -R "^manga_tests_data$"`

Expected: PASS；每个失败点都没有半个 promoted batch。

- [ ] **Step 6: 授权后提交**

```powershell
git add src/data/import/PendingBatchWriter.* src/data/project/ProjectLayout.h tests/unit/data/PendingBatchWriterTest.*
git commit -m "feat(data): add atomic pending import batches"
```

### Task 5: P1-04 归档扫描与安全 manifest

**Files:**
- Create: `src/data/import/ArchiveScanner.*`
- Create: `tests/unit/data/ArchiveScannerTest.*`
- Create: `tests/unit/data/fixtures/generate_import_fixtures.py`
- Create: `tests/unit/data/fixtures/import/README.md`
- Modify: `tests/unit/data/CMakeLists.txt`

**Interfaces:**
- Consumes: archive path、`ImportPolicySnapshot`、stop token。
- Produces: 有序 `ArchiveManifest`；每项保存原始字节 tie-breaker、安全 locator、ordinal、可信/未知大小元数据，不保存提取路径。

- [ ] **Step 1: 生成独立安全 fixture**

生成并登记 SHA-256：正常 CBZ、空归档、非图片、traversal、绝对/盘符/UNC、空段、深度 33、symlink、hardlink、device、encrypted、nested ZIP、NFKC collision、未知大小、损坏 central directory 和自然排序样本。

- [ ] **Step 2: 写表驱动失败测试**

```cpp
QTest::newRow("traversal") << fixture("zip-slip.cbz") << "import.archive.path_traversal";
QTest::newRow("symlink") << fixture("symlink.cbz") << "import.archive.special_entry";
QTest::newRow("encrypted") << fixture("encrypted.cbz") << "import.archive.encrypted";
```

- [ ] **Step 3: 实现全量结构扫描**

所有 Entry 包括忽略项都计入 10,000 限额。先把 `\\` 和 `/` 统一为归档分隔符，再 UTF-8 严格解码、NFKC、路径验证、冲突检测和自然排序；原始 Entry 名只作为脱敏诊断和最终稳定 tie-breaker。

- [ ] **Step 4: 明确 fatal/recoverable 分界**

路径、类型、加密、嵌套、结构或硬限额违规使整批失败；普通非图片 Entry 只形成 source diagnostic。NFKC 或 case-fold 后冲突整批失败，不允许依赖宿主文件系统大小写规则。

- [ ] **Step 5: 运行 ArchiveScanner 测试**

Run: `ctest --preset windows-msvc-test -R "^manga_tests_data$"`

Expected: PASS；任何不安全 Entry 都未进入 Decoder manifest。

- [ ] **Step 6: 授权后提交**

```powershell
git add src/data/import/ArchiveScanner.* tests/unit/data/ArchiveScannerTest.* tests/unit/data/fixtures/import tests/unit/data/fixtures/generate_import_fixtures.py tests/unit/data/CMakeLists.txt
git commit -m "feat(data): validate archive import manifests"
```

### Task 6: P1-05 受限归档页面解码

**Files:**
- Create: `src/data/import/ArchivePageDecoder.*`
- Create: `tests/unit/data/ArchivePageDecoderTest.*`
- Modify: `tests/unit/data/CMakeLists.txt`

**Interfaces:**
- Consumes: 已批准且有序的 `ArchiveManifest`、policy、内部 spool 根、`PageSink`。
- Produces: 按 manifest ordinal 输出 `DecodedImportPage`；实际解压字节计数与压缩比状态。

- [ ] **Step 1: 写边界测试**

用可注入的小限额覆盖恰好等于/超过单 Entry、累计大小和 100:1；覆盖 header 声明不可信、读到一半取消、page 2 损坏但 page 1/3 成功。

- [ ] **Step 2: 确认测试失败**

Run: `cmake --build --preset windows-msvc-test --target manga_tests_data`

Expected: FAIL，缺少 Decoder。

- [ ] **Step 3: 实现流式 spool 与实际字节计数**

每个 Entry 写入随机生成的内部 spool 名；绝不把 Entry 名拼接成磁盘路径。每次写入前后检查 stop token、单项和累计实际字节；可信压缩/解压大小才执行 ratio 限制。

- [ ] **Step 4: 接入内容探测与 PageNormalizer**

扩展名只用于 safe locator；magic-byte/Qt decode 决定是否为受支持图片。已批准图片解码失败产生 page diagnostic 并继续，存储写入失败使整批失败。

- [ ] **Step 5: 运行测试并检查 spool 清理**

Run: `ctest --preset windows-msvc-test -R "^manga_tests_data$"`

Expected: PASS；成功、失败和取消后均无遗留 spool。

- [ ] **Step 6: 授权后提交**

```powershell
git add src/data/import/ArchivePageDecoder.* tests/unit/data/ArchivePageDecoderTest.* tests/unit/data/CMakeLists.txt
git commit -m "feat(data): decode bounded archive pages"
```

### Task 7: P1-06 PDF worker renderer

**Files:**
- Create: `src/data/import/PdfPageRenderer.*`
- Create: `tests/unit/data/PdfPageRendererTest.*`
- Modify: `tests/unit/data/CMakeLists.txt`

**Interfaces:**
- Consumes: 一个 PDF path、policy、`PageSink`、stop token。
- Produces: PDF 原顺序的 RGBA8/白底 sRGB `DecodedImportPage` 和实际 DPI warning。

- [ ] **Step 1: 写纯尺寸预算测试**

```cpp
QCOMPARE(renderPlan(QSizeF(612, 792), defaultPolicy()).requestedDpi, 300);
QVERIFY(renderPlan(oversizedPage(), defaultPolicy()).actualDpi < 300);
QVERIFY(!renderPlan(impossiblePage(), tinyBudgetPolicy()).has_value());
```

断言任何 `QImage` allocation 发生前已经满足 32 MP、8,192 px 和 512 MiB 同时存活预算。

- [ ] **Step 2: 写 Qt Pdf 集成测试**

覆盖无效/损坏/加密 PDF、旋转、不同页面尺寸、透明页白底合成、页序、单页失败继续和取消。

- [ ] **Step 3: 实现 worker 所有权**

`QPdfDocument` 在执行 decode 的同一 worker context 创建、load、render 和销毁；不得从 GUI 线程创建后移动。等待明确 load status 后再调用 `pageCount()`。

- [ ] **Step 4: 实现确定性降 DPI**

目标像素按 `pagePointSize / 72 * dpi` 计算并保持纵横比；从 300 DPI 向下选择满足全部预算的最高整数 DPI。低于 policy 冻结的最小 DPI 时该页失败，不对其他页改号。

- [ ] **Step 5: 运行 PDF 测试**

Run: `ctest --preset windows-msvc-test -R "^manga_tests_data$"`

Expected: PASS；测试线程记录证明 `QPdfDocument` 从未进入 GUI 线程。

- [ ] **Step 6: 授权后提交**

```powershell
git add src/data/import/PdfPageRenderer.* tests/unit/data/PdfPageRendererTest.* tests/unit/data/CMakeLists.txt
git commit -m "feat(data): render pdf pages within budget"
```

### Task 8: P1-07 统一 Pipeline、发布竞态和单一端口

**Files:**
- Create: `src/data/import/ImportPipeline.*`
- Modify: `src/data/import/FileSystemComicImporter.*`
- Modify: `src/data/CMakeLists.txt`
- Create: `tests/unit/data/ImportPipelineTest.*`
- Modify: `tests/unit/data/FileSystemComicImporterTest.*`

**Interfaces:**
- Consumes: 三个 Decoder、`PageNormalizer`、`PendingBatchWriter`、policy、context。
- Produces: `PreparedImportResult`；阶段进度 `Validating/Scanning/Decoding/Rendering/Promoting`，completed units 和 optional total。

- [ ] **Step 1: 用 Fake Decoder 写状态机测试**

覆盖 all-success、1 good/2 corrupt/3 good、零成功、unknown total、publish 前取消、promotion 前取消、rename 后取消、publication/cancel 两种赢家和 stale generation。

- [ ] **Step 2: 实现 tagged dispatch**

```cpp
return std::visit(overloaded{
    [&](const ImageFilesImportSource& s) { return m_images.decode(makeRequest(s), sink, stop); },
    [&](const ArchiveFileImportSource& s) { return m_archive.decode(makeRequest(s), sink, stop); },
    [&](const PdfFileImportSource& s) { return m_pdf.decode(makeRequest(s), sink, stop); },
}, request.source);
```

- [ ] **Step 3: 实现单一 ownership transfer**

只有所有 page sink 调用结束、批次 promotion 成功、stop/session 二次检查通过后才能发布 `PreparedImportResult`。发布和取消通过同一个受保护状态从 `OwnedByJob` 转到 `OwnedByCaller` 或 `CleaningUp`。

- [ ] **Step 4: 缩减 FileSystemComicImporter**

保留全局线程池调度、异常到 `internal.unexpected_exception` 的收口、`ProjectIoCoordinator` 写票据和对 writer 的 discard/ack 调用；移除格式判断、逐文件最终 rename 和内存 pending map。

- [ ] **Step 5: 运行 data 全套测试**

Run: `ctest --preset windows-msvc-test -R "^manga_tests_data$"`

Expected: PASS；unknown total 不发布伪造百分比，迟到回调不泄漏批次。

- [ ] **Step 6: 授权后提交**

```powershell
git add src/data/import src/data/CMakeLists.txt tests/unit/data
git commit -m "feat(data): orchestrate unified prepared imports"
```

### Task 9: P1-08 Application 最终化与 UI 恢复

**Files:**
- Modify: `src/application/usecase/import/ImportUseCases.h`
- Modify: `tests/unit/application/FakeComicImporter.h`
- Modify: `tests/unit/application/ImportImagesUseCaseTest.*`
- Modify: `src/ui/screen/import/ImportState.h`
- Modify: `src/ui/screen/import/ImportViewModel.*`
- Modify: `src/ui/screen/import/ImportPage.*`
- Modify: `src/ui/screen/import/FileDropZone.*`
- Modify: `src/ui/screen/import/PendingSourcesModel.*`
- Modify: `tests/unit/ui/import_view_model_test.cpp`
- Modify: `tests/unit/ui/import_page_test.cpp`

**Interfaces:**
- Consumes: `PreparedImportResult` 和 explicit recovery actions。
- Produces: accept-all/discard/retry-ack/retry-cleanup UI 意图，以及 `Applying → Saving → Acknowledging` 最终化序列。

- [ ] **Step 1: 写 Application 最终化失败矩阵**

断言 stale/apply failure/save failure 时 Domain 恢复且调用 discard；save success + ack failure 时 Domain 和磁盘项目保持采用并返回 `AdoptedPendingAck`；重复 accept 不追加页面。

- [ ] **Step 2: 实现 save-before-ack UseCase**

```text
validate → snapshot Domain → apply prepared pages → save project
  save failed: restore snapshot → discard batch → report save/cleanup state
  save passed: acknowledge batch
    ack failed: keep Domain and saved project → RetryAcknowledgement
    ack passed: Acknowledged
```

UseCase 负责该顺序；ViewModel 不直接拼接 Adapter 调用。Domain apply 和 Undo/Redo command 都不得执行 I/O。

- [ ] **Step 3: 写 UI 状态测试**

测试图片/文件夹多选可以成批；CBZ/ZIP/PDF 必须单独成批；混合拖放被拒绝。`ReviewRequired` 展示成功/失败数量和“采用成功页面”“取消导入”，不展示逐页 checkbox。

- [ ] **Step 4: 实现 UI 状态映射**

`ImportState` 使用 stable stage 和 optional total；total 未知时显示 indeterminate progress。错误恢复严格来自 DTO enum，不按 error message 字符串分支。

ViewModel 提供的 `ImportProgressCallback` 必须通过 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 回到其所属线程，并用 request token 丢弃迟到进度；worker 不直接写 UI state。

- [ ] **Step 5: 明确 Dirty/Undo 语义**

新建项目向导中的首次导入在保存成功后进入编辑器，`dirty=false` 且不产生可见 Undo 项。未来从编辑器追加页面时复用同一 prepared result，并由一个复合 `AddPagesCommand` 设置 `dirty=true`；该接线属于 V2-P5，本阶段只保留可复用 Application 合同。

- [ ] **Step 6: 运行 Application/UI 测试**

Run: `ctest --preset windows-msvc-test -R "manga_tests_(application|ui)"`

Expected: PASS；ack failure 不释放已保存资产，cleanup failure 可重试且 Domain 不变。

- [ ] **Step 7: 授权后提交**

```powershell
git add src/application src/ui/screen/import tests/unit/application tests/unit/ui
git commit -m "feat(ui): review and finalize prepared imports"
```

### Task 10: P1-09 组合根、端到端安全与完成证据

**Files:**
- Modify: `src/app/ApplicationContext.*`
- Create: `tests/integration/ImporterIntegrationTest.cpp`
- Modify: `tests/integration/CMakeLists.txt`
- Modify: `tests/unit/data/fixtures/import/README.md`
- Modify: `docs/plan/v0.2/phase-1-importers.md`

**Interfaces:**
- Consumes: 所有 Decoder、Pipeline、writer、Application UseCase。
- Produces: 生产组合、离线 E2E 证据和阶段退出判定。

- [ ] **Step 1: 在唯一组合根构造依赖**

构造顺序为 writer → normalizer → image/archive/pdf decoder → pipeline → `FileSystemComicImporter`；UI 仍只拿 `IComicImporter*`。

- [ ] **Step 2: 写 E2E 场景**

覆盖真实 CBZ 与 PDF 的导入、部分失败采用、整批丢弃、取消、磁盘失败、保存失败、ack 重试、项目重开顺序、启动恢复、输入 SHA-256/时间戳不变和所有提交引用均为相对路径。

- [ ] **Step 3: 运行格式和全量构建**

Run: `clang-format -i`，仅传入本阶段修改的 C/C++ 文件。

Run: `cmake --preset windows-msvc-test`

Run: `cmake --build --preset windows-msvc-test --parallel`

Expected: 全部 target 构建成功。

- [ ] **Step 4: 运行完整测试**

Run: `ctest --preset windows-msvc-test`

Expected: 0 failed；普通运行不访问网络。

- [ ] **Step 5: 运行仓库检查**

Run: `prek run --all-files`

Run: `git diff --check`

Expected: hooks 和 whitespace 检查均通过；Git 中没有 build、缓存、临时 staging、用户内容或绝对本机路径。

- [ ] **Step 6: 记录完成证据**

在本节末尾附上实际执行的 configure/build/ctest/prek 命令、fixture SHA-256、许可证、峰值内存、取消注入点、故障注入点和 project round-trip 结果；证据齐全后把状态从 `Planned` 改为 `Completed`。

- [ ] **Step 7: 授权后提交**

```powershell
git add src/app tests/integration tests/unit/data/fixtures/import docs/plan/v0.2/phase-1-importers.md
git commit -m "test(data): verify archive and pdf import lifecycle"
```

---

## 4. 阶段退出条件

- P1-00 证明 V2-P0 已冻结 tagged source、policy、错误、进度、ownership、idempotency 和批次相对路径；P1 没有私造第二套 DTO。
- ZIP Slip、绝对/盘符/UNC、特殊 Entry、加密、嵌套、NFKC 冲突和资源炸弹均在 page decode 前或有界读取中失败关闭。
- 实际流式字节而非只读 header 决定 256 MiB、2 GiB 和压缩比限制；未知总量只显示阶段与单位计数。
- PDF 在分配 render target 前计算尺寸和 512 MiB 工作集预算，保持纵横比，按最高安全 DPI 渲染且不阻塞 GUI。
- 图片、CBZ/ZIP、PDF 通过同一 prepared-batch 合同生成稳定页面顺序和相对资产路径。
- 部分失败只发布一个包含所有成功页和有序诊断的 pending batch；采用一次或整批丢弃，不重复解码或写文件。
- 取消/stale 在发布前获胜时没有引用或残留；发布后取消通过显式 discard 解决。
- Domain apply/save 失败不改变项目并清理 pending；save 后 acknowledge 失败绝不删除已引用资产。
- discard、acknowledge 和 accept 重试均幂等；崩溃恢复不会删除项目已引用资产。
- 输入 CBZ/ZIP/PDF 的内容 hash 和时间戳不变；诊断脱敏；普通测试离线且无用户漫画。
- Light/Dark、正常/窄宽度下，导入选择、indeterminate progress、partial review 和恢复动作可读且操作唯一。

## 5. 完成证据模板

```text
Configure:
Build:
Tests:
Hooks:
Fixtures and licenses:
Peak PDF working set:
Cancellation injection points:
Fault injection points:
Project round-trip:
Unverified boundaries:
```

只有实际执行后才能填写以上字段；未执行项必须保留在 `Unverified boundaries`，不能以计划命令代替验证结果。
