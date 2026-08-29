# MangaTranslator 技术决策

> 状态：V0.1 基线已锁定
> 适用范围：V0.1；后续版本的决定在进入对应版本前补充
> 关联文档：[架构设计](architecture-design.md)、[功能设计](feature-design.md)、[产品路线图](roadmap.md)、[V0.2 技术决策](v0.2-technical-decisions.md)、[V0.3 技术决策](v0.3-technical-decisions.md)

## 1. V0.1 交付边界

V0.1 只验证核心人工辅助编辑闭环：

```text
图片/文件夹导入
→ 目录型项目保存
→ 矩形 Region 编辑
→ Manga OCR 或人工录入
→ OpenAI-compatible 非流式翻译
→ 纯色清除层
→ 基础横排/竖排
→ Undo/Redo
→ PNG/JPEG 导出
```

CBZ、PDF、自动文字检测、复杂 Inpaint、流式 LLM、完整日文排版、旋转 Region 和多边形 Region 不进入 V0.1。

## 2. 项目与数据

### 2.1 项目格式

项目使用目录结构：

```text
ProjectName/
├── project.json
└── assets/
    ├── original/
    └── derived/
```

- 默认复制源图到 `assets/original/`，保证项目可移植。
- `project.json` 只保存项目根目录下的相对路径。
- 派生图层与可重建结果放入 `assets/derived/`，不得覆盖原图。
- 保存先写入同文件系统的临时位置，校验成功后原子替换目标。
- 保留 `.bak` 恢复文件；恢复流程必须区分当前文件、临时文件和备份文件。

### 2.2 JSON Schema

- 使用 JSON Schema Draft 2020-12。
- 根对象至少包含 `projectVersion`、`schemaVersion` 和 `formatVersion`。
- 打开项目时执行结构校验，再进入语义校验和迁移。
- 未知的较新主版本不得静默降级保存。
- Schema 文件随应用发布并纳入项目格式测试。

三个版本字段的职责：

- `projectVersion`：该项目内容的保存修订信息。
- `schemaVersion`：JSON 结构版本。
- `formatVersion`：项目整体格式及资产布局版本。

### 2.3 稳定 ID

- Page、Region 和需要跨保存引用的实体使用 UUID v4。
- JSON 中序列化为带连字符的小写标准字符串。
- 保存、重开、导出和重跑任务不改变 ID。
- 复制整个项目时保留 ID。
- 新导入或项目合并出现 ID 冲突时，为冲突实体生成新 ID，并在本次操作中维护旧 ID 到新 ID 的映射。

### 2.4 Region 几何

- V0.1 只持久化轴对齐矩形。
- Schema 保留 `geometry.type`，V0.1 唯一合法值为 `rect`。
- `x`、`y`、`width`、`height` 使用源图像像素坐标和 double。
- 最小尺寸为 2×2 源图像像素。
- 创建时越界则拒绝；编辑后的结果裁剪到页面边界。
- V0.1 不支持旋转和多边形。
- Region 保存 `orderIndex`；独立阅读顺序编辑工具推迟到 V0.2。

### 2.5 术语数据

- 全局术语保存在用户数据目录的 SQLite 数据库。
- 项目术语保存在 `project.json`。
- 有效术语只在运行时合并，不形成第三份持久数据。
- 源术语规范化顺序为 Unicode NFKC、去首尾空白、大小写折叠。
- 唯一键为 `sourceLanguage + targetLanguage + normalizedSource`。
- 项目术语覆盖同键的全局术语，并保留最终采用来源。
- 完整术语库与 Translation Memory 工作流属于 V0.3，不是 V0.1 交付门槛。

## 3. 工具链与依赖

### 3.1 Windows 基线

- 首选 MSVC 2022。
- CI 使用 GitHub Actions `windows-2022` runner。
- V0.1 仅支持 x64。
- 最低系统建议为 Windows 10 22H2。
- V0.1 不承诺 MinGW 或 Clang-cl。

### 3.2 依赖管理

- 使用 vcpkg manifest，并锁定 builtin baseline。
- Qt 6.11 通过 CMake 配置路径或工具链配置提供，不写死开发者目录。
- FluentQt 通过其 CMake package 与 `FluentQt::FluentQt` 使用。
- 第三方依赖不得使用无版本约束的 FetchContent。
- 模型权重、私有资源和运行时缓存不进入 Git。

### 3.3 V0.1 CMake 目标

至少拆分：

- `manga_domain`
- `manga_application`
- `manga_data`
- `manga_services`
- `manga_network`
- `manga_ui`
- MangaTranslator 应用程序

