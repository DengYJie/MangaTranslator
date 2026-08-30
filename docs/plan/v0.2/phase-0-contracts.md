# V2-P0：契约、Schema 与模型门

> 状态：Planned
> 前置阶段：V0.1 P6 纵向切片合同稳定；V0.2 发布仍要求 V0.1 P7 通过
> 输出：可由 Fake 驱动、可向后迁移且不绑定具体检测模型的 V0.2 核心合同

## 1. 阶段边界

本阶段冻结 V0.2 的 Domain、Schema、Application Port、候选补丁和页面工作流语义，不实现 libarchive、Qt PDF、ONNX 或 OpenCV Adapter，不交付生产 UI。

Koharu 只提供工作流语义参考。MangaTranslator 保持自动结果候选优先、显式采用、字段级人工保护和现有依赖方向。

### 1.1 V0.1 基线校正门

V0.2 不以扩大 V0.1 范围的方式修正基线。以下校正必须在冻结 V0.2 公共合同前完成，并保持 V0.1 的手工 Region 主线可独立运行：

- [ ] 在架构总文档中分别列出版本化流水线。V0.1 从手工 Region 开始，执行 `Baberu OCR → Translation → SolidColor Cleaning → Text Layout → Review → Export`；V0.2 才从 `ITextDetector`、Lines 和 Mask 开始。
- [ ] 对齐 V0.1 设置页与已冻结能力：固定显示 Baberu OCR 身份和模型校验状态，不提供单选项的 OCR 引擎选择器，也不提供 Port/Adapter 不支持的自动横竖排开关。
- [ ] 明确 OCR 请求中的语言来自项目源语言快照。全局默认语言只用于新建项目，不作为切换 OCR 模型或隐式重跑既有 Region 的设置。
- [ ] OpenAI-compatible Provider 的默认模型使用可编辑模型 ID；模型列表只能作为 Provider 明确支持时的可选增强，不能成为配置和运行前置。
- [ ] 扩展 Baberu Manifest 值对象以保留已校验的 `source.repository`、`source.revision`、`source.license`、`runtime.opset`、`runtime.minimumOnnxRuntime` 和 `runtime.executionProvider`，供诊断、模型状态和发布证据使用。
- [ ] 在功能设计中标注版本边界：V0.1 固定单一 Baberu OCR，V0.2 引入检测和类型化 Profile，用户可选的多模型目录不属于 V0.2。
- [ ] 不向 V0.1 Domain、Schema 或 `OcrResponse` 回灌 `ITextDetector`、Bubble、Panel、Mask 或 `ModelProfile`；V0.1 P6/P7 对自动检测和复杂 Inpaint 的排除继续有效。

## 2. Domain 与 Schema

### 2.1 版本与迁移

- [ ] 为 V0.2 增加明确的 `projectVersion`、`schemaVersion` 和资源格式版本，不复用应用版本替代 Schema 版本。
- [ ] 冻结 V0.1 → V0.2 的逐步迁移顺序；迁移前只校验 envelope，迁移后执行完整 Schema 和语义校验。
- [ ] V0.1 项目迁移时为新增字段填充确定性默认值，不猜测不存在的 Mask、检测来源或阅读顺序证据。
- [ ] 迁移写盘使用现有 Repository 原子保存/备份协议；打开失败不修改源项目。

### 2.2 Region、Mask 与来源

- [ ] 增加自动检测来源、run ID、model/adapter/config version、confidence、review flag 和 lineage。
- [ ] 分别持久化 geometry、mask、source text、translation、reading order 和 layout 的人工覆盖状态。
- [ ] 冻结 Region 局部 Gray8 二值 Mask 引用、源图坐标矩形、automatic/effective 资产和 generation 元数据。
- [ ] 冻结页面级 tombstone；删除自动 Region 后重跑不得自动复活。
- [ ] 合并/拆分创建新 Region ID，并记录父 Region lineage；旧 ID 不复用。
- [ ] 阅读顺序继续以持久化 `orderIndex` 为权威，自动顺序只形成候选。

## 3. Application 契约

### 3.1 Ports

- [ ] 冻结 `ITextDetector` 的输入快照、整页源图、generation、stop token、输出坐标和 capability set。
- [ ] `DetectionResponse` 必须包含 TextRegion/Lines/Mask，允许可选 Bubble/Panel/Class 候选；区分“不支持能力”和“支持但本页为空”。
- [ ] Bubble/Panel 在 V0.2 只属于 candidate analysis，不新增持久 Domain 实体、不参与导出，也不成为 OCR 前置。
- [ ] 冻结 `IArchiveImporter`/`IPdfImporter` 或现有 Importer 扩展边界；安全策略由 Application DTO 显式传递，不藏在 UI。
- [ ] 冻结 OpenCV `IInpainter` 配置扩展，保持 V0.1 Port 的错误与取消语义兼容。
- [ ] 所有 Port 只交换值 DTO、`QImage`、相对资产引用和稳定错误，不暴露 Adapter 类型或 Domain 指针。

### 3.2 Candidate patch

- [ ] 定义不可变 `PipelineInvocationId`、`StageCandidateId`、输入 revision/generation 和依赖 candidate ID。
- [ ] 定义 detection、OCR、translation、inpaint 和 layout candidate patch 的最小字段。
- [ ] candidate patch 在应用前不得修改 Domain；派生文件可以先原子写入内容寻址暂存区，但不能切换有效引用。
- [ ] 定义 `ApplyPipelineCandidatesUseCase` 的复核顺序：项目/Page/Region 身份、revision、generation、字段覆盖、资产完整性。
- [ ] 一次采用所选 patch 生成一个复合 Undo 命令；Undo/Redo 只切 Domain/资产引用，不执行 I/O 或推理。

