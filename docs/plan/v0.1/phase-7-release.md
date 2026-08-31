# P7：稳定性与发布验收

> 状态：Planned
> 前置阶段：P6 退出条件全部满足
> 输出：经过可追溯验证的 Windows x64 V0.1 便携 ZIP 发布候选

## 1. 阶段边界

P7 证明已实现的 V0.1 可以发布，不补做功能主线。阻断缺陷回到责任阶段修复后，重跑受影响测试和发布门；不得通过隐藏错误、放宽数据安全规则或删除测试获得通过。

V0.1 发布物是便携 ZIP，不包含安装器、MSIX、自动更新或强制模型下载。自动检测、CBZ/PDF、复杂 Inpaint、流式翻译和批处理仍不进入发布范围。

## 2. 发布候选身份

每个候选必须记录并保持一致：

- Git commit、版本号、构建时间、MSVC/Windows SDK、Qt、FluentQt、QWindowKit、ONNX Runtime 和 vcpkg baseline。
- `PROJECT_VERSION` 是应用版本、文件版本和版本说明的单一来源；不在 `Application.cpp` 维护可漂移的第二份版本常量。
- Release preset、CMake cache 摘要和是否包含 OCR 模型。
- ZIP 和可执行文件 SHA-256。
- 测试报告、视觉评审、许可证清单和已知限制所对应的同一候选编号。

修复任何发布阻断项后生成新候选；旧测试结果不能自动继承到不同二进制。

## 3. 自动验证

### 3.1 预设与命令

仓库现有预设语义为：

- `windows-msvc-test`：Debug + `BUILD_TESTING=ON`，运行全部自动测试。
- `windows-msvc-release`：Release + `BUILD_TESTING=OFF`，生成候选并执行打包后烟雾测试。
- `windows-msvc-debug`：开发调试构建，不与 Test preset 重复宣称一套发布测试。

P7 至少执行并保存结果：

```powershell
cmake --preset windows-msvc-test
cmake --build --preset windows-msvc-test --parallel
ctest --preset windows-msvc-test

cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
```

Release preset 关闭测试，因此“Release 通过”必须指打包后二进制烟雾与主线检查，不能写成 Release 单元测试已通过。若未来需要 Release 测试，应新增明确 preset，而不是复用当前名称制造误解。

### 3.2 测试套件

- Domain、Application、Rendering、Services、Data、Network、UI、Contract、Integration 和 app smoke 全部运行。
- 固定样本覆盖 Schema、迁移/恢复、坐标、Undo/Redo、generation、保存竞争、清除两阶段采用、PNG/JPEG 导出。
- `p4-model` 使用固定 revision 与 SHA-256 的模型包完成真实 OCR 推理；模型缺失不得显示为成功跳过。
- 网络协议测试使用本地 mock server，覆盖成功、401、429、Retry-After、超时、取消、非法 Structured Output 和 RegionId 不匹配。
- CI 不使用真实 Credential Manager secret、公共 Provider 或付费请求。

### 3.3 CI 责任

当前 Windows CI 的 Test matrix 已是 Debug 测试配置，Release matrix 只编译；文档不再要求第三个重复的 Debug CI job。合并门必须包含：

- Test configure/build/CTest。
- Release configure/build。
- 独立 `p4-model` 真实模型验收。
- 失败时上传不含用户数据或密钥的 CTest diagnostics。

CI 运行在 Windows Server runner，不能替代 Windows 10/11 客户端兼容性验证。

## 4. 便携包设计

### 4.1 独立 staging

新增显式 package/staging 入口，将 Release 产物复制到空目录后运行 `windeployqt`。不得直接压缩开发 build tree，也不得把测试 DLL、CTest 文件、编译数据库、缓存或 `qoffscreen` 测试插件带入发布包。

Staging 最少包含：

- `MangaTranslatorApp.exe`。
- `windeployqt` 解析出的 Qt runtime、平台插件和实际使用的 imageformats 插件。
- ONNX Runtime 动态库及应用运行必需的非系统 DLL。
- 应用资源、Schema、默认 manifest 和安全的初始配置。
- `LICENSE`、`THIRD_PARTY_NOTICES`、版本说明与已知限制。

VC Runtime 策略必须明确：若使用 app-local 部署则清单列出文件和许可证；若依赖系统已安装 runtime，则首次启动说明和干净 VM 验收必须覆盖缺失情况。

### 4.2 模型边界

- 模型权重不进入 Git。
- 只有权利链和再分发条款完成书面审核时，模型才可进入 ZIP。
- 不随包时，应用在无模型状态正常启动，明确显示模型目录/准备动作，人工主线仍可完成。
- 随包时记录模型名称、版本、来源 revision、逐文件 SHA-256、许可证和包内路径；启动校验失败时禁用 OCR，不静默使用损坏模型。

