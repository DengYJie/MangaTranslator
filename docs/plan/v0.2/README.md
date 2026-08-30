# V0.2 分阶段实施计划

> 状态：Planned
> 产品目标：在 V0.1 人工闭环上增加可审阅自动检测、安全 CBZ/PDF 导入、基础 Inpaint 和页面级工作流
> 决策基线：[V0.2 技术决策](../../v0.2-technical-decisions.md)
> 关联文档：[V0.1 计划](../v0.1/README.md)、[架构设计](../../architecture-design.md)、[功能设计](../../feature-design.md)、[路线图](../../roadmap.md)
> 工作流参考：[Koharu 官方仓库](https://github.com/koharu-rs/koharu)、[Process Pages](https://koharu.rs/workflow/process-pages/)、[Vision and Inpainting Models](https://koharu.rs/models/vision-and-inpainting/)、[Translation and Generation](https://koharu.rs/models/translation-and-generation/)

## 交付主线

```text
创建或打开 V0.1/V0.2 项目
→ 导入图片、CBZ/ZIP 或 PDF
→ 对当前页运行检测
├→ 生成 OCR → 翻译 → 排版候选
└→ 生成 OpenCV Inpaint 候选
→ 审阅 Region、文字行、Mask、阅读顺序和合成结果
→ 采用所选候选
→ 保存、重开和导出
```

V0.2 借鉴 Koharu 的固定具名 DAG、分析与呈现分离、阶段子集、部分结果、安全停止和每处理器独立配置。MangaTranslator 继续执行“自动结果先成为候选、用户采用后才修改 Domain”的更严格边界。

V0.2 不复制 Koharu 的 Rust/Tauri/WebGPU 技术栈，也不引入其完整模型目录。当前 Koharu 使用统一 RF-DETR 布局模型，而不是旧版双检测器/YuzuMarker 组合；V0.2 只借鉴统一能力合同和每阶段 Profile。跨页任务、整个项目范围、上下文翻译和恢复型批处理属于 V0.3。

## 固定工作流

```text
检测 ─→ OCR ─→ 翻译 ─→ 排版 ─┐
  └────→ 清除 ───────────────┼→ 预览与采用
原图 ────────────────────────┘
```

- 当前页是 V0.2 的最大执行范围；OCR、翻译、清除和排版可缩小到选中 Region。
- 可以运行任意非空阶段子集；系统不静默补齐未选前置阶段。
- 无适用输入是显式 `skipped`，依赖不可用是 `blocked`，输入过期是 `stale`。
- 同一项目会话最多运行一个页面流水线；停止后不启动新阶段。
- 成功阶段发布不可变 candidate patch。用户采用时再次校验 revision 和人工覆盖，并生成一个复合 Undo 命令。

## 模型与处理基线

| 阶段 | V0.2 使用项 | 发布条件 | 不纳入 V0.2 |
| --- | --- | --- | --- |
| 检测 | `comic-text-detector.onnx` 技术候选，ONNX Runtime CPU | 固定 revision/Hash、黄金样本、授权链通过 | RF-DETR 仅比较；Bubble/Panel 只作可选分析候选 |
| OCR | 复用 V0.1 Baberu OCR | V0.1 真实模型验收继续通过 | PaddleOCR-VL/Manga OCR/Hayai OCR 选择器 |
| 翻译 | 复用 V0.1 OpenAI-compatible Provider | 协议、凭据和错误恢复继续通过 | 本地 LLM、整章上下文、多 Provider 编排 |
| 清除 | OpenCV Telea 默认；Navier-Stokes 显式可选 | RGBA/Mask/非破坏性测试通过 | LaMa、AOT 和生成式 Inpaint |
| 排版 | 复用 `manga_rendering` | UI 预览与导出同源 | 第二套排版或渲染实现 |

Koharu Layout RF-DETR Seg 2XL 仅作为检测质量比较参考。其模型卡标为自定义许可，训练数据涉及 Manga109 学术使用条款并强烈建议 CUDA，不满足 V0.2 的明确再分发和 CPU 基线。`ITextDetector` 仍允许未来统一返回 text、bubble、panel 和 mask capability，但 V0.2 只持久化 TextRegion、Lines 和 Mask。

检测、OCR、翻译和 Inpaint 各有一个独立 Model Profile。V0.2 的 Profile 用于配置隔离、Manifest/许可证校验、候选来源和 stale 判断；每个阶段仍只有一个生产实现，不提供模型选择器。

## 阶段与依赖

V0.2 阶段统一使用 `V2-Px`，避免与 V0.1 的 `P0–P7` 混淆。

| 阶段 | 文档 | 交付结果 | 前置阶段 |
| --- | --- | --- | --- |
| V2-P0 | [契约、Schema 与模型门](phase-0-contracts.md) | 冻结 V0.2 数据、Port、候选和工作流合同 | V0.1 P6；发布需 V0.1 P7 |
| V2-P1 | [安全归档与 PDF 导入](phase-1-importers.md) | 可恢复的 CBZ/ZIP/PDF 页面导入 | V2-P0 |
| V2-P2 | [文字检测适配器](phase-2-detection.md) | 可替换、可校验的页面检测能力 | V2-P0 |
| V2-P3 | [检测审阅与 Mask 编辑](phase-3-review.md) | 可采用、可合并拆分、可保护人工修改的检测结果 | V2-P2；图片路径来自 V2-P1 或 V0.1 |
| V2-P4 | [基础 Inpaint 与阅读顺序](phase-4-inpaint-order.md) | 非破坏性清除与人工顺序调整 | V2-P0；与 V2-P3 共同实现 effective Mask 合同 |
| V2-P5 | [页面工作流与 UI](phase-5-page-pipeline.md) | 固定 DAG、候选审阅、停止/重试和页面工作流入口 | V2-P1–V2-P4 |
| V2-P6 | [纵向切片与迁移集成](phase-6-integration.md) | V0.1 升级和 V0.2 端到端主线 | V2-P5 |
| V2-P7 | [稳定性与发布验收](phase-7-release.md) | Windows x64 V0.2 发布候选 | V2-P6 |

```text
V0.1 P6 → V2-P0 ┬→ V2-P1 ───────────────┐
                 ├→ V2-P2 → V2-P3 ──────┼→ V2-P5 → V2-P6 → V2-P7
                 └────────→ V2-P4 ───────┘
```

V2-P1、V2-P2 和 V2-P4 的底层实现可在 V2-P0 冻结后并行。V2-P3 可以先用普通图片和 Fake 检测结果开发，但退出条件必须覆盖真实检测输出。V2-P5 不得先发明临时 Pipeline DTO 再让底层追认。

## 阶段状态规则

- 状态只使用 `Planned`、`In progress`、`Completed`、`Blocked`。
- 阶段开始时更新对应文件状态；退出条件、验证和证据同时满足后才能标为完成。
- 模型许可证、真实推理和安全导入是硬门，不能通过 skip 或仅使用 Fake 标记完成。
- 发现 V0.1 契约无法兼容时，先提交向后兼容的迁移和窄接口修订，再开始依赖阶段。
- Koharu 的能力只用于设计比较；任何新增范围必须符合 MangaTranslator 路线图。

## V0.2 总体验收

- V0.1 项目无损打开并迁移；失败时保留原项目和可理解错误。
- 图片、受支持 CBZ/ZIP 和 PDF 可以安全导入，路径穿越、特殊 Entry 和资源炸弹被拒绝。
- 检测候选与源图坐标对齐，Region、文字行和 Mask 可审阅、采用、撤销和重跑。
- 人工 Region 及字段级人工修改不被检测、OCR、翻译、清除或排版结果静默覆盖。
- 当前页可运行固定工作流的任意阶段子集；跳过、阻塞、失败、取消和 stale 状态准确。
- 停止、切页、项目切换和关闭不会把迟到结果写入错误页面。
- OpenCV 清除不修改源图；预览、保存重开和导出引用同一有效派生资产。
- 普通 CI 离线确定性；真实检测模型由固定 Hash 的独立作业验收，缺失不得假成功。
- 未解决检测模型权利链时，发布包不包含权重，并保持手工 V0.1 主线可用。
