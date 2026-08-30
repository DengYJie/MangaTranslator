# V2-P5：页面工作流与 UI

> 状态：Planned
> 前置阶段：V2-P1–V2-P4 的生产合同和错误恢复入口可用
> 输出：在现有编辑器中可选择阶段、运行、停止、审阅、采用和局部重试的页面级工作流

## 1. 阶段边界

V2-P5 连接已有 UseCase 与 EditorViewModel，不在 UI 中实现检测、匹配、Mask 合成、Inpaint 或文件导入规则。V0.2 不增加整章队列页面、后台恢复数据库或全项目处理。

Koharu 的 processing selector、固定阶段集合、activity progress 和部分结果语义被缩小到当前页；Selected pages 与 Entire project 保留到 V0.3。

## 2. 页面工作流交互

### 2.1 入口与范围

- [ ] 在现有 CommandBar/处理入口提供 `Process page`，不新建第二套编辑器 Shell。
- [ ] 范围只有 `Current page` 和 `Selected regions`；未选 Region 时禁用后者并说明原因。
- [ ] 检测只支持 Current page；选择 Selected regions 时检测阶段不可选。
- [ ] 阶段选择为 Detection、OCR、Translation、Inpaint、Layout 的任意非空子集。
- [ ] 预设明确展开：Through detection、Through OCR、Through translation、Through inpaint、Full page；Inpaint 预设不隐式加入 OCR/Translation。
- [ ] 未选择前置阶段时显示将使用“本 invocation candidate 或当前已采用输入”，不静默添加阶段。
- [ ] Detection 阶段展示当前 Profile 的 capability 摘要；Bubble/Panel 只作为 analysis overlay 开关，不作为独立 pipeline stage。

### 2.2 运行与审阅

- [ ] 开始前捕获 PageId、RegionIds、page revision、generation、阶段集合和每处理器配置摘要。
- [ ] 同一项目已有 invocation 时禁止第二次启动，并提供定位到活动任务。
- [ ] Activity 区显示 page、stage、processor/model、attempt、已完成、skipped、blocked、failed 和 stop 状态。
- [ ] 成功阶段立即显示 candidate preview；后续失败不隐藏较早结果。
- [ ] 候选审阅可以逐阶段勾选，也可 Apply selected；一次采用形成一个复合 Undo 命令。
- [ ] 失败阶段可以在修正输入后局部重试；重试不自动重跑已成功且输入未 stale 的阶段。
- [ ] 关闭候选面板不采用结果；重新运行同阶段前明确提示未采用候选将被替换。

## 3. 调度器

- [ ] `RunPagePipelineUseCase` 根据固定 DAG 计算 eligible stages，不接受 UI 传入任意边。
- [ ] stage 优先读取本 invocation 的依赖 candidate，再读取当前 Domain snapshot。
- [ ] 无输入标记 skipped；依赖失败/未选且无已采用输入标记 blocked；revision 变化标记 stale。
- [ ] CPU-only V0.2 使用每处理器有界 lane；检测/OCR 不并发过度争用 ORT 线程预算。
- [ ] Translation 网络请求遵循现有 network thread/retry policy；不得因本地模型 lane 串行而阻塞 GUI。
- [ ] 进度只来自已知工作单元；单次原生推理无法细分时显示 indeterminate。

## 4. 停止、切页与生命周期

- [ ] Stop 后立即禁止新 stage 调度并向可取消工作传递 stop token。
- [ ] 正在执行的 ORT/OpenCV 原生调用在安全边界返回；停止发生在发布前时丢弃输出。
- [ ] 已发布 candidate 保留并标注 invocation stopped，用户仍可审阅。
- [ ] 切页增加 view generation，但页面 pipeline 继续绑定原 PageId；任务状态可从 Activity 区重新进入。
- [ ] 项目切换/关闭增加 project session generation，请求停止并拒绝所有迟到发布/采用。
- [ ] ViewModel/QFutureWatcher 销毁前断开回调；工作线程不持有 View 或 Domain 指针。

## 5. 分析与呈现

- [ ] 检测 Region/Line/Mask 使用 analysis overlay，不成为导出图层。
- [ ] OCR/translation candidate 在 Inspector/校对视图显示，不自动成为可见译文。
- [ ] Inpaint candidate 通过现有 before/after 或 split preview 显示，不切换有效清除引用。
- [ ] Layout candidate 使用共享 `manga_rendering` 预览；采用后 UI 与导出读取相同 resolved layout。
- [ ] review 顺序固定为检测 → OCR → 翻译 → 清除 → 排版 → 最终合成，但用户可以跳过不适用步骤。

## 6. 设置与模型状态

- [ ] Pipeline 设置分别持有 Detection/OCR/Translation/Inpaint Profile，不把另一处理器字段写进当前配置。
- [ ] V0.2 Profile 只显示当前生产实现的 identity、capabilities、runtime、license/model status 和允许参数，不显示多模型选择器。
- [ ] 检测模型状态显示 Missing、Invalid、License blocked、Ready 和 Running；不读取或显示敏感内容。
- [ ] Baberu OCR、Translation Provider、Telea/NS 和 Layout 继续复用 V0.1 配置与凭据状态。
- [ ] 首次模型加载与热运行状态分开显示，不承诺虚假的固定耗时。
- [ ] Profile 更新后使旧未采用候选显示 stale；已采用结果只保留来源，不自动重跑。
- [ ] 模型不可用时相应阶段禁用，但手工 Region、文字、Mask、保存和导出仍可用。

## 7. 错误到 UI 动作

| RecoveryAction | 页面工作流入口 |
| --- | --- |
| Retry | 保留 scope/stages/config，创建新 attempt |
| ReviewInput | 定位 Region、Mask、source text 或 candidate diff |
| OpenModelSettings | 打开模型状态与安装目录 |
| SelectPath | 回到导入来源/目标选择 |
| FreeDiskSpace | 展示安全路径摘要与重试 |
| Reauthenticate/OpenSettings | 打开 V0.1 Provider 凭据配置 |
| None | 显示诊断 ID，不提供无效按钮 |

## 8. 测试

- [ ] DAG 顺序、分支并行资格、阶段子集、预设展开和“不自动补前置”。
- [ ] current page/selected regions、检测范围约束和空选择。
- [ ] partial success、skipped、blocked、failure、retry、stop、stale 和 Apply selected。
- [ ] 切页、项目切换、View 销毁、应用关闭和不可中断推理迟到结果。
- [ ] 一次采用一个复合 Undo、人工覆盖冲突、保存重开和候选未采用不改 Domain。
- [ ] ModelMissing/Invalid/LicenseBlocked、网络错误、Mask 错误与 RecoveryAction 路由。
- [ ] Profile 配置隔离、identity 显示、capability 空状态和敏感字段不进入 State/日志。
- [ ] Light/Dark、窄宽度、键盘、IME、读屏和 Reduce Motion 下主流程可用。

## 9. 退出条件

- 用户能运行固定 DAG 任意阶段子集，并准确理解输入来源、scope 和配置。
- 每个 stage 的 progress/status/error/attempt 可观察，部分结果和停止语义符合合同。
- 候选未采用不改项目；采用前复核冲突；Apply/Undo/Redo 均不重新处理或写盘。
- 切页和关闭不发生错误写回或悬挂；GUI 线程持续响应。
- UI 未暴露 Selected pages/Entire project、模型切换或其他 V0.3 能力。

## 10. 完成证据

记录状态序列测试、预设映射、Activity 截图、partial/stop/retry 场景、Undo 序列、生命周期测试和可访问性证据。性能与发布包行为在 V2-P7 记录。