### 4.3 依赖审计

对 staging 目录执行 DLL 依赖检查，拒绝：

- Debug Qt/CRT/ONNX DLL。
- 开发机绝对路径、PDB（除非作为独立符号包）、测试 fixture、用户设置和 Credential 数据。
- 未列入第三方清单的动态库或许可证缺失资产。

ZIP 文件名包含版本和架构；发布时同时提供 SHA-256。V0.1 若无代码签名证书，可以发布未签名便携包，但必须在已知限制中说明 Windows SmartScreen 提示，不得伪称已签名。取得证书后，签名发生在压缩前，并在干净机验证签名链。

Staging 完成后至少执行一次 `MangaTranslatorApp.exe --smoke-test` 并检查零退出码；该命令必须从 staging 目录运行，且临时清空 Qt/开发依赖相关环境变量，证明启动未意外借用 SDK 或 build tree。

## 5. 干净环境验证

在不含源码、构建目录、Qt SDK、vcpkg、模型缓存和开发者 QSettings 的干净目录/VM 中：

1. 校验 ZIP SHA-256 并解压。
2. 断网且无模型、无凭据首次启动；应用不得崩溃或无限等待。
3. 完成 P6-E01 纯人工离线主线。
4. 配置固定 OCR 模型后完成 OCR 主线。
5. 使用测试 Provider/本地 mock 完成翻译恢复路径；不使用个人生产密钥作为发布证据。
6. 保存、退出、重启、打开项目并导出 PNG/JPEG。
7. 从只读目录启动并把项目/导出选择到可写目录。

Windows 10 22H2 和 Windows 11 x64 必须各在真实客户端 VM/机器执行。Windows Server CI 只能提供编译和自动回归证据，不能把 runner 名称当作客户端支持证明。

## 6. 手工、视觉与无障碍验收

### 6.1 视觉证据

使用同一 Release 候选和确定性项目状态，覆盖：

- Light/Dark、高对比度。
- Wide、Standard、Compact、Narrow、Minimum 和低于 560 px 高度。
- Empty、Loading、Single、Multi、Error、Overflow、ModelMissing、CredentialMissing。
- 100%/200% 文本缩放、100%/200% display scale 和 Reduce Motion。
- Inspector Overlay、Flyout、ContentDialog、Toast/InfoBar、焦点、Disabled 和键盘选择。

P5 的人选概念方向必须有选择记录；最终 Release 候选由不同于实现作者的人或独立评审者按 full-profile 视觉证据检查。概念选择和最终实现评审是两个门，不以单张默认截图代替。

### 6.2 键盘与输入

- 纯键盘完成创建项目、导入、切页、创建/编辑 Region、文本校对、保存和导出。
- Tab/F6 顺序、可访问名称、错误关联、读屏状态和高对比焦点可理解。
- CJK IME preedit、commit、Escape 层级、Ctrl+Z/Y 单一项目历史和跨页 Undo 通过。
- 长中文/日文/英文、混合文本和 200% 文本缩放不裁切关键动作。

## 7. 性能、资源与稳定性

在固定测试机记录硬件、系统、显示缩放、页面尺寸和采样方法：

- 500 页项目启动不解码所有原图/缩略图；滚动与切页保持惰性加载。
- 300 Region 页面平移/缩放稳定大于 30 fps，交互 paint 路径无 I/O、布局或 Domain 查询。
- 热缓存切页小于 300 ms；冷加载立即显示占位且 GUI 线程无长任务。
- 缩略图缓存不超过 64 MiB，活动页文字缓存不超过 128 MiB。
- OCR、翻译、导出运行时窗口持续响应，取消与关闭无错误写回。
- 连续执行至少 30 次快速切页、10 次项目开关和完整保存/导出循环，无崩溃、悬挂、幽灵任务或显著持续内存增长。

普通 CI 验证结构不变量和无同步阻塞；上述 wall-clock/fps 只在固定测试机作为发布证据，不能跨 runner 比较。

## 8. 数据安全、隐私与恢复

- 完整主线前后逐文件校验源图 SHA-256，必须不变。
- 保存/Save As/恢复失败保留最后有效项目和原 `.bak`，不得用空文件覆盖。
- 导出永不覆盖既有文件；失败不删除其他成功输出。
- 项目 JSON、QSettings、日志、diagnostics、崩溃信息和导出元数据均不含 API Key。
- 默认日志不含完整漫画图片、原文或译文；需要诊断内容时必须显式同意并可预览。
- 日志和 UI 错误不显示 Credential、完整 Provider response 或不必要的本机绝对路径。
- 发布清单和 ZIP 不含用户漫画、模型缓存、临时文件、开发凭据或来源不明 fixture。

