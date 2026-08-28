# P3：数据持久化与图片导入

> 状态：Planned
> 前置阶段：P2
> 可与 P4、P5 并行
> 输出：可移植、可恢复的目录型项目和 V0.1 图片导入

## 目标

实现 `IProjectRepository`、`IComicImporter` 与 `IPageImageLoader`，保证保存失败不破坏项目，源图始终不可变。

## TODO

### 项目仓储

- [ ] 创建 `project.json`、`assets/original/`、`assets/derived/`。
- [ ] 默认复制源图，仅保存项目根目录内的相对路径。
- [ ] 保存前执行结构和语义校验。
- [ ] 在同文件系统写入临时文件，完成同步与校验后原子替换。
- [ ] 维护 `.bak`，定义当前文件、临时文件和备份文件的恢复优先级。
- [ ] 实现打开、另存、保存冲突、磁盘空间不足和只读目录错误。
- [ ] 禁止把绝对本机路径写入项目。
- [ ] 以项目 revision 绑定保存快照；旧快照完成时不得把更新后的项目标记为 Clean。
- [ ] 同一项目的 create/save/saveAs/import/close 串行执行，加载操作持有项目会话租约。
- [ ] 打开时区分缺失原图与可丢弃派生资产，并校验所有资产引用仍位于项目根目录内。

### 图片导入

- [ ] 支持 JPG/JPEG、PNG、WEBP 和图片文件夹。
- [ ] 校验真实图片内容，不能只依赖扩展名。
- [ ] UseCase 负责顶层来源规范化、重复拒绝和自然排序；Importer 展开文件夹（仅顶层）并对目录项使用同一排序规则。
- [ ] 处理 Unicode 路径、重复名称、损坏文件和部分导入失败。
- [ ] 导入时应用 EXIF 方向。
- [ ] 将工作图转换为 RGBA8、straight alpha、sRGB，同时保留原始文件。
- [ ] 为页面生成稳定 UUID 和项目内唯一资产名（扫描目录取 max+1）。
- [ ] 每个文件通过独立暂存事务提交；失败或取消不得留下被误认为成功的资产。
- [ ] 返回完整 `ComicPage`、来源路径和 warning；由 `ApplyImportResultUseCase` 在 GUI 线程原子加入项目。

### 派生资产

- [ ] 定义清除层、译文预览和缩略图的版本化相对引用。
- [ ] 暂存写入成功后再更新 `project.json` 引用。
- [ ] 损坏或缺失的可重建派生资产可明确判定为失效；P3 不负责后台重新生成。

## 测试

- [ ] 保存、重开、另存及备份恢复往返。
- [ ] 保存期间中断、磁盘不足、拒绝写入和残留临时文件。
- [ ] Unicode/NFKC、自然排序、同请求重复拒绝和后续重复导入。
- [ ] 损坏、伪装扩展名、高位深、ICC、EXIF 和透明图片。
- [ ] 测试夹具使用手工构造的最小二进制基线文件（EXIF JPEG、ICC PNG、16-bit PNG、动画 WEBP、CMYK JPEG、伪装扩展名），每个几百字节；QImage 无法生成带元数据的样本。
- [ ] 源图哈希在导入、保存、编辑和导出后保持不变。
- [ ] 第二实例打开同一项目被 `persistence.locked` 拒绝；崩溃残留锁可被识别清除。
- [ ] 部分导入失败：成功页面保持确定性顺序，失败项逐条携带原因。
- [ ] 重复导入相同文件始终生成新 Page 与新资产，不触碰已有 Region。
- [ ] 页面图像加载与导入使用同一规范化输出（EXIF、ICC、RGBA8）。
- [ ] SaveAs 完整切换后旧目录原样保留。
- [ ] 保存期间继续编辑时，旧保存结果不清除新修改的 Dirty 状态。
- [ ] save/import/saveAs/close 并发请求按项目串行，资产序号不冲突。
- [ ] 绝对路径、`..`、符号链接、junction/reparse point 和项目根逃逸均被拒绝。
- [ ] 导入在预检、暂存复制、解码和文件间检查取消；取消后的清理范围可验证。
- [ ] warning 可到达 Application DTO；错误码、RecoveryAction 和 diagnostic 不泄露本机路径或用户内容。
- [ ] WEBP 插件在测试配置中可用；缺失时配置或测试明确失败。