### 3.3 状态和错误

- [ ] 阶段状态冻结为 `pending/running/succeeded/skipped/blocked/failed/cancelled/stale`，采用状态独立表示。
- [ ] `skipped` 必须带稳定原因，如 `no_regions`、`no_source_text`、`empty_mask`；不得伪装为推理成功。
- [ ] `blocked` 指未选择/不存在有效前置输入；`stale` 指输入 revision 或 generation 已改变。
- [ ] 增加 archive、PDF、detector、mask、pipeline 和 model-license 的稳定错误码与 `RecoveryAction` 映射。
- [ ] 错误诊断不得包含完整用户路径、漫画内容、模型下载 token 或 Provider secret。

### 3.4 Model Profile

- [ ] 为 Detection、OCR、Translation 和 Inpaint 冻结类型化 Profile；V0.2 每类只有一个生产 Profile。
- [ ] Profile 包含 processor/model/adapter/config/runtime/backend/capabilities/manifest/license identity 和非敏感参数摘要。
- [ ] Profile 不包含 API Key、Prompt 全文、访问 token 或本机绝对路径。
- [ ] Profile identity 进入 operation、candidate 和错误来源；Profile 变化使旧未采用候选 stale。
- [ ] 不为 Profile 增加多模型选择 UI；排版使用 `manga_rendering` 配置快照而不是 ML Profile。

## 4. 固定 DAG 与调度语义

- [ ] 冻结 `Detection → OCR → Translation → Layout` 与 `Detection → Inpaint` 的依赖图。
- [ ] 允许任意非空阶段子集；遗漏的前置阶段不自动补齐，优先读取当前 invocation candidate，其次读取已采用 Domain 输入。
- [ ] V0.2 只允许 `CurrentPage` 和 `SelectedRegions`；`SelectedPages`/`EntireProject` DTO 可以不出现，不能以页面循环偷渡。
- [ ] 同一项目会话只允许一个 Pipeline invocation；检测/模型推理的 admission policy 不由 UI 控制。
- [ ] 停止后不启动新阶段；未发布的原生调用结果丢弃，已发布候选保留。
- [ ] 后续阶段失败不删除较早候选；局部重试创建新 attempt 并保留可追溯关系。

## 5. 模型决策门

- [ ] 保持 `comic-text-detector.onnx` 为技术候选，记录固定 revision、大小、SHA-256、张量和后处理版本。
- [ ] 书面核验 ONNX 权重来源、转换授权、GPL 上游关系、署名与再分发义务；结论进入第三方清单。
- [ ] 用同一组代表性页面比较当前候选与 Koharu Layout RF-DETR Seg 2XL 的 Region/Mask 质量、CPU 延迟、峰值内存和部署成本。
- [ ] 记录 RF-DETR 的 text/onomatopoeia/bubble/panel/mask capability，并证明未将可选类别反向扩大为 V0.2 Domain/UI 必交付。
- [ ] RF-DETR 仅作评估：其自定义许可、Manga109 学术条款、SafeTensors/RF-DETR 运行时和 CUDA 建议均记为不采用理由。
- [ ] 若当前候选授权不通过，只替换 `ITextDetector` Adapter/Manifest，不改 Domain、UseCase 或 UI 合同。
- [ ] 权重不得进入 Git；许可证未通过时不得进入默认安装包或自动下载路径。

## 6. 验证

- [ ] 基线文档测试或人工审查证明架构、功能、设置和 V0.1/V0.2 阶段文档使用一致的版本边界，不再把 `ITextDetector` 表述为 V0.1 前置。
- [ ] Baberu Manifest 测试断言 source/runtime 字段被校验并保留，未知字段、缺失字段和不兼容 runtime 继续失败关闭。
- [ ] 设置契约审查确认 V0.1 不出现无执行路径的 OCR 控件，OpenAI-compatible 自定义模型 ID 不依赖模型枚举接口。
- [ ] Schema 覆盖 V0.1 迁移、V0.2 round-trip、未知版本、损坏 Mask 引用和非法覆盖组合。
- [ ] Application 测试覆盖候选产生不改 Domain、采用复核、复合 Undo/Redo、revision 冲突和人工字段保护。
- [ ] Capability 测试覆盖 required 输出、optional 输出、不支持和合法空结果；Bubble/Panel 不进入项目 JSON/导出。
- [ ] Profile 测试覆盖配置隔离、来源追踪、敏感字段排除和 Profile 变化后的 stale。
- [ ] Pipeline Fake 测试覆盖分支顺序、任意阶段子集、skip、blocked、failure、stop 和 stale。
- [ ] 依赖测试证明 Domain 不依赖 Qt，Application 不依赖 Adapter/UI，Rendering 不依赖 I/O。

## 7. 退出条件

- V0.1 基线校正已进入文档和 Manifest 测试，且没有扩大 V0.1 Domain、Schema 或 UI 功能范围。
- V0.2 Schema、迁移和公共 DTO 有测试，V0.1 fixture 可无损升级并重开。
- 固定 DAG、候选暂存、显式采用、状态机和停止语义没有未决歧义。
- `ITextDetector` 可由 Fake 完整驱动，模型替换不要求修改内层。
- 检测模型具有书面“可分发”“仅用户安装”或“必须替换”结论，不以仓库元数据猜测。
- 后续阶段不需要自行增加跨层临时接口。

## 8. 完成证据

记录 V0.1/V0.2 版本边界审查、Baberu Manifest source/runtime 测试、设置契约审查、Schema diff、迁移 fixture、Port/DTO 头文件、候选采用测试、DAG 状态序列测试、模型比较报告和许可证结论。真实性能与真实 Adapter 验收分别由 V2-P2、V2-P6 和 V2-P7 完成。