故障注入至少覆盖：模型缺失/损坏、网络离线、401、429、超时、磁盘不足、只读目录、损坏项目、图片缺失、导出目标冲突和应用退出中的任务。

## 9. 发布文档与许可证

- V0.1 版本说明只列已完成能力，并明确无自动检测、CBZ/PDF、复杂 Inpaint、流式翻译和 Undo 持久化。
- 用户文档说明项目目录、备份/恢复、模型准备、远程发送边界、凭据存储、便携设置位置和卸载后残留数据。
- `THIRD_PARTY_NOTICES` 列出 Qt、FluentQt、QWindowKit、ONNX Runtime、vcpkg 库、字体、图标和可选模型的版本、许可证、来源与是否随包。
- Qt LGPL 合规材料、动态链接边界和所需许可证文本必须由发布负责人复核；文档检查不能替代法律意见。

## 10. 发布门与缺陷规则

阻断发布：

- 数据丢失、源图修改、保存/导出覆盖、Secret 泄漏。
- 崩溃、死锁、关闭后错误写回、人工修改被迟到结果覆盖。
- V0.1 主线任一步不可完成，或纯人工离线主线依赖模型/网络。
- 支持系统无法启动，发布包缺 DLL/插件/许可证，或真实 OCR 验收失败。
- 开放的 blocker/major 视觉、可访问性或恢复缺陷。

Minor 问题只有在不影响数据安全、任务完成和可访问性时才可进入已知限制，并记录 owner 和后续版本；不得用“已知问题”降级 blocker。

发布批准记录必须列出：候选 SHA-256、所有门的证据链接、未验证边界、已知限制和批准人。Agent、CI 或脚本可以验证规则，但不能自行授予产品发布批准。

## 11. 回滚与发布后处理

- 保留上一候选及其校验和；新候选失败时撤回下载入口，不覆盖用户项目或自动降级项目格式。
- V0.1 `schemaVersion = 1` 不做破坏性升级；应用拒绝未知更高版本并保留原文件。
- 若发现数据安全问题，停止分发、发布明确说明并提供只读打开/备份指导；不得建议用户删除原项目作为修复步骤。
- 发布工件、符号包和验证报告按候选编号归档；符号包与用户包分离。

## 12. 实施清单

### 候选与自动验证

- [ ] 从 `PROJECT_VERSION` 生成应用/文件/发布版本，记录候选 commit、工具链、依赖和构建配置。
- [ ] 运行 `windows-msvc-test` 全套测试、Release 构建和 `p4-model`，保存同一候选的结果。
- [ ] CI 合并门覆盖 Test、Release build、真实 OCR 和安全 diagnostics；不把 Windows Server 当作客户端兼容证据。

### 打包与干净环境

- [ ] 新增独立 staging/package 入口，运行 `windeployqt` 并排除 qoffscreen、测试文件、Debug DLL、缓存和开发路径。
- [ ] 生成第三方许可证/模型边界清单、版本说明、已知限制、ZIP/EXE SHA-256。
- [ ] 从 staging 运行 `--smoke-test`，再在干净 Windows 10 22H2 与 Windows 11 x64 完成首次启动和主线矩阵。
- [ ] 验证无模型/无凭据/断网启动，以及可选模型的来源、Hash、许可证和损坏回退。

### 质量与批准

- [ ] 完成 full-profile 视觉、键盘、IME、读屏、高对比、文本缩放和响应式验收。
- [ ] 在固定测试机完成 500 页、300 Region、缓存预算、切页、取消、开关项目和内存稳定性验收。
- [ ] 完成源图不变、恢复、只读/磁盘不足、Secret/日志隐私和包内容审计。
- [ ] 清零 blocker/major，记录 Minor owner/版本，由发布负责人批准候选。

### 初验演示

- [ ] 按 §14 完成初验 PPT 和演示脚本，素材全部来自同一发布候选的证据包。
- [ ] 初验前在干净环境按演示脚本完整彩排一次，录屏备用（防止现场环境故障）。

## 13. 候选证据包

每个候选的所有证据归档到 `build/release-evidence/<candidate-id>/`（不入 Git，仅 SHA-256 清单入库）：

```text
<candidate-id>/
├── build/            # CMake cache 摘要、工具链版本、依赖清单与 revision
├── tests/            # ctest 输出、p4-model 结果、场景↔测试名映射表
├── package/          # staging 文件清单、ZIP/EXE SHA-256、依赖审计结果
├── clean-env/        # 干净 VM/目录验证记录（每步结果 + 截图）
├── visual/           # full-profile 视觉矩阵截图（复用 P5-D 证据）
├── performance/      # 固定测试机 wall-clock/fps/内存记录与采样方法
├── security/         # 源图 SHA-256 前后对照、隐私审计、故障注入清单
└── acceptance/       # 初验 PPT、演示脚本、彩排录屏、评审记录与批准结论
```