MSVC 生产目标启用 `/W4` 与 `/permissive-`；第三方警告不提升为项目错误。

## 4. OCR、翻译与凭据

### 4.1 OCR

- V0.1 OCR 引擎为 **Baberu OCR**（Hugging Face `genshiai-daichi/baberu-ocr`，revision `d9cc13153e9a1cd8fdfa3b7b1cc329da2020aeae`，Apache-2.0），直接使用上游发布的 ONNX 产物（vision fp16 + decoder prefill/step int8），不自建权重转换流水线；只接入一个 OCR 引擎。上游 ONNX 产物不可用或不满足再分发条件时，备选为 manga-ocr-base 的自维护转换（原方案），人工录入主线始终可用。
- 用户始终可以跳过 OCR，直接录入或修改原文。
- OCR 模型由模型目录和 Manifest 管理，不进入仓库；随应用分发的仅是上游 ONNX 文件与 Manifest，来源 revision 与 SHA-256 固定。
- 开始实现前必须确认模型格式、许可证（Apache-2.0，已确认）、语言覆盖（ja/zh/en，已确认）和再分发条件；不满足条件时不得随应用发布，并以人工录入保持 V0.1 主线可用。

### 4.2 翻译 Provider

- 首个实现为通用 OpenAI-compatible Chat Completions 适配器。
- 配置项为 endpoint、model、API key 和 timeout。
- V0.1 不集成 Provider 专属 SDK。
- V0.1 不支持流式响应，使用一次性结构化结果。

### 4.3 凭据

- API key 使用 Windows Credential Manager。
- 禁止把密钥明文写入项目、QSettings、日志、诊断包或错误信息。
- 读取、写入和删除凭据失败必须返回明确且安全的错误。
- 设置页通过不读取明文的状态查询显示“已配置/缺失/不可用”，只允许替换或删除，不执行 read-then-discard。

## 5. LLM 协议

### 5.1 请求

一次请求处理一个页面的 Region 集合，包含：

- Page ID 与稳定 Region ID。
- 源语言、目标语言和阅读方向。
- 按阅读顺序排列的 Region 与源文本。
- 当前有效术语。
- 可选页面上下文。
- 只允许返回已知 Region ID 的明确约束。

### 5.2 响应

结构化结果使用以下逻辑形状：

```json
{
  "regions": [
    {
      "regionId": "uuid",
      "sourceText": "原文",
      "translation": "译文",
      "candidates": ["候选一", "候选二"]
    }
  ]
}
```

客户端按 Region ID 合并，不能依赖数组位置。

### 5.3 异常规则

- 缺失 ID：对应 Region 失败，不影响其他合法 Region。
- 重复 ID：整次响应视为协议错误，不自动采用。
- 未知 ID：忽略并记录安全诊断，同时提示响应异常。
- sourceText 与请求不符：对应 Region 失败，不影响其他合法 Region。
- 超时和限流：使用有限次数指数退避。
- 非幂等操作不得自动重复提交。
- 取消后停止等待并标记取消；界面明确说明无法保证服务端停止执行或计费。
- 模型译文、候选译文和人工译文分开保存。
- 人工译文优先，后台结果不得覆盖人工修改。

## 6. 异步与 Undo/Redo

### 6.1 任务执行器

- 使用有界 `QThreadPool + QRunnable`。
- Port 异步契约统一返回 `QFuture<std::expected<T, ApplicationError>>`；接口不继承 `QObject`、不定义 signal。
- 取消使用标准 `std::stop_token`，随 `OperationContext`（`OperationId`、generation、令牌）传入；进度通过 `QFuture/QPromise` 进度通道报告。`viewGeneration` 只隔离当前页面视图加载，`projectSessionGeneration` 隔离打开项目的后台处理。
- 不把 QtConcurrent 作为核心任务抽象（`QFuture/QPromise` 属 QtCore，不在此列）。
- 任务输入使用不可变快照，并携带所属项目、页面和请求 ID。
- 切换页面只使旧页面的图像/预览投递失效，不取消用户启动的 OCR、翻译、清除、排版或导出。切换/关闭项目或应用退出后，旧项目任务不得发布业务结果。

### 6.2 Undo/Redo

- 用户可见的 Region 创建、删除、几何、文本和排版修改使用命令对象。
- 连续拖动合并为一个命令。
- 连续文本输入按短时间窗口和同一编辑目标合并。
- 异步 OCR 或翻译结果只有在用户确认或明确采用时进入 Undo 栈。
- 涉及文件 I/O 的清除结果先在后台提交不可变派生资产，再由 GUI 线程 push 命令；命令 redo/undo 只同步切换 Domain 引用，不执行 I/O。
- 保存不产生 Undo 命令，也不清空当前 Undo 历史。