## 退出条件

- 项目重新打开后页面顺序及全部用户编辑一致。
- 任何保存错误都不破坏最后一次有效项目与源图。
- 导入部分失败有完整汇总，成功页面仍保持确定性顺序。

## 注意事项

- V0.1 不解析 CBZ/ZIP、PDF、CBR、CB7 或 EPUB。
- 不把缩略图、缓存和派生图层误当用户数据唯一副本。
- 文件操作必须使用明确路径并防止资产名穿越项目目录。

## 设计规格参考

### IProjectRepository 契约

```cpp
virtual QFuture<Result<ProjectDescriptor>> createProject(CreateProjectRequest, OperationContext) = 0;
virtual QFuture<Result<OpenProjectResult>> openProject(OpenProjectRequest, OperationContext) = 0;
virtual QFuture<Result<OpenProjectResult>> recoverProject(OpenProjectRequest, OperationContext) = 0;
virtual QFuture<Result<void>> saveProject(SaveProjectRequest, OperationContext) = 0;
virtual QFuture<Result<ProjectDescriptor>> saveProjectAs(SaveAsProjectRequest, OperationContext) = 0;
virtual QFuture<Result<void>> closeProject(CloseProjectRequest, OperationContext) = 0;
```

```cpp
struct ProjectDescriptor {
    ProjectId id;
    QString name;
    QString path;      // 运行期绝对路径，禁止写入项目文件
};

struct OpenProjectResult {
    domain::ComicProject project;
    RecoverySource recoverySource;   // Current / Backup
    QVector<ProjectWarning> warnings;
};
```

- `openProject` 只读 `project.json`（current）；损坏即返回 `persistence.recovery_failed`，不自动回退。
- `recoverProject` 读 `.bak` 并标记 `RecoverySource::Backup`；由 UI 在收到 `persistence.recovery_failed` 后显式调用。
- `createProject`：目标目录必须为空或不存在；已有 `project.json` 或非空目录返回 `persistence.target_not_empty`。
- `closeProject()` 只释放锁、文件句柄和 Repository 运行时资源；不修改 Domain，不写盘。
- 恢复来源为 `Backup` 时项目只加载进内存，并通过 `recoverySource` 和 `warnings` 通知 UI；不自动覆盖 `project.json`。
- `ProjectWarning` 只携带稳定 code、安全摘要和项目内相对路径；不得包含绝对本机路径或用户文本。
- Repository 按 `ProjectId` 持有运行期 `ProjectDescriptor`、锁和规范化根目录；未知或已关闭的 ProjectId 返回 `project.not_open`。
- 调用保存前捕获 `savedRevision = request.project.revision()`；成功后仅调用 `currentProject.markCleanIfRevision(savedRevision)`。旧快照保存成功不得清除之后产生的新 Dirty 修改，也不清空 Undo 历史。

错误映射（data 层内部类型进入 Port 前完成）：

```text
ValidationError（结构/语义/版本）→ ErrorCategory::Project
OS 与文件系统错误              → ErrorCategory::Persistence
```

稳定错误码：

```text
input.empty_import
input.duplicate_import_path
input.invalid_import_request
input.invalid_import_cleanup
input.invalid_image_request
input.image_too_large

project.invalid_structure
project.invalid_semantic
project.unsupported_schema_version
project.unsupported_format_version
project.missing_asset
project.not_open

persistence.not_found
persistence.permission_denied
persistence.read_only
persistence.disk_full
persistence.io
persistence.atomic_commit_failed
persistence.backup_failed
persistence.locked
persistence.recovery_failed
persistence.target_not_empty
persistence.save_conflict
```

序列化器现有的 `std::expected<T, ValidationError>` 是 data 层内部返回类型，由 Repository 在 Port 边界映射为 `ApplicationError`。第三方与标准库异常在适配器内捕获，不穿越 Port。

### 保存事务与恢复

```text
serialize
→ Schema + 版本 + 语义校验
→ 校验 project.json 指纹仍与打开/上次保存时一致，否则 persistence.save_conflict
→ 取消检查点（事务开始前最后一次；事务开始后忽略 stopToken）
→ 现有 project.json 完整有效时，通过以 project.json.bak 为目标的 QSaveFile 原子轮转单代备份
   （从 Backup 恢复后的首次保存：current 损坏，跳过轮转，直接 commit）
→ QSaveFile 写入 project.json（同目录临时文件，commit 原子替换）
→ 更新会话内文件指纹
```