候选编号规则：`v<PROJECT_VERSION>-rc<N>`（N 从 1 递增）。任何阻断修复后 N+1，旧候选证据保留但标记 superseded。

## 14. 项目初验演示（PPT）

初验面向评审者介绍项目全貌。PPT 无固定模板，按本节大纲组织即可；核心原则是**全部素材来自真实证据包和设计文档，不为演示单独编造内容**。建议 12–15 页、讲解 8–10 分钟 + 现场演示 3–5 分钟。

### 14.1 页面大纲

1. **封面**：项目名称、V0.1 版本号、候选编号、日期、汇报人。
2. **项目概述**：一句话定位（漫画翻译桌面工作台）与要解决的问题（机器结果不可靠，人工校对与排版才是瓶颈）；目标用户与典型场景。
3. **设计思路**：以"当前漫画页"为 Hero Surface 的工作台定位；自动结果始终是候选、人工显式采用；MVVM + 单向数据流。素材：`docs/architecture-design.md`、`docs/plan/v0.1/phase-5-ui-editor-supplement.md` §1。
4. **开发过程**：P0–P7 分阶段契约先行；每阶段 TDD 与冻结契约；子代理执行 + 独立评审的工作流；prek/CI 质量门。素材：`docs/plan/v0.1/README.md` 阶段图、CI 通过记录。
5. **技术栈**：C++23、Qt 6.11 Widgets、FluentQt（公共 API）、QWindowKit（窗口边框）、ONNX Runtime、CMake presets + vcpkg。说明每个选择的理由与替代方案。
6. **算法与处理管线**：固定流水线 OCR → 翻译 → 清除 → 排版；清除/排版的两阶段采用协议（先原子落盘不可变资产，Undo/Redo 只切引用不执行 I/O）；OpenAI-compatible 协议解析与 429/Retry-After 重试策略；共享渲染核心保证 UI 预览与导出逐像素同源。素材：`docs/v0.2-technical-decisions.md` 对照表、P6-E04 一致性测试。
7. **功能演示**（现场或录屏）：创建项目 → 导入图片文件夹 → 手工 Region → OCR 候选 → 采用 → 翻译 → 清除 → 排版调整 → Undo/Redo → 保存 → 关闭重开 → 导出 PNG。即 P6-E01/E04 主线的用户视角版本。
8. **界面**：Home/Editor/Settings 的 Light/Dark 截图；响应式断点（Wide/Narrow）对比；空态、错误态、加载态。素材：视觉证据矩阵，不单独摆拍。
9. **创新点**：候选-人工字段级保护；generation + operationId 的迟到结果失效语义；内容寻址派生资产；冻结契约驱动的多代理协作开发。
10. **难点与解决**：FluentQt 窗口实现缺陷 → 评估并迁移 QtWindowKit 外壳；异步完成与 GUI 线程归属；Undo 栈游标与 Domain Dirty 的一致性；打包依赖审计。
11. **测试与质量**：测试金字塔（unit/contract/integration/app smoke）数量与场景映射表；干净机首次启动矩阵；源图 SHA-256 不变证据。
12. **总结与展望**：V0.1 交付清单与已知限制；V0.2（自动检测、CBZ/PDF、页面级流水线）与 V0.3（LaMa 等生成式修复）路线；Koharu 参考边界。
13. **Q&A**：预留。

### 14.2 演示规则

- 演示数据使用 `tests/fixtures/` 中已登记来源与许可证的素材，不使用用户漫画或来源不明图片。
- 现场演示断网进行；OCR 演示使用固定模型包；翻译演示使用 loopback mock 或明确声明的测试 Provider，不暴露真实凭据。
- PPT 中所有性能数字必须出自 `performance/` 证据目录，标注测试机配置；不允许引用开发机一次运行的印象数据。
- 评审记录（问题、结论、整改项）归档到 `acceptance/`，整改项按 §10 缺陷规则分级回流到责任阶段。

## 15. 退出条件

- P6 主线在同一 Release 候选上通过，全部自动测试和真实 OCR 作业通过。
- Windows 10 22H2、Windows 11 x64 与干净目录首次启动矩阵通过。
- 便携 ZIP 可独立启动，依赖、模型边界、许可证、版本和 SHA-256 齐全。
- 性能、视觉、无障碍、故障恢复、隐私和源图不变证据齐全。
- 无开放 blocker/major；Minor 均进入已知限制并有后续归属。
- 发布负责人完成最终批准，V0.2 可在不推翻项目格式、Region、Undo 和异步协议的前提下继续。
