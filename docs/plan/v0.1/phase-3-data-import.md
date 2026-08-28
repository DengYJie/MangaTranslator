# P3：数据持久化与图片导入

> 状态：Planned
> 前置阶段：P2
> 可与 P4、P5 并行
> 输出：可移植、可恢复的目录型项目和 V0.1 图片导入

## 目标

实现 `IProjectRepository` 与 `IComicImporter`，保证保存失败不破坏项目，源图始终不可变。

## TODO

### 项目仓储

- [ ] 创建 `project.json`、`assets/original/`、`assets/derived/`。
- [ ] 默认复制源图，仅保存项目根目录内的相对路径。
- [ ] 保存前执行结构和语义校验。
- [ ] 在同文件系统写入临时文件，完成同步与校验后原子替换。
- [ ] 维护 `.bak`，定义当前文件、临时文件和备份文件的恢复优先级。
- [ ] 实现打开、另存、保存冲突、磁盘空间不足和只读目录错误。
- [ ] 禁止把绝对本机路径写入项目。

### 图片导入

- [ ] 支持 JPG/JPEG、PNG、WEBP 和图片文件夹。
- [ ] 校验真实图片内容，不能只依赖扩展名。
- [ ] 对文件夹内容执行 Unicode NFKC 和稳定自然数字排序。
- [ ] 处理 Unicode 路径、重复名称、损坏文件和部分导入失败。
- [ ] 导入时应用 EXIF 方向。
- [ ] 将工作图转换为 RGBA8、straight alpha、sRGB，同时保留原始文件。
- [ ] 为页面生成稳定 UUID 和项目内唯一资产名。

### 派生资产

- [ ] 定义清除层、译文预览和缩略图的版本化相对引用。
- [ ] 暂存写入成功后再更新 `project.json` 引用。
- [ ] 损坏或缺失的可重建派生资产可明确失效并重新生成。

## 测试

- [ ] 保存、重开、另存及备份恢复往返。
- [ ] 保存期间中断、磁盘不足、拒绝写入和残留临时文件。
- [ ] Unicode/NFKC、自然排序和相同规范化名称的稳定次序。
- [ ] 损坏、伪装扩展名、高位深、ICC、EXIF 和透明图片。
- [ ] 源图哈希在导入、保存、编辑和导出后保持不变。
- [ ] 第二实例打开同一项目被 `persistence.locked` 拒绝；崩溃残留锁可被识别清除。
- [ ] 部分导入失败：成功页面保持确定性顺序，失败项逐条携带原因。
- [ ] 重复导入相同文件始终生成新 Page 与新资产，不触碰已有 Region。
- [ ] 页面图像加载与导入使用同一规范化输出（EXIF、ICC、RGBA8）。
- [ ] SaveAs 完整切换后旧目录原样保留。

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
};
```

- `CreateProjectRequest`：目标目录、项目名、语言对、阅读方向；创建 `project.json`、`assets/original/`、`assets/derived/` 并写入初始合法项目。
- `closeProject()` 只释放锁、文件句柄和 Repository 运行时资源；不修改 Domain，不写盘。
- 恢复来源为 `Backup` 时项目只加载进内存并附带警告，不自动覆盖 `project.json`；UI 提示用户保存以确认恢复结果。
- 保存成功后由应用层清除 dirty 标记；不清空 Undo 历史。

错误映射（data 层内部类型进入 Port 前完成）：

```text
ValidationError（结构/语义/版本）→ ErrorCategory::Project
OS 与文件系统错误              → ErrorCategory::Persistence
```

稳定错误码：

```text
project.invalidStructure
project.invalidSemantic
project.unsupportedSchemaVersion
project.unsupportedFormatVersion

