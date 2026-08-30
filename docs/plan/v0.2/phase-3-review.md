# V2-P3：检测审阅与 Mask 编辑

> 状态：Planned
> 前置阶段：V2-P2 产生符合合同的 detection candidate set；V2-P0 已冻结字段级覆盖与采用语义
> 输出：用户可见、可比较、可修正、可采用和可撤销的 Region/Line/Mask 检测结果

## 1. 阶段边界

本阶段负责从检测候选到可编辑 Domain 的人工审阅闭环，不实现新的检测模型、整页流水线调度或复杂绘画系统。

分析覆盖层与呈现层分离：检测框、文字行和 Mask 是分析数据，不进入导出层级；已采用译文和清除结果才参与可见合成。

统一布局模型返回的 Bubble/Panel/Class 只作为可选分析候选。V0.2 不创建 Bubble/Panel Domain 实体，也不提供独立编辑、保存或导出能力。

## 2. 候选集与确定性匹配

- [ ] 每次检测生成不可变 candidate set，包含 run/model/adapter/config/input revision 身份。
- [ ] 只在同一 Page 内匹配；Manual Region 不参与自动覆盖。
- [ ] IoU ≥ 0.5 形成高置信建议；IoU 0.3–0.5 结合中心距离和 Mask 重叠形成需审阅建议。
- [ ] 匹配使用稳定排序和确定性一对一分配；一个旧 Region 或新候选最多出现一次。
- [ ] 差异明确分类为 added、updated、unchanged、retained、stale、tombstoned 和 conflict。
- [ ] 候选列表保留匹配理由与 confidence，但不把阈值伪装成人类确认。
- [ ] 可选 Bubble/Panel overlay 按 capability 显示；不支持与本页空结果使用不同空状态。
- [ ] TextRegion 可以显示只读的临时候选类别/邻近气泡诊断，但采用 TextRegion 不得连带持久化 Bubble/Panel。
- [ ] 新检测只替换当前未采用候选；已采用内容保持到用户再次明确确认。

## 3. 字段级采用

- [ ] 未修改的自动 Region 可采用新 geometry/lines/mask，并保留 Region ID。
- [ ] 人工 geometry、effective mask、source text、translation、reading order 或 layout 分别阻止对应字段覆盖。
- [ ] 自动 Region 未匹配时标记 stale 并默认保留，不自动删除。
- [ ] tombstone 匹配结果默认隐藏为“曾删除”，只有用户显式恢复才重新创建/关联。
- [ ] 采用前复核 project revision、page revision、candidate input revision 和资产 Hash。
- [ ] 一次“采用所选变化”形成一个复合 Undo 命令；Undo 恢复旧字段、lineage、tombstone 和资产引用。
- [ ] 检测完成和候选选择本身不进入项目 UndoStack。

## 4. Region 审阅操作

- [ ] 复用 V0.1 的选择、移动、缩放和删除交互，新增 automatic/manual/adopted/stale/review-needed 状态样式。
- [ ] 支持从候选新建、忽略和恢复；忽略只影响候选审阅，不隐式创建 tombstone。
- [ ] 合并多个 Region 时创建新 ID，合并 geometry、lines 和 effective mask，并保留 parent IDs。
- [ ] 拆分 Region 由用户指定矩形切割或选择文字行分组；每个结果创建新 ID 和 lineage。
- [ ] 合并/拆分不得猜测 source text/translation 分配；存在文本时必须要求用户确认保留策略。
- [ ] 手工创建 Region 继续是正式漏检恢复路径，并默认带人工 geometry。

## 5. Mask 审阅和编辑

- [ ] 提供 analysis overlay：隐藏、轮廓、半透明填充三态，不改变派生图层可见性。
- [ ] Mask 工具只编辑选中 Region 的 effective local mask；automatic mask 永不原地修改。
- [ ] 支持 Add/Erase brush、直径、硬边缘和单手势预览；V0.2 不做软边、自由颜色或通用绘画图层。
- [ ] 笔刷坐标由 device → scene → source pixel 统一映射，缩放和高 DPI 下结果一致。
- [ ] 一次 pointer press 到 release 形成一个 Undo 命令；Escape 取消未提交手势。
- [ ] 手势完成后裁剪到页面和 Mask asset rect，规范化为 0/255，并以原子文件提交后切换引用。
- [ ] “恢复自动 Mask”显式从 automatic 创建新的 effective 资产版本，可撤销。
- [ ] empty mask 合法但必须显示“清除将跳过”；损坏或尺寸不匹配的 Mask 阻止采用/Inpaint。

## 6. UI 状态与可访问性

- [ ] EditorViewModel 只保存 PageId/RegionId/candidate ID 等稳定值，不持有 Domain 指针或 graphics item 指针。
- [ ] 切页 generation 变化后旧 overlay、diff 和 Mask 载入结果不得激活到新页。
- [ ] Inspector 提供来源、confidence、差异、人工保护和恢复入口；颜色不是唯一状态信号。
- [ ] Region 与文字行 overlay 有可访问名称、键盘选择和 review-next/review-previous 路径。
- [ ] Bubble/Panel analysis overlay 默认关闭且不抢占 TextRegion 选择；隐藏后不影响 OCR、Mask 或导出。
- [ ] 300 Region 页面平移/缩放时 paint 路径不做 Mask 解码、I/O 或 Domain 查询。

## 7. 测试

- [ ] IoU 边界、稳定一对一匹配、顺序无关、tombstone 和 stale。
- [ ] 每个字段人工覆盖组合、revision 冲突、采用/Undo/Redo 和保存重开。
- [ ] 合并/拆分的 geometry、mask、lineage、文本冲突和 ID 唯一性。
- [ ] Mask brush 的坐标、高 DPI、边界裁剪、Add/Erase、取消和单手势合并。
- [ ] automatic/effective 资产独立、原子提交失败、损坏恢复和源图哈希不变。
- [ ] Light/Dark/high contrast、键盘、读屏名称和快速切页 stale result。
- [ ] Optional capability、不支持/空 Bubble/Panel、overlay 关闭和项目 JSON/导出不含可选分析实体。

## 8. 退出条件

- 用户可以逐项审阅新增、更新、保留、stale、冲突和 tombstone 候选。
- 所有自动字段只有显式采用后才修改 Domain，且一次采用可完整 Undo/Redo。
- 人工 Region 和字段级人工修改在重跑、采用、保存重开后保持。
- Region 合并/拆分和 Mask Add/Erase 可撤销，不损坏 automatic mask 或源图。
- 画布分析层与导出呈现层没有依赖或选择冲突。

## 9. 完成证据

记录候选 diff fixture、字段保护矩阵、合并/拆分案例、Mask 前后 Hash、Undo 序列、保存重开结果和主要 UI 状态截图。真实检测质量不由本阶段主观宣称，使用 V2-P2 固定输出作为输入。