## 7. 图像、清除与排版

### 7.1 内部图像格式

- 处理工作图统一为 RGBA8、straight alpha、sRGB。
- 导入时应用 EXIF 方向。
- V0.1 将高位深与复杂 ICC 内容转换为 sRGB 8-bit 工作图。
- 原始文件以未修改资产保留。

### 7.2 基础清除

- V0.1 只支持用户显式选择颜色与透明度的纯色填充，不猜测边缘色或近似主色。
- 清除结果形成独立图层，不覆盖原图。
- OpenCV inpaint 推迟到 V0.2。
- LaMa 推迟到 V0.3。

### 7.3 基础排版

- V0.1 支持横排、竖排、字体、字重、字距、颜色、描边、对齐、换行、行距、边距和字号二分适配。
- 测量与栅格化使用独立 `manga_rendering` target，只依赖 Domain 与 Qt Core/Gui；排版 Adapter、画布预览和导出共享同一实现。
- V0.1 不承诺完整日文禁则、振假名和高级文本塑形。
- 字体缺失时使用明确的字体回退，并在界面显示警告。

## 8. 导入与导出

### 8.1 版本范围

| 版本 | 导入格式 |
| --- | --- |
| V0.1 | JPG/JPEG、PNG、WEBP、图片文件夹 |
| V0.2 | CBZ/ZIP、PDF |
| V0.3 以后 | CBR、CB7、Fixed Layout EPUB，按价值与安全边界决定 |

### 8.2 V0.2 方案

CBZ/ZIP、PDF、检测模型、Mask、OpenCV Inpaint、安全上限和检测结果合并规则已经独立锁定，详见 [`docs/v0.2-technical-decisions.md`](v0.2-technical-decisions.md)。其中检测模型的再分发许可仍是发布门槛。

### 8.3 V0.1 导出

- 支持 PNG 与 JPEG，不支持 WEBP。
- PNG 保留透明度。
- JPEG 质量可配置，默认 95。
- 文件名冲突默认追加序号，不覆盖已有文件。
- 不写入隐私性元数据。
- 导出先写暂存文件，成功后提交为最终文件。

## 9. 设置

- 用户级设置与会话状态使用 QSettings。
- 项目级设置写入 `project.json`。
- API key 始终使用 Windows Credential Manager，不进入 QSettings。

## 10. 构建、测试与发布

### 10.1 Preset

- `windows-msvc-debug`
- `windows-msvc-release`
- `windows-msvc-test`

### 10.2 CI 与测试优先级

- GitHub Actions 覆盖 Debug、Release 和测试配置。
- 测试优先覆盖 Domain、UseCase、项目保存/恢复、过期异步结果、坐标转换和导出。
- 模型与测试样本必须确认许可证，不能提交用户漫画或来源不明的数据。

### 10.3 发布

- V0.1 使用便携 ZIP 与 `windeployqt`。
- 安装器或 MSIX 推迟到 V1.0。
- Qt、FluentQt、VC Runtime 和模型文件边界写入发布清单。
- 模型权重不进入 Git；发布包是否包含模型由模型许可证和分发策略决定。

## 11. 已锁定的产品行为

- 对比模式：V0.1 使用并排对比；滑块对比推迟。
- 源语言等于目标语言：允许继续，但显示明确警告。
- Ctrl+单击页面：只改变批量选择集合，不切换当前页；普通单击切换当前页。
- 旋转 Region：V0.1 不支持。
- 多边形 Region：V0.1 不支持，只预留 Schema 与领域类型。
- 阅读顺序：V0.1 保存 `orderIndex`，不提供独立工具；V0.2 提供人工调整。
- 新建项目和直接导入：统一为 `新建项目/导入` 入口，在导入流程中配置保存位置、语言和引擎。

## 12. 后续版本决策

- V0.2 的检测、导入、Mask 和 OpenCV 规则见 [`docs/v0.2-technical-decisions.md`](v0.2-technical-decisions.md)。
- V0.3 的任务恢复、上下文、术语、TM、LaMa、阅读顺序、排版和校对规则见 [`docs/v0.3-technical-decisions.md`](v0.3-technical-decisions.md)。
- 检测与 LaMa 权重的再分发权利仍是发布门槛；仓库许可证元数据不能替代权利链审核。
- DirectML/CUDA、软 Mask 或灰度权重的新格式，以及旋转/多边形 Region 均不进入 V0.3。