persistence.notFound
persistence.permissionDenied
persistence.readOnly
persistence.diskFull
persistence.io
persistence.atomicCommitFailed
persistence.backupFailed
persistence.locked
persistence.recoveryFailed
```

序列化器现有的 `std::expected<T, ValidationError>` 是 data 层内部返回类型，由 Repository 在 Port 边界映射为 `ApplicationError`。第三方与标准库异常在适配器内捕获，不穿越 Port。

### 保存事务与恢复

```text
serialize
→ 语义校验
→ 现有 project.json 存在则复制为 project.json.bak（单代）
→ QSaveFile 写入 project.json（同目录临时文件，commit 原子替换）
```

- 选用 `QSaveFile`，不直接调用 Win32 API；`directWriteFallback` 保持 false，不牺牲原子性。
- 不引入 `.pending` 恢复层：`QSaveFile` 本身就是同文件系统临时写入、失败即清理、崩溃时当前文件保持完整；追加一层只增加写盘开销，不增加安全性。恢复链保持两级：

```text
project.json 有效 → 使用 current
current 损坏     → project.json.bak 有效 → 从 bak 打开
全部无效         → open 失败
```

- `.bak` 单代，每次成功保存前轮转；`QSaveFile` 自身的临时文件永远不是恢复候选。

### SaveAs

```text
完整复制到新根目录：project.json + assets/original/ + assets/derived/
→ 全部写入成功后：释放旧目录锁，对新目录加锁，返回包含新 path 的 ProjectDescriptor
→ 旧项目目录原样保留
```

- 新目录写入失败时会话仍指向旧目录，旧目录不被破坏。
- 锁切换失败则 SaveAs 整体失败。

### 图片导入管线

归属 `data/import/`：外部文件到规范化资产是外部表示映射，不是本地计算引擎；架构文档已将导入器划入 data。

```text
文件 → magic bytes 探测 → 解码 → EXIF 方向 → ICC → sRGB → RGBA8 straight alpha
→ 复制原始文件到 assets/original/（字节不变）→ 创建 ComicPage（尺寸取规范化后工作图）
```

- 格式白名单按文件头：PNG signature、JPEG SOI、RIFF+WEBP。
- 内容是白名单格式即接受，扩展名不符附带警告；内容不是白名单格式即拒绝，即使扩展名正确。
- 规范化（EXIF/ICC/RGBA8）是共享的内存中工具，导入与页面加载复用；原始文件保持字节不变，源图哈希测试依赖此点。

导入 DTO：

```cpp
struct ImportImagesRequest {
    ProjectId projectId;
    QVector<QString> filePaths;    // Unicode 路径
};

struct ImportedPage {
    PageId pageId;
    QString assetPath;
    int orderIndex;
};

struct ImportFailure {
    QString path;
    ApplicationError error;
};

struct ImportImagesResult {
    QVector<ImportedPage> importedPages;
    QVector<ImportFailure> failures;
};
```

- 部分失败：成功页面确定性进入项目，失败项逐条携带原因。
- 排序：目录内 NFKC + 数字自然序（`1, 2, 10`）。
- 资产命名沿用 P1 冻结的 `page_NNNN.<ext>` 导入序号命名：名称在导入时分配，永不随页面重排改变（重排只改 `orderIndex`）；序号计数器按项目维护，生成式命名不与源文件名耦合，无命名冲突。
- 重复导入永远作为新 Page：新 PageId、新资产名、新序号；即使源文件哈希相同也不替换已有页面，避免破坏已有 Region。

### 页面图像加载

独立 Port，不并入 `IProjectRepository`：

```cpp
struct LoadPageImageRequest {
    ProjectId projectId;
    PageId pageId;
    QString assetPath;    // 项目根相对路径
};

virtual QFuture<Result<QImage>> loadPageImage(LoadPageImageRequest, OperationContext) = 0;
```

- 加载即规范化：读取原始资产，应用与导入相同的 EXIF/ICC/RGBA8 转换，返回 P2 约定的解码工作图。
- `QImage` 在工作线程构建；`QPixmap` 与 Scene 只在 GUI 线程创建。
- 加载器不保证持久缓存；缓存属于可选实现优化，不进入 P3 契约。

### 派生资产

```text
assets/derived/
└── <page-uuid>/
    ├── work/v1.png
    ├── thumbnail/v1.png
    ├── cleaning/v1.png
    └── preview/v1.png
```

- 版本号写入路径段：生成器升级时写 `v2` 并清理旧目录；`project.json` 引用保持 P1 冻结的纯相对路径字符串，Schema 的 `layers` 形状不变。
- 失效判据：文件不存在或无法解码；派生资产可安全丢弃，不作为用户数据唯一副本。

### 多实例并发

- `create`/`open` 时在项目根创建 `.mangatranslator.lock`（`QLockFile`），`tryLock(0)` 非阻塞获取。
- 长持有文档锁模式：`staleLockTime` 设为 0 禁用时长判定，仅保留持有进程存活判定；崩溃实例的残留锁据此识别为 stale 并清除。
- 获取失败：拒绝打开，错误码 `persistence.locked`，safeMessage 说明项目已被另一实例打开。
- V0.1 不做第二实例只读打开、多人并发或合并。
- `saveAs` 成功后锁随根目录切换。
