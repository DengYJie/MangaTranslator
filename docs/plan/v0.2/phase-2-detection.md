# V2-P2：文字检测适配器

> 状态：Planned
> 前置阶段：V2-P0 的 `ITextDetector`、Manifest、candidate patch 和许可证决策已冻结
> 输出：固定输入、可复现后处理、可取消调度且具备统一布局 capability 的页面检测能力

## 1. 阶段边界

本阶段实现检测模型 Adapter、Manifest 校验和真实推理验收。Region 采用、Mask 笔刷、合并拆分和生产 UI 属于 V2-P3/V2-P5。

默认技术候选仍为 `mayocream/comic-text-detector-onnx`。许可证门未通过时允许开发用户侧安装模式，但阶段不得宣称模型可随应用发布。

当前 Koharu 的统一 RF-DETR 架构是 Port 形状和质量比较参考。V2-P2 不实现旧版 `comic-text-bubble-detector + comic-text-detector-seg + YuzuMarker` 三处理器组合，也不引入 RF-DETR/SafeTensors/CUDA-oriented 生产依赖。

## 2. Manifest 与模型生命周期

- [ ] 读取 V2-P0 冻结的 Manifest，校验 model ID/version、adapter version、文件名、大小、SHA-256、backend 和 input shape。
- [ ] 模型目录来自应用设置/模型管理，不硬编码开发机路径，不在日志输出用户完整路径。
- [ ] Hash、张量名/类型/形状或 adapter version 不匹配时拒绝启动，返回可恢复模型错误。
- [ ] `Ort::Env` 和 Session 按进程/Adapter 生命周期复用，不按页面或 Region 重建。
- [ ] Detection Profile identity 与 Manifest、Session 和 candidate 绑定；Profile 变化不复用不兼容 Session。
- [ ] 模型惰性加载；首次运行报告 load 与 inference 分段进度，后续复用不伪报下载。
- [ ] CPU Session 线程数遵循有界执行器预算，避免 ORT 内外层过度订阅。
- [ ] 同一检测 Adapter 使用一个 admission lane；停止只能在 ORT 调用边界生效。

## 3. 预处理与后处理

- [ ] 原图按纵横比 letterbox 到 1024×1024，保存 scale、padding 和源尺寸。
- [ ] 固定 NCHW float32、颜色通道和 `[0,1]` 归一化；用参考实现逐阶段 fixture 锁定，不按注释猜测。
- [ ] 校验 `images` 输入和 `blk/seg/det` 输出元数据；不接受动态或意外输出。
- [ ] 冻结 confidence、NMS、DBNet/文字行和 Region 分组参数，并纳入 adapter config version。
- [ ] 去除 padding，将框、文字行和 Mask 映射回源图像像素；裁剪到页面边界并拒绝退化几何。
- [ ] 从 `seg` 生成 Region 局部 Gray8 二值 Mask，像素只允许 0/255，并写入 automatic candidate asset。
- [ ] 输出稳定排序，确保相同输入、模型和配置产生相同 candidate identity/diff 顺序。

### 3.1 统一输出能力

- [ ] 当前 Adapter 声明 `text-regions/text-lines/text-mask`，不得伪报 bubble/panel capability。
- [ ] `DetectionResponse` 对 required 和 optional 输出使用同一源图坐标、confidence 和 model/profile identity。
- [ ] 支持未来 RF-DETR 的 Adapter 可声明 `bubble-regions/panel-regions/text-class/instance-mask`，但 V0.2 Application 只消费可选分析候选。
- [ ] “不支持 bubble”与“支持 bubble 但检测结果为空”具有不同 DTO 状态和测试。
- [ ] Bubble/Panel 候选不分配 Domain ID、不写项目 JSON、不进入导出；TextRegion/Lines/Mask 继续走显式采用。

## 4. UseCase 与并发

- [ ] `DetectTextUseCase` 捕获 PageId、page revision、generation、源图快照、Manifest identity 和配置。
- [ ] 工作线程不访问 `ComicProject`、ViewModel、QPixmap 或 Widget；只返回值 DTO、QImage/Mask candidate 和错误。
- [ ] 结果回到 Application 后复核 invocation/generation，再发布不可变 detection candidate set。
- [ ] 取消前未进入 ORT 时不启动；ORT 已开始时等待安全返回并丢弃未发布结果。
- [ ] OOM/模型损坏/输入过大/输出非法/推理失败分别映射稳定错误，不用同一个 `inference.failed` 隐藏恢复动作。
- [ ] 手工 Region 创建在模型缺失或检测失败时始终可用。

## 5. Koharu 模型比较

- [ ] 使用同一组页面比较当前 ONNX 候选和 Koharu Layout RF-DETR Seg 2XL，记录 text/COO/bubble/panel 能力差异。
- [ ] 比较只用于决策报告，不把 SafeTensors、Python RF-DETR 或 CUDA 依赖加入生产 CMake。
- [ ] 记录 RF-DETR 不提供 OCR/阅读顺序、强烈建议 CUDA、模型卡 custom license 和 Manga109 条款。
- [ ] 记录是否存在张量合同、Hash 和授权均可冻结的 ONNX Runtime CPU 导出；没有时保持评估状态。
- [ ] 若其质量明显更好，将“寻找许可清晰的可部署导出”列为后续研究，不在 V0.2 临时切换运行时。

## 6. 测试与 CI

- [ ] 单元测试覆盖 letterbox、通道、归一化、padding 回映、边界裁剪、Mask 二值化和稳定排序。
- [ ] 契约测试覆盖 capability set、optional Bubble/Panel、合法空输出和未知类别拒绝/诊断。
- [ ] 合成张量 fixture 覆盖阈值、NMS、文字行分组、空输出和非法数值。
- [ ] 固定模型、固定 Hash 和有许可参考页的真实推理作业断言 Region/Mask 基线；模型缺失不得显示为 skipped success。
- [ ] 普通 CI 不下载权重，使用 Fake/合成张量保持离线确定性。
- [ ] 真实作业保存结构化指标和小型授权可分发 overlay，不上传用户页面或完整模型。
- [ ] 多分辨率、横竖页、透明图、灰度图和异常大图覆盖坐标一致性。

## 7. 性能基线

- [ ] 在固定 Windows CPU 测试机记录冷加载、热推理、预处理、后处理和峰值 RSS。
- [ ] 推理期间 GUI 事件循环保持响应；不以任意开发机时长作为跨机器硬门。
- [ ] 连续检测多页后 Session 数稳定，内存回落到记录的可接受驻留范围，无逐页增长。

## 8. 退出条件

- 相同输入/模型/config 产生确定性候选，源坐标 Region/Lines/Mask 通过黄金样本。
- 模型损坏、缺失、取消、OOM 和非法输出有稳定错误与恢复入口。
- Session 被复用且线程预算有界；GUI 线程无推理或后处理长任务。
- 真实模型作业不可假跳过；发布权限状态与模型文件技术验收分别记录。
- Adapter 只实现 `ITextDetector`，不修改 Domain 或依赖 UI。

## 9. 完成证据

记录 Manifest、Hash、张量探测结果、adapter config、黄金样本、真实作业链接、性能报告、Koharu 模型比较和许可证结论。V2-P3 负责证明候选如何进入用户可编辑项目。
