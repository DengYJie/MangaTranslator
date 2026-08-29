# V0.1 分阶段实施计划

> 状态：Planned
> 产品目标：不依赖自动文字检测，完成可保存、可恢复、可撤销的人工辅助翻译闭环
> 决策基线：[V0.1 技术决策](../../technical-decisions.md)
> 关联文档：[架构设计](../../architecture-design.md)、[功能设计](../../feature-design.md)、[路线图](../../roadmap.md)

## 交付主线

```text
创建或打开项目
→ 导入图片或图片文件夹
→ 手工创建矩形 Region
→ OCR 或人工录入原文
→ 翻译或人工录入译文
→ 纯色清除与基础排版
→ Undo/Redo
→ 保存并重新打开
→ 导出 PNG/JPEG
```

V0.1 不包含 CBZ/ZIP、PDF、自动文字检测、OpenCV/LaMa Inpaint、跨页上下文、完整术语库、Translation Memory、流式翻译、旋转/多边形 Region、WEBP 导出和完整日文禁则。

## 阶段与依赖

| 阶段 | 文档 | 交付结果 | 前置阶段 |
| --- | --- | --- | --- |
| P0 | [工程与构建基线](phase-0-foundation.md) | 可重复构建和测试的空应用 | 无 |
| P1 | [Domain 与项目格式](phase-1-domain-project.md) | 冻结 V0.1 核心数据与 Schema | P0 |
| P2 | [Application 契约与 UseCase](phase-2-application.md) | 可由 Fake 驱动的业务边界 | P1 |
| P3 | [数据持久化与图片导入](phase-3-data-import.md) | 可恢复项目和图片来源 | P2 |
| P4 | [OCR、翻译、清除、排版与导出](phase-4-processing.md) | 可替换处理与导出适配器 | P2 |
| P5 | [UI、MVVM 与漫画画布](phase-5-ui-editor.md) | 可人工编辑的工作台 | P1/P2 契约修订、P3 图片/派生路径、P4 共享渲染/处理 Port |
| P6 | [纵向切片集成](phase-6-integration.md) | 端到端真实工作流 | P3、P4、P5 |
| P7 | [稳定性与发布验收](phase-7-release.md) | V0.1 发布候选 | P6 |

```text
P0 → P1 → P2 ┬→ P3 ─┐
             ├→ P4 ─┼→ P6 → P7
             └→ P5 ─┘
```

P3、P4、P5 的主体可以在 P2 契约冻结后并行，但 P5 的画布渲染、缩略图、凭据状态和 Clear Undo 必须等待表中列出的窄契约落地；外壳、静态布局和 ViewModel 基础不必等待完整 P4 Adapter。各阶段不得复制领域规则或绕过 UseCase。

## 阶段状态规则

- 状态只使用 `Planned`、`In progress`、`Completed`、`Blocked`。
- 阶段开始时，将对应文件状态改为 `In progress`。
- TODO 全部完成不等于阶段完成；必须同时通过测试、退出条件和文档检查。
- 阻塞项写明原因、影响范围和解除条件，不用删除 TODO 表示延期。
- 阶段范围变化先更新 V0.1 技术决策与本索引，再修改实现。
- 不用 V0.2/V0.3 能力替代 V0.1 验收缺口。

## V0.1 总体验收

- 导入真实图片后可以完成整个主线，并在重新打开项目后继续编辑。
- 页面、Region、原文、译文、排版、图层引用和人工修改保持一致。
- 源图永不被覆盖；保存或导出失败不会破坏已有有效内容。
- 用户可见编辑具有 Undo/Redo；后台迟到结果不能覆盖人工修改。
- OCR、翻译、清除、排版、导出及失败恢复均不阻塞 GUI 线程。
- PNG/JPEG 单页和批量导出通过确定性样本测试。
- Light/Dark、正常/窄宽度、高 DPI、键盘和 IME 的主工作流可用。