- 选用 `QSaveFile`，不直接调用 Win32 API；`directWriteFallback` 保持 false，不牺牲原子性。
- **`.bak` 轮转条件**：仅当 current 通过完整 Schema、版本和语义校验时才轮转；备份写入失败保留旧 `.bak` 并终止保存。从 Backup 恢复后首次保存跳过轮转，以保留唯一好副本。
- **取消边界**：save/saveAs 的取消检查点只在 serialize + validate 阶段；事务开始后（bak 轮转 → QSaveFile commit）忽略 `stopToken`，保证原子性不被中断。
- 不引入 `.pending` 恢复层：`QSaveFile` 本身就是同文件系统临时写入、失败即清理、崩溃时当前文件保持完整。恢复链保持两级：

```text
project.json 有效 → openProject 成功（RecoverySource::Current）
current 损坏     → openProject 返回 persistence.recovery_failed
                 → UI 调用 recoverProject → 读 .bak（RecoverySource::Backup）
全部无效         → recoverProject 也失败
```

- `.bak` 单代；`QSaveFile` 自身的临时文件永远不是恢复候选。
- 不扫描或删除 Qt 私有临时文件命名。需要显式暂存的批量操作只使用应用自有 staging 目录，并记录本次创建的精确路径用于失败清理。
- 会话保存 `project.json` 的大小、最后修改时间和 SHA-256；任一变化都重新计算并比较内容指纹，防止外部程序绕过 `QLockFile` 后静默覆盖。

### 打开与资产校验

- 打开顺序固定为：锁定项目根 → 读取 current/backup → envelope 校验 → 迁移 → 完整 Schema 校验 → 构建 Domain → 语义校验 → 资产引用校验 → `markClean()`。
- `originalAsset` 必须是项目根内的普通可读文件；缺失、目录、根逃逸或链接重定向均以 `project.missing_asset`/`project.invalid_semantic` 拒绝打开。
- 派生资产缺失或损坏不阻止打开；对应引用在内存中标记失效并产生 `ProjectWarning`，但不得静默写回 `project.json`。
- 所有相对引用先拒绝绝对路径和 `..`，再以 canonical 项目根做包含性校验。Windows junction、symlink 和其他 reparse point 解析后的目标也必须位于项目根。
- `recoverProject` 与前一次失败的 `openProject` 使用同一 Repository 会话锁；恢复完成、明确关闭或操作放弃前不得出现无锁窗口。

### SaveAs

```text
newPath 必须为空目录或不存在；非空返回 persistence.target_not_empty
→ 记录该操作是否创建了目标目录
→ 在新根目录获取目标锁，旧项目锁继续持有
→ 可取消地复制到应用自有 staging：project.json + assets/original/ + assets/derived/
   （排除 .mangatranslator.lock，它是运行时状态）
→ 校验 staging 完整性并提交到新根目录
→ 原子切换 Repository 会话 descriptor/path
→ 最后释放旧目录锁，返回包含新 path 的 ProjectDescriptor
→ 旧项目目录原样保留
```

- `newPath` 是项目根目录路径；ProjectId 不变（同一项目换位置）；成功后 dirty 清零。
- 复制阶段每个文件前后检查取消；只有最终提交和会话切换不可取消。
- 失败时只清理本次操作明确创建的 staging 和空目标目录，绝不递归删除调用前已存在的目录；清理失败只进 diagnostic。
- 会话仍指向旧目录，旧目录不被破坏。
- 目标锁、复制、校验或提交失败均保持旧锁和旧会话不变。

### 进程内并发与会话生命周期

- `data` 内部使用由 `ApplicationContext` 注入给 Repository、Importer 和 Loader 的项目 I/O 协调器；它不是全局单例，也不进入 Application Port。
- create/save/saveAs/import/close 按 canonical 项目根串行；页面加载可并发读取，但必须持有会话租约，close 等待现有租约释放且不接受新操作。
- 资产序号扫描与最终存在性检查位于同一写临界区；即使发生意外冲突也继续取下一个序号，绝不覆盖。
- `QLockFile` 负责进程间互斥，项目 I/O 协调器负责同一进程内互斥；两者不可互相替代。

### 图片导入管线

