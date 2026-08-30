# V2-P4：基础 Inpaint 与阅读顺序

> 状态：Planned
> 前置阶段：V2-P0 的资产、Undo 和 effective Mask 合同已冻结；与 V2-P3 在 V2-P5 前完成集成
> 输出：基于 OpenCV 的非破坏性页面清除，以及明确可人工调整的阅读顺序

## 1. 阶段边界

本阶段包含两个独立能力：OpenCV 基础清除和阅读顺序候选/人工调整。二者不形成相互依赖。LaMa、AOT、生成式 Inpaint、自动分镜语义和跨页阅读顺序属于后续版本。

Koharu 的“检测 Mask 驱动派生 artwork，人工 Mask 与自动结果分离”作为数据流参考；V0.2 不增加通用 raster paint layer。

## 2. OpenCV Inpaint Adapter

### 2.1 输入与算法

- [ ] 输入为整页 immutable QImage、选中 Region effective masks 和配置快照。
- [ ] 默认 `INPAINT_TELEA`、半径 3 源像素；`INPAINT_NS` 只能由用户显式选择。
- [ ] 默认对 Mask 执行 2 源像素 dilation；算法、半径和 dilation 进入派生资产 identity。
- [ ] 多 Region Mask 先在源图坐标下确定性合并；重叠不重复改变像素。
- [ ] empty mask 返回 `skipped(empty_mask)`，非法尺寸/坐标/像素格式返回失败。

### 2.2 RGBA 与输出

- [ ] QImage 规范化为 straight-alpha RGBA 工作图，再提取 OpenCV 支持的 8-bit 三通道颜色平面。
- [ ] Mask 规范化为同尺寸 8-bit 单通道，非零表示修复区域。
- [ ] OpenCV 结果恢复原工作图 Alpha；V0.2 Inpaint 不改变透明度。
- [ ] Mask 与非不透明像素相交时产生质量警告，不把 RGBA 直接传给 `cv::inpaint`。
- [ ] 输出是独立 derived cleaning asset；原图字节和 Hash 永不改变。

### 2.3 Prepare/Apply/Undo

- [ ] Prepare 在工作线程执行并把内容寻址 PNG 原子提交到派生资产存储，返回 before/after 引用和诊断。
- [ ] Prepare 完成只产生候选，不切换 `ComicPage` 的有效清除引用。
- [ ] Apply 在 GUI/Application 串行边界复核 revision 和 Mask generation，再 push 命令。
- [ ] Undo/Redo 只切换已经存在的引用；不得再次运行 OpenCV、重新编码图片或访问临时文件。
- [ ] 重跑创建新版本并保留旧引用供 Undo；垃圾回收不得删除 UndoStack 或未决候选仍引用的资产。

## 3. 阅读顺序

- [ ] 自动初始顺序仅根据已采用 Region geometry、项目阅读方向和稳定规则生成 candidate order。
- [ ] 规则版本、输入 revision 和 tie-breaker 被记录；相同输入产生相同顺序。
- [ ] 当前持久 `orderIndex` 始终权威；自动候选不覆盖已有 manual reading-order override。
- [ ] 用户可以在列表中拖放、用键盘上移/下移，并在画布显示编号 overlay。
- [ ] 一次拖放或批量采用自动顺序是一个 Undo 命令；顺序始终规范化为连续 `0..N-1`。
- [ ] 新增、删除、合并、拆分 Region 后生成“顺序需要检查”状态，不静默重排人工顺序。
- [ ] OCR/翻译 selected-region 请求按持久顺序组织；检测 adapter 不负责预测阅读顺序。

## 4. 错误与恢复

| 情况 | 状态 | 恢复 |
| --- | --- | --- |
| 无有效 Mask | `skipped` | 编辑 Mask 或继续其他阶段 |
| Mask 损坏/越界 | `failed` | 恢复 automatic mask 或重新检测 |
| OpenCV 失败/内存不足 | `failed` | 缩小范围、释放内存或重试 |
| Apply 时 revision 改变 | `stale` | 重新预览并采用 |
| 顺序候选输入过期 | `stale` | 重新生成候选 |
| 人工顺序存在 | 候选冲突 | 保留人工顺序，用户逐项确认 |

错误不得删除已有清除层、Mask、顺序或源图。

## 5. 测试

- [ ] Telea/NS、半径、dilation、Mask 合并、empty/invalid mask。
- [ ] RGB/RGBA/灰度输入、alpha 保持、透明区域警告和输出可重读。
- [ ] Prepare/Apply revision 竞争、原子资产失败、Undo/Redo 无 I/O 和源图 Hash 不变。
- [ ] 规则顺序在 LTR/RTL、同一行/列、重叠和 tie 情况下确定性。
- [ ] 手工顺序保护、新增/删除/合并/拆分后的 review flag、拖放和键盘 Undo。
- [ ] UI 预览与 ExportPageSnapshot 使用同一有效清除资产和阅读顺序。

## 6. 退出条件

- OpenCV 清除使用有效 Mask 生成独立派生资产，透明度和源图保持不变。
- Prepare 不改 Domain；Apply 可撤销；Undo/Redo 不执行 I/O 或处理。
- Telea 默认和 NS 显式选择具有可复现配置，不静默切换算法。
- 阅读顺序候选确定性、可审阅且不覆盖人工顺序。
- 模型/Mask 缺失不阻止手工编辑、保存和导出。

## 7. 完成证据

记录算法配置、固定图片/Mask 输出、alpha 和源图 Hash、资产引用 Undo 序列、阅读顺序 fixture 与 UI 操作证据。复杂背景主观质量只记录已知限制，不作为增加生成式模型的理由。
