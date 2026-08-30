# V2-P7：稳定性与发布验收

> 状态：Planned
> 前置阶段：V2-P6 退出条件全部满足
> 输出：模型权利边界清晰、可安全导入和可人工恢复的 Windows x64 V0.2 发布候选

## 1. 阶段边界

V2-P7 验证已实现的 V0.2，不新增功能。阻断问题回到责任阶段修复并重跑受影响门禁；不得通过放宽归档限制、隐藏模型许可状态或跳过真实模型作业获得通过。

发布物继续遵循 V0.1 冻结的 Windows x64 便携包策略，新增 Qt PDF、libarchive、OpenCV 和可选检测模型的部署/许可证审计。

## 2. 发布候选身份

- [ ] 记录 Git commit、版本、MSVC/Windows SDK、Qt、FluentQt、ONNX Runtime、libarchive、OpenCV 和 vcpkg baseline。
- [ ] 记录 ZIP/EXE SHA-256、Schema 版本、migration 版本、detector adapter/config version 和默认 Manifest Hash。
- [ ] 记录 Detection/OCR/Translation/Inpaint Profile identity、capabilities、runtime/backend 和 license status；排版记录 rendering config version。
- [ ] 测试、视觉、安全、性能和许可证证据必须对应同一候选；修复后生成新候选。
- [ ] `THIRD_PARTY_NOTICES` 列出新增库与实际打包组件，不把模型权重许可与应用代码许可混写。

## 3. 模型发布门

检测模型只有以下两种允许状态：

1. `redistributable`：权利链、转换授权、署名、源代码/offer 义务和文件 Hash 已书面确认，可按结论随包。
2. `user-supplied`：发布包不含权重，应用只接受用户选择的匹配 Manifest 文件并本地校验。

`requires-review` 不得进入 Release Candidate。

- [ ] 不以 Hugging Face metadata 单独证明可再分发。
- [ ] 若当前 ONNX 候选无法澄清，发布使用 user-supplied 模式或更换许可清晰模型，不改变 Port/UI 合同。
- [ ] Koharu Layout RF-DETR Seg 2XL 不随 V0.2 发布；其 custom license、Manga109 学术条款和 CUDA 建议写入评估记录。
- [ ] PaddleOCR-VL/Manga OCR/Hayai OCR、LaMa/AOT/FLUX/RORem 和本地 GGUF 权重不进入 V0.2 staging、默认 Manifest 或模型缓存初始化。
- [ ] 无检测模型、损坏模型和 Hash 不匹配时应用正常启动，检测禁用，手工主线可完成。
- [ ] 模型文件、用户缓存和下载 token 不进入 Git、诊断包或构建缓存。

## 4. 自动验证与 CI

- [ ] V0.1 全套 Domain/Application/Rendering/Services/Data/Network/UI/Integration 回归通过。
- [ ] V0.2 Schema/migration、archive、PDF、detector postprocess、candidate apply、Mask/OpenCV、pipeline 和 app smoke 通过。
- [ ] 检测 capability、optional Bubble/Panel 不持久化、Profile 配置隔离和 Profile 变化 stale 测试通过。
- [ ] 独立 detector-model 作业使用固定 revision/SHA-256 做真实推理；权重缺失、Hash 错误或黄金样本偏差必须失败。
- [ ] Baberu OCR 真实作业继续通过；网络协议测试继续只用 loopback mock 和 Fake credentials。
- [ ] 普通 CI 不从未固定 URL 下载权重，不访问真实 Provider 或用户目录。
- [ ] fuzz/security corpus 运行时设置总时间和内存上限，失败保存最小化输入而非用户内容。

## 5. 安全与资源验收

### 5.1 Archive

- [ ] ZIP Slip、绝对/盘符/UNC/NUL、link/special entry、深路径和伪装图片全部拒绝。
- [ ] 10,000 Entry、256 MiB 单项、2 GiB 总量、100:1 比例和深度 32 在流式读取中生效。
- [ ] 超限、取消、磁盘不足和解码失败后 staging 清理，既有项目和源归档 Hash 不变。

### 5.2 PDF

- [ ] 300 DPI 默认、32 MP、8,192 px、512 MiB 工作集限制和降 DPI 提示与技术决策一致。
- [ ] 多尺寸、旋转、透明、损坏页、密码保护和部分失败可理解且不阻塞 GUI。
- [ ] 连续导入大型合成 PDF 时内存有界，无逐页持续增长。

### 5.3 Pipeline