归属 `data/import/`：外部文件到规范化资产是外部表示映射，不是本地计算引擎；架构文档已将导入器划入 data。

**职责分工**：`ImportImagesUseCase` 规范化并自然排序顶层来源，拒绝同一请求内 NFKC/case-fold 后重复的路径；`IComicImporter` 展开文件夹（仅顶层普通文件，不递归），目录项使用同一自然排序规则。直接文件与文件夹展开结果按已排序顶层来源的位置依次展开。

- 同一文件同时被直接选择和由文件夹展开命中时，在任何写盘前以 `input.duplicate_import_path` 拒绝整次请求。
- 不同请求再次导入同一源文件是合法的新导入，生成新 PageId 与资产名。
- `projectRoot` 是 `ProjectDescriptor.path` 的运行期绝对路径副本，只用于本次 I/O，不进入 `project.json`；Adapter 必须 canonicalize 后再执行包含性校验。

```text
单次打开源文件并流式复制到项目根内 staging，同时计算 SHA-256
→ 对 staging 副本执行 magic bytes 探测和 QImageReader header 预检
→ 解码并应用一次 EXIF 方向 → ICC 转 sRGB → RGBA8 straight alpha
→ 构造 ComicPage（尺寸取规范化后工作图）
→ 将原始字节 staging 原子提交到 assets/original/
```

- 格式白名单按文件头：PNG signature、JPEG SOI、RIFF+WEBP；最终扩展名由探测结果规范为 `.png`、`.jpg` 或 `.webp`，不信任源扩展名。
- 内容是白名单格式即接受，扩展名不符附带警告；内容不是白名单格式即拒绝，即使扩展名正确。
- 规范化（EXIF/ICC/RGBA8）是共享的内存中工具，导入与页面加载复用；原始文件保持字节不变，源图哈希测试依赖此点。
- **V0.1 导入不写 work/v1.png**；加载时从原图即时规范化。work/ 留给未来缓存优化。

解码角落规则：

- `QImageReader::setAutoTransform(true)` 只设置一次；规范化后的最终尺寸是 Domain `imageSize`，不得再次应用 EXIF。
- 最终工作图固定为非 premultiplied `QImage::Format_RGBA8888` 且 `QColorSpace::SRgb`。无 ICC 时按 sRGB 解释；无效 ICC 附带 `import.invalid_color_profile_assumed_srgb` warning 后按 sRGB 解释。
- CMYK JPEG：解码成功后显式转 sRGB，附带 warning（`import.cmyk_converted`）。
- 动画 WEBP：取第一帧，附带 warning（`import.animated_webp_first_frame`）。
- 尺寸上限：宽或高 > 16384 px 拒绝，`ErrorCategory::Input`，code `input.image_too_large`。
- 内存上限：解码后 RGBA8 体积 > 256 MB 拒绝，同上。
- 在分配前使用 header 尺寸进行溢出安全的 `width × height × 4` 预检；EXIF 变换和最终转换后再次检查实际尺寸与字节数。
- 测试启动时断言 `QImageReader::supportedImageFormats()` 包含 WEBP；缺失插件直接使 P3 测试失败，P7 负责发布包部署验收。

导入 DTO：

```cpp
struct ImportImagesRequest {
    ProjectId projectId;
    QString projectRoot;           // 运行期绝对路径，不持久化
    QVector<QString> filePaths;    // 已排序顶层来源，可含文件夹
};

struct ImportWarning {
    QString path;
    QString code;
    QString safeMessage;
};

struct ImportedPage {
    ComicPage page;
    QString sourcePath;
    QVector<ImportWarning> warnings;
};

struct ImportFailure {
    QString path;
    ApplicationError error;
};

struct ImportImagesResult {
    QVector<ImportedPage> importedPages;
    QVector<ImportFailure> failures;   // 按输入顺序排列
};

struct DiscardImportedAssetsRequest {
    ProjectId projectId;
    QString projectRoot;
    QVector<QString> assetPaths;       // 仅允许 assets/original/ 下本次返回的精确路径
};
```

