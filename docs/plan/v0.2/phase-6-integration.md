# V2-P6：纵向切片与迁移集成

> 状态：Planned
> 前置阶段：V2-P5 退出条件满足，V2-P1–V2-P4 的生产 Adapter 已接入
> 输出：V0.1 项目可升级、V0.2 主线可端到端重复验证的生产对象图

## 1. 阶段边界

本阶段只集成 V0.2 已冻结能力。发现 Port、Schema、Adapter 或 UI 缺口时回到责任阶段做最小修正，不在 `ApplicationContext` 或集成测试中复制业务规则。

不得在本阶段加入整章任务、SQLite 队列、跨页上下文、本地 LLM、LaMa、多模型选择或无人值守处理。

## 2. Composition Root

- [ ] 在现有 `ApplicationContext` 注册 archive/PDF importer、detector、OpenCV Adapter 和 V0.2 UseCases，不创建第二个 Context 或 Service Locator。
- [ ] 模型 Manifest/目录、归档限制、PDF 限制和处理配置通过类型化设置注入，不硬编码 UI 值。
- [ ] Detection/OCR/Translation/Inpaint Profile 分别注入并进入 operation/candidate 来源；排版继续使用 rendering config snapshot。
- [ ] `Ort::Env`、检测/OCR Session、有界执行器、network thread 和 Store 按既有生命周期共享。
- [ ] 项目会话拥有当前项目、UndoStack、pipeline invocation registry 和 project generation。
- [ ] ViewModel 只拥有 view generation、Watcher 和当前页候选展示；不拥有 Adapter。
- [ ] 检测模型缺失或被许可证门阻止时应用正常启动，V0.1 人工主线保持可用。

## 3. 端到端场景

### V2-P6-E01 V0.1 升级

打开固定 V0.1 项目 → envelope 校验 → 迁移 → 完整校验 → 保存为 V0.2 → 关闭重开 → 比较页面、Region、文字、排版、派生引用和源图 Hash。

同时覆盖只读项目、未知未来版本、迁移中磁盘不足和损坏 V0.1 项目。失败必须保留原文件和可恢复副本。

### V2-P6-E02 CBZ 检测主线

导入安全 CBZ → 验证顺序 → 检测当前页 → 审阅 added/updated → 编辑一个 Mask → 采用 → OCR → 翻译 → 排版 → 保存重开 → 导出。

普通 CI 使用真实 archive/data/rendering 和 Fake detector/OCR/translation；独立模型作业覆盖真实 detector/OCR。

### V2-P6-E03 PDF 与部分失败

导入含多尺寸页的合成 PDF → 某页触发资源降级或失败 → 用户确认有效页 → 对一页检测并采用 → 保存重开。断言失败页不改变其他页顺序和资产。

### V2-P6-E04 Inpaint 分支

检测 candidate mask → 人工修正 effective mask → Telea prepare → before/after preview → apply → Undo/Redo → 保存重开 → 导出。断言原图/automatic mask Hash 不变、Alpha 保持、Undo 无 I/O。

### V2-P6-E05 重跑与人工保护

采用检测 → 人工修改 geometry/source/translation/order/layout → 重跑检测 → 显示 conflict/stale/tombstone → 只采用未冲突字段 → 保存重开。逐字段断言人工值未被覆盖。

同时用两个 Fake detector capability 运行：一个只返回 TextRegion/Lines/Mask，另一个额外返回 Bubble/Panel。两条路径产生相同的 V0.2 持久 TextRegion 合同，Bubble/Panel 不进入项目 JSON 或导出。

### V2-P6-E06 工作流失败和停止

运行 Full page → detection 成功 → OCR 成功 → translation 失败，同时 Inpaint 正在执行 → Stop → 不再启动 Layout → 保留已发布候选 → 修复翻译并局部重试 → Apply selected。

断言状态序列、candidate dependency、丢弃未发布结果、单个复合 Undo 和关闭无迟到写回。

## 4. 真实与替身边界

| 场景 | 必须真实 | 可用确定性替身 |
| --- | --- | --- |
| E01 | Repository、Schema、migration、filesystem | 无 |
| E02 | libarchive importer、Repository、candidate apply、render/export | 普通 CI Fake detector/OCR/translator |
| E03 | Qt PDF、Repository、page image loader | Fake detector |
| E04 | OpenCV、derived asset store、render/export、Undo command | Fake detection mask |
| E05 | Domain/Application match/apply、Repository | 固定 candidate sets |
| E06 | Pipeline scheduler、operation lifecycle、candidate workspace | 可控 Fake stages/clock |

真实 detector 和 Baberu OCR 使用固定 Hash 的独立模型验收；不能因普通 CI 已用 Fake 而省略。

## 5. 保存、候选和资产

- [ ] 保存捕获项目 revision；`savedRevision` 只通过 `markCleanIfRevision(savedRevision)` 更新 clean point。
- [ ] 未采用候选不进入项目 JSON；关闭项目时明确丢弃，除非未来版本另行冻结持久候选格式。
- [ ] 已采用 automatic/effective mask、cleaning asset、lineage、tombstone 和 overrides round-trip 一致。
- [ ] 保存期间的新编辑保持 Dirty；保存不清空 UndoStack。
- [ ] 派生资产垃圾回收保留当前 Domain、UndoStack 和活动 candidate workspace 的可达引用。
- [ ] 导出只使用已采用 snapshot，不把未采用候选混入文件。

## 6. 生命周期与故障

- [ ] 快速切页只使旧 view generation 失效；活动 pipeline 继续绑定原 PageId 并可从 Activity 找回。
- [ ] 项目关闭/切换使 project generation 失效，停止 invocation，并在释放依赖前等待安全边界。
- [ ] archive/PDF/detector/OpenCV/translation 的取消都不发布半成品或假成功。
- [ ] 模型缺失/损坏、401/429、Mask 损坏、磁盘不足、只读目录和 OOM 连接到真实 RecoveryAction。
- [ ] Profile 变化使旧候选 stale，不影响已采用内容；不同处理器 Profile 设置互不污染。
- [ ] 日志和诊断不包含漫画像素、完整 OCR/译文、secret 或不必要绝对路径。

## 7. 自动测试

- [ ] 按 E01–E06 拆分测试文件和 CTest labels，不保留 placeholder。
- [ ] 异步测试使用 event loop/signal 和明确 timeout，不用无条件 sleep。
- [ ] fixture 的 ID、时间、目录、图片/PDF/CBZ 和 stage result 可控且记录许可证。
- [ ] 测试断言 Domain、磁盘、Hash、导出像素和状态序列，不只断言 UI 显示成功。
- [ ] App smoke 在 offscreen 构造生产对象图、打开 V0.2 项目、启动并停止一次 Fake pipeline 后正常销毁。

## 8. 退出条件

- E01–E06 在支持配置通过，且 V0.1 人工离线主线没有回归。
- V0.1 项目可安全迁移；失败不会覆盖原文件；V0.2 保存重开内容一致。
- CBZ/PDF、检测审阅、Mask/Inpaint、顺序和页面工作流形成完整纵向切片。
- partial failure、retry、stop、stale、切页和关闭无错误写回、悬挂或 use-after-free。
- 预览与导出只使用已采用有效资产，源图 Hash 不变。

## 9. 完成证据

记录 E01–E06 到测试名映射、fixture 清单/许可、命令与结果、迁移 diff、源图/资产 Hash、状态序列和 P7 未验证边界。真实硬件性能、安装包和人工视觉质量属于 V2-P7。