- [ ] 停止后无新阶段启动；不可中断推理返回后未发布结果被丢弃。
- [ ] failure/blocked/skipped/stale 不显示整页成功；partial candidates 可审阅。
- [ ] 项目切换和应用退出不会让迟到结果写入新会话。
- [ ] 故障注入不修改源图，不删除最后有效派生资产或人工字段。

## 6. 性能与质量基线

在固定 Windows 测试机记录硬件、系统、页面尺寸、模型状态和采样方法：

- [ ] detector 冷加载、热推理、预/后处理、峰值 RSS 和连续 20 页驻留趋势。
- [ ] 1024 输入检测期间窗口可交互；Stop 控件立即反馈，不承诺中断 ORT 内部调用。
- [ ] 300 Region + Lines + Mask overlay 的平移/缩放保持 V0.1 画布性能门，paint 路径无 I/O/解码。
- [ ] Telea/NS 对固定 Mask 的耗时、峰值内存、Alpha 保持和像素越界检查。
- [ ] CBZ/PDF 导入进度持续更新，取消有界，页面列表不一次解码全部图片。
- [ ] 检测质量集覆盖黑白/彩色、横排/竖排、拟声词和复杂页面，分别记录漏检/误检/Mask 问题，不只给总平均。

质量不足时保留手工创建、Mask 编辑和局部重跑为正式恢复路径；不得在发布阶段临时引入未计划模型。

## 7. 便携包与干净机

- [ ] 从独立 staging 打包 Qt PDF、libarchive、OpenCV、ONNX Runtime 和实际 Qt 插件；拒绝 Debug/test DLL 和开发路径。
- [ ] 无权重包在断网、无模型、无凭据、无开发 SDK 的 Windows 10 22H2 与 Windows 11 x64 启动。
- [ ] 在干净机完成 V0.1 人工主线、CBZ 导入、PDF 导入、用户侧模型校验、当前页检测、审阅采用、Telea 和导出。
- [ ] 从只读目录启动并把项目/导出写到可写目录；临时/缓存目录不可用时错误可恢复。
- [ ] 打包后执行 smoke test 和 DLL 依赖审计，生成 ZIP SHA-256。

## 8. 视觉、键盘与可访问性

- [ ] Light/Dark/high contrast 下检查 analysis Region/Line/Mask、candidate diff、Activity 和错误状态。
- [ ] Wide/Standard/Compact/Narrow 下处理入口、阶段选择、Stop、Apply 和 RecoveryAction 不被遮挡。
- [ ] 纯键盘完成导入、检测、候选审阅、Mask 工具取消、顺序调整、Apply、Undo/Redo 和导出。
- [ ] 状态不只依赖颜色；confidence、stale、manual override、skipped 和 blocked 有文本/图标语义。
- [ ] 高 DPI 和 100%/200% display/text scale 下 Mask 笔刷、Region handle 和源坐标一致。

## 9. 文档与已知限制

- [ ] 用户文档解释固定 DAG、阶段子集、前置阶段不自动补齐、candidate 与 adopted 的区别。
- [ ] 用户文档解释当前生产 Profile 与 Koharu 参考模型目录的区别，不显示尚未支持模型的下载/选择入口。
- [ ] 说明 V0.2 只有当前页/选中 Region，不提供整章后台任务。
- [ ] 说明 detector 模型来源、安装/校验方式、许可状态、CPU 性能和手工回退。
- [ ] 说明 OpenCV Telea/NS 是基础清除，复杂纹理、网点和跨线稿区域可能需要手工 Mask 或后续版本。
- [ ] 说明 CBZ/PDF 限制、拒绝的归档类型和资源上限。
- [ ] 变更日志列出 V0.1 项目迁移、备份和不可逆边界；不存在不可逆边界时明确说明。

## 10. 退出条件

- V2-P6 场景、完整自动测试、真实 detector/OCR 作业和安全 corpus 对同一候选通过。
- 模型状态不是 `requires-review`；包内每个权重/二进制有来源、Hash 和许可记录。
- Windows 10/11 干净环境完成 V0.2 主线，无开发 SDK 或隐式缓存依赖。
- 归档/PDF 资源限制、模型失败、停止、stale 和迁移故障不会损坏用户数据。
- 人工 V0.1 主线、保存重开、Undo/Redo 和导出没有回归。
- 已知限制、用户模型安装和恢复路径与实际 UI 一致。

## 11. 完成证据

保存候选身份、CI/CTest 报告、真实模型结果、安全 corpus 摘要、固定机性能、干净机矩阵、视觉/无障碍记录、第三方清单、模型许可结论、包 SHA-256 和已知限制。任何未验证边界必须明确标为阻断或延期，不写成已完成。