- 空 `filePaths` → `ErrorCategory::Input`，code `input.empty_import`。
- 部分失败：成功页面确定性进入项目，失败项逐条携带原因，按输入顺序排列。
- 全部失败 → `Result` 成功但 `importedPages` 为空（部分失败语义一致性）。
- 资产命名沿用 P1 冻结的 `page_NNNN.<ext>` 导入序号命名：序号通过扫描 `assets/original/` 现有最大序号 +1 确定（不加 Schema 字段），存在性检查防冲突；名称在导入时分配，永不随页面重排改变。
- 文件级失败在提交最终资产前删除 staging；成功页由 `ApplyImportResultUseCase` 使用项目快照原子加入 Domain，`ComicProject::addPage` 从当前页面数开始重新分配连续 orderIndex。
- 请求取消时回滚本次请求已提交但尚未 Apply 的全部资产并返回 `Cancelled`，不返回部分成功；清理仅针对内存中记录的精确生成路径。
- Apply 因项目/generation 变化或协议错误被拒绝时，调用 `DiscardImportedAssetsUseCase`；Adapter 再次校验 canonical 包含关系和 `assets/original/` 前缀后删除这些未采用资产。
- warning 随对应 `ImportedPage` 返回；failure 与 warning 都保持输入顺序，不把绝对路径或完整用户内容写入 safeMessage/diagnostic。

### 页面图像加载

独立 Port，不并入 `IProjectRepository`：

```cpp
struct LoadPageImageRequest {
    ProjectId projectId;
    QString projectRoot;    // 运行期绝对路径，不持久化
    PageId pageId;
    QString assetPath;    // 项目根相对路径
};

virtual QFuture<Result<QImage>> loadPageImage(LoadPageImageRequest, OperationContext) = 0;
```

- 加载即规范化：读取原始资产，应用与导入相同的 EXIF/ICC/RGBA8 转换，返回 P2 约定的解码工作图。
- `QImage` 在工作线程构建；`QPixmap` 与 Scene 只在 GUI 线程创建。
- 加载器不保证持久缓存；缓存属于可选实现优化，不进入 P3 契约。
- Loader 使用与 Importer 完全相同的 header 预检、单次 EXIF、ICC 和最终格式函数；不得维护第二套规范化实现。
- `projectRoot` 和 `assetPath` 经 canonical 包含性校验后才能打开；PageId 用于结果身份校验，不用于拼接文件名。
- 文件不存在、不可读、内容损坏、尺寸与 Domain `imageSize` 不一致分别映射稳定错误码；缺失原图不可用派生资产替代。
- 解码前后检查 `stopToken`；已开始的单次 Qt 解码不能强制中止时，完成后丢弃图像并返回 `Cancelled`。

### 派生资产

```text
assets/derived/
└── <page-uuid>/
    ├── work/v1.png          ← V0.1 不写入，加载时即时规范化
    ├── thumbnail/v1.png     ← 约定路径，不进 project.json
    ├── cleaning/v1.png      ← 进 project.json layers.clear
    └── preview/v1.png       ← 约定路径，不进 project.json
```

- **清除层/译文层/人工覆盖层**：引用写入 `project.json` 的 `layers` 对象（P1 Schema 已支持 `clear`/`translation`/`manualOverride` 字符串字段）。
- **缩略图/work/preview**：按约定路径查找，不进 `project.json`；缺失或损坏即视为 stale。后台生成与重建由后续阶段的派生资产服务负责。
- 版本号写入路径段：生成器升级时写 `v2` 并清理旧版本文件。
- 失效判据：文件不存在或无法解码；派生资产可安全丢弃，不作为用户数据唯一副本。

### 多实例并发

- `create`/`open`/`recover` 时在项目根创建 `.mangatranslator.lock`（`QLockFile`），`tryLock(0)` 非阻塞获取。
- 长持有文档锁模式：`staleLockTime` 设为 0 禁用时长判定，仅保留持有进程存活判定；崩溃实例的残留锁据此识别为 stale 并清除。
- 获取失败：拒绝打开，错误码 `persistence.locked`，safeMessage 说明项目已被另一实例打开。
- 同一进程重复打开相同 canonical 根目录返回已有会话或明确的 `persistence.locked`，不得创建第二套锁和路径映射。
- `openProject` 因 current 损坏返回恢复建议时保留锁；随后 `recoverProject` 复用该锁。用户放弃恢复时显式关闭失败会话。
- V0.1 不做第二实例只读打开、多人并发或合并。
- `saveAs` 获取新锁并完成会话切换后才释放旧锁；全量复制时排除 `.mangatranslator.lock`（运行时状态，不是项目数据）。
