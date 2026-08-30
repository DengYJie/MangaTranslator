# MangaTranslator 架构设计

> 状态：初稿
> 来源：根据“漫画翻译系统实现”对话中的最终结论整理
> 技术基线：C++23、Qt Widgets 6.11、FluentQt、CMake
> V0.1 决策基线：[`docs/technical-decisions.md`](technical-decisions.md)
> V0.2 决策基线：[`docs/v0.2-technical-decisions.md`](v0.2-technical-decisions.md)
> V0.3 决策基线：[`docs/v0.3-technical-decisions.md`](v0.3-technical-decisions.md)

## 1. 文档目的

本文定义 MangaTranslator 的总体架构、模块边界、数据流、线程模型和关键技术决策。仓库名使用 MangaTranslator；若产品展示名继续使用 MangaFlow，只需替换品牌文案，不影响本文架构。

系统定位是：

> AI 辅助漫画翻译与排版工作台，而不是不可编辑的全自动翻译器。

目标工作流为：

```text
导入漫画
  -> 文字检测
  -> OCR
  -> 阅读顺序
  -> 上下文翻译
  -> 原文清除
  -> 译文排版
  -> 人工校对
  -> 导出
```

## 2. 架构目标

- AI、OCR、模型和文件格式均可替换，不影响 UI 与核心业务。
- 画布编辑、人工校对和非破坏性图层是一等能力。
- 所有长任务异步、可取消、可报告进度，并能拒绝过期结果。
- View 只渲染 State 和发送用户意图，业务逻辑集中在 UseCase。
- 适合从单页人工工作流逐步演进到整章自动批处理。
- 支持本地模型和远程翻译服务，同时保持凭据和用户内容安全。

## 3. 总体分层

项目参考 DengYJie/ForgeAI 当前的 Clean Architecture、Composition Root 和 Per-Screen State 设计。

```text
src/
├── app/          # Application、ApplicationContext、启动与关闭
├── core/         # 日志、设置、ID、基础工具
├── domain/       # 漫画、页面、文本区域、翻译、排版等领域模型
├── application/  # ports + usecase
├── rendering/    # Qt Core/Gui 排版测量与栅格化共享核心，不含 I/O 或 Widgets
├── services/     # 检测、OCR、清除、排版等本地实现
├── data/         # 项目存储、SQLite、导入、导出、缓存
├── network/      # LLM/翻译 Provider 与 HTTP 适配器
└── ui/           # ViewModel、State、页面、导航、Fluent 界面、漫画画布
```

依赖方向：

```text
ui ----------> application ----------> domain
 |                                      ^
 +-----------> rendering ---------------+
                    ^                    ^
                    |                    |
services -----------+----> application --+
data/network ------------> application --+

app/ApplicationContext 构造并连接所有层
core 不依赖任何业务层
```

`rendering` 是 UI 与 Services 可共同依赖的功能共享层。它只依赖 Domain 与 Qt Core/Gui，负责确定性的文字测量、glyph 定位和源图像像素坐标下的栅格化；不得访问项目目录、Port、ViewModel、QWidget、FluentQt 或具体 Adapter。排版 Adapter、画布预览和导出图层快照生产必须复用该层；Data Exporter 只合成调用方传入的不可变图层，不能反向依赖 Rendering。

### 3.1 Domain

Domain 描述系统中的业务事实和约束，不感知 QWidget、网络、数据库、ONNX Runtime 或具体文件格式。

主要模型：

- `ComicProject`：项目元数据、页面顺序、语言、阅读方向和处理配置。
- `ComicPage`：源页面、页面身份、区域集合和非破坏性编辑数据。
- `TextRegion`：几何、文字行、Mask、原文、译文、置信度和方向。V0.1 只使用源图像像素坐标下的轴对齐矩形；类型系统与 Schema 为后续多边形预留扩展点。
- `TextLayout`：字体、字重、字号、字距、颜色、描边、方向、对齐、行距、边距和排版边界。
- `TranslationEntry`：原文、译文、候选译文、人工修改和来源信息。
- `GlossaryItem`：源术语、目标译法、类别、持久归属、匹配策略和启用状态。
- `EffectiveGlossaryItem`：由全局与当前项目术语解析出的只读结果，包含最终采用译法、真实来源和覆盖/冲突状态；它不是第三份持久数据。
- `ReadingDirection`：从左到右或从右到左。

源图像像素坐标是文本区域和排版区域的规范坐标系。模型输入、Scene、View、设备像素和导出坐标都必须显式转换。

### 3.2 Application

Application 表达“系统要做什么”。

`application/ports` 由应用层定义能力接口，例如：

- `IProjectRepository`
- `IGlossaryRepository`
- `IComicImporter`
- `ITextDetector`
- `IOcrEngine`
- `ITranslator`
- `IInpainter`
- `ITextLayouter`
- `IComicExporter`

`application/usecase` 按任务组织，例如：

- 项目：创建、打开、保存、关闭、导入和导出。
- 编辑：选择页面、检测文字、OCR、翻译、清除、排版和更新区域。
- 批处理：启动、暂停、继续、取消、重试失败页面。
- 术语库：查询、添加、修改、删除、导入、导出，以及按当前项目解析有效术语。

UseCase 只依赖 Domain 和 Port，不知道具体使用哪种 OCR、LLM、数据库或 Widget。

Port 契约统一形态：异步操作返回 `QFuture<std::expected<T, ApplicationError>>`，取消通过 `OperationContext` 携带标准 `std::stop_token`，进度通过 `QFuture/QPromise` 进度通道投递；Port 接口不继承 `QObject`、不定义 signal，`QObject` 与网络栈只出现在适配器内部。`OperationId`（UUID v4）用于日志与流水线追踪；`viewGeneration` 隔离当前页面视图，`projectSessionGeneration` 隔离打开项目会话。Application 目录按 `error/`、`operation/`、`dto/`、`ports/`、`mapping/`、`usecase/`、`workflow/` 组织；DTO 与 Domain 的映射放在 `mapping/`，第三方类型不穿越 Port。

术语数据遵循两层持久归属：全局术语保存在用户目录 SQLite，项目术语随 `ComicProject` 保存。术语键在 Unicode NFKC、去首尾空白和大小写折叠后，由源语言、目标语言与规范化源词共同确定。项目术语与启用的全局术语匹配同一键时，项目术语优先。解析 UseCase 必须返回可追踪来源的 `EffectiveGlossaryItem`，检测同一归属内的歧义，并保证 UI 预览与翻译请求使用同一份解析结果。无当前项目时只能解析和修改全局术语。

项目切换、关闭和异步索引完成均携带稳定项目 ID/请求 generation；旧项目的完成结果不得发布到新项目 State。缓存或术语索引可重建，不能成为全局或项目术语的唯一副本。

### 3.3 Adapters

- `services` 实现本地计算 Port，包括 ONNX 检测与 OCR、清除和排版 Adapter；文字测量与最终栅格化委托给共享 `rendering` 层。V0.1 只接入一个 Manga OCR ONNX 实现与基础纯色清除；OpenCV 和 LaMa 按路线图后移。
- `data` 实现目录型项目仓储、用户级术语 SQLite、V0.3 项目任务/TM SQLite，以及按版本开放的图片文件夹、CBZ、PDF 等导入器。
- `network` 在 V0.1 只实现通用 OpenAI-compatible Chat Completions 适配器；Provider 专属 SDK 和其他协议适配器属于后续能力。

第三方类型不得穿过 Port 泄漏到 UseCase、ViewModel 或 Domain。

### 3.4 UI

UI 按职责组织：

```text
ui/
├── base/        # BasePage、BaseViewModel
├── screen/      # main、home、editor、batch、glossary、settings
├── manga/       # ComicCanvas、ComicScene、GraphicsItems、编辑工具
├── navigation/  # 应用导航与路由
├── widget/      # 可复用复合组件
└── window/      # 顶层窗口
```

## 4. MVVM 与 UDF

系统采用 ForgeAI 风格的 Per-Screen State，而不是 Redux。

```text
View
  -> Intent
ViewModel<State>
  -> UseCase
  -> Port
  -> Adapter
  -> Result
ViewModel::updateState()
  -> stateChanged(State)
View::render(State)
```

核心规则：

- 每个 ViewModel 持有一个权威 State 快照。
- View 通过 `observe()` 立即获得当前 State，并订阅后续变化。
- View 不能直接修改 State，只能调用 ViewModel 的意图方法。
- ViewModel 只能通过 `updateState()` 改变持久 UI 状态。
- 新旧 State 相等时不发送 `stateChanged`。
- State 必须有确定的值语义和相等比较。
- Toast、打开文件选择器、导航和导出完成提示等一次性事件使用独立 Signal，不塞入 State。
- 异步调用使用 `beginRequest()`、`isRequestCurrent()`、`invalidateRequests()` 或等价请求代次机制，防止旧页面结果覆盖新页面。
- 不引入全局 `Store / Action / Reducer / Effects` 体系。

典型 `EditorState` 包含：

- 当前项目和页面。
- 当前选中的稳定 Region ID。
- 可渲染的页面与文本区域快照。
- 当前预览模式：原图、清理、翻译或对比。
- 检测、OCR、翻译、清除、排版的忙碌和进度状态。
- 可恢复错误和状态说明。
- 当前工具、缩放和面板可见性。

典型 `GlossaryState` 包含：

- 可选的当前项目 ID 与安全显示名称。
- 当前视图：有效术语、当前项目或全局术语；无项目时只能为全局术语。
- 当前视图的条目快照、筛选、选择、滚动恢复键和结果数量。
- 有效术语的最终译法、真实来源、覆盖与冲突状态。
- 编辑草稿、持久归属、验证、Dirty、保存和导入/导出状态。
- 解析 generation、加载状态和可恢复错误。

## 5. ApplicationContext

进程入口先调用 `fluent::prepareHighDpiApplication()`，再构造 `QApplication`；随后调用 `fluent::initializeResources()`，并使用 `Typography::fontStyle(Typography::FontRole::Body)` 设置默认字体。完成这些前置步骤后，`app/ApplicationContext` 作为唯一 Composition Root，负责：

1. 初始化日志，加载并验证设置。
2. 通过 FluentQt 公开主题 API 应用已保存的主题偏好。
3. 构造 Repository、Provider、Service 和 UseCase。
4. 构造 ViewModel。
5. 将 ViewModel 注入 View 和 MainWindow。
6. 在退出时取消任务、保存安全状态并按反向顺序销毁对象。

禁止在业务类中全局查询 `ApplicationContext`，也不使用 Service Locator。

## 6. 翻译处理流水线

```text
ComicPage
  -> ITextDetector
       -> blocks + lines + mask
  -> ReadingOrder
  -> IOcrEngine
       -> sourceText
  -> ITranslator
       -> translatedText + candidates
  -> IInpainter
       -> cleaning layer
  -> ITextLayouter
       -> TextLayout
  -> Preview
  -> User correction
  -> IComicExporter
```

### 6.1 文字检测

第一阶段检测“文字区域”，不把“气泡检测”设为前置条件。推荐以 comic-text-detector 的 ONNX 模型作为候选，实现必须隐藏在 `ITextDetector` 后面。

输出至少包含：

- 稳定 Region ID。
- 几何数据。V0.1 为轴对齐矩形，后续版本才可启用多边形。
- 文本行信息。
- 用于清除的 Mask。
- 置信度。
- 映射回源图像的坐标。

OpenCV 适合预处理、轮廓、Mask 修正和后处理，不作为复杂漫画场景的主检测器。

### 6.2 OCR

V0.1 通过 `IOcrEngine` 接入一个 Manga OCR ONNX 实现，并始终允许人工直接录入或修改原文。模型格式、许可证、语言覆盖和下载策略通过验证后才能成为发布依赖；模型权重不进入仓库。

OCR 接受页面与 Region，输出文本、置信度和必要的诊断信息。竖排、振假名、漫画字体和低质量扫描是日漫 OCR 的重点。通用多语言 OCR 和云端 OCR 可在后续版本作为新的 Port 实现加入。

### 6.3 阅读顺序与翻译

不逐气泡孤立调用 LLM。先完成整页 OCR，按阅读方向排序，再携带页面上下文、术语表和稳定 Region ID 一次请求一组文本。

V0.1 使用非流式结构化响应。返回结果必须按 Region ID 验证，不能仅依赖数组顺序：缺失 ID 只使对应 Region 失败，重复 ID 使整次响应成为协议错误，未知 ID 被忽略并形成安全诊断。模型译文、候选译文和人工译文分开保存，人工译文优先，迟到结果不得覆盖人工修改。

V0.1 保存 `orderIndex`，V0.2 提供人工调整。V0.3 以确定性几何规则生成带置信度的候选顺序，混合横竖排页面不强行推断；只有用户采用后才更新 `orderIndex`，而翻译始终使用当前已保存顺序。

V0.3 的跨页上下文仍以单页为提交和输出单位：当前页完整 Region、最多前 2 页确认文本、受限章节摘要、有效术语和 TM 候选共同组成请求。超限按固定次序裁剪，上下文失败后的单页降级默认关闭且必须可见。详细预算与缓存键见 [V0.3 技术决策](v0.3-technical-decisions.md)。

### 6.4 原文清除

按版本分三级实现：

1. V0.1：用户显式选择颜色与透明度的纯色背景填充，不猜测近似主色。
2. V0.2：使用 OpenCV Telea/Navier-Stokes 处理普通复杂背景。
3. V0.3：使用固定的漫画 LaMa ONNX 候选处理高级复杂背景，只交付 CPU 推理，并保留 OpenCV 和人工清除层回退。

清除结果保存为独立图层或可重建缓存，不永久覆盖原图。

### 6.5 排版与渲染

`ITextLayouter` 根据文字、可用区域、方向、字体和边距产生 resolved `TextLayout`。`manga_rendering` 中的 `TextLayoutEngine` 是测量与绘制的唯一实现；`QtTextLayouter`、画布和导出图层快照生产调用它，Data Exporter 不调用。自动字号通过二分搜索寻找不溢出的最大字号。

页面采用非破坏性图层：

```text
Original Image
+ Cleaning Layer
+ Translation Layer
+ Manual Overrides
= Preview / Export Flatten
```

只有预览或导出时进行合成，以支持修改译文、移动文字、恢复原文和 Undo/Redo。

## 7. 漫画画布

编辑器采用：

```text
ComicCanvas : QGraphicsView
ComicScene  : QGraphicsScene
```

主要 Scene 层：

1. 原始页面。
2. 清除/Mask 预览。
3. 文本区域。
4. 翻译文字。
5. 选择框和缩放手柄。
6. 辅助线和临时工具覆盖层。

`QGraphicsItem` 只是 State 的投影，不保存权威业务数据，不直接调用 UseCase。交互产生选择、移动、缩放、编辑、创建区域等语义意图，再交给 ViewModel。

## 8. FluentQt 与界面外壳

系统采用文件工作台布局：

```text
NavigationView
  -> Page browser | Comic canvas | Inspector
  -> Command bar
  -> Status/task area
```

- 固定一级区域使用 `NavigationView`。
- 用户管理的多文档工作区才使用 `TabView`。
- 页面列表、画布、Inspector 使用可调整的 `SplitView`。
- 优先使用 FluentQt 公共组件。
- `QGraphicsView` 是核心编辑能力所需的原生 Qt 例外。
- 不引入自定义 `QStyle/QProxyStyle`、`ui/style`、独立主题目录或应用级硬编码 QSS；直接使用 FluentQt 已提供的语义主题与颜色 Token。
- 原生 Qt 或自绘控件直接消费 FluentQt 语义 Token；控件专属的几何和视觉 Metrics 以 `constexpr` 放在对应 `.cpp` 的匿名命名空间内。
- 完整支持 Light/Dark 语义主题，但漫画画布工作区可在两种主题下保持中性深色，以突出白色页面。

## 9. 异步与线程模型

GUI Thread 只负责 Widget、ViewModel、Scene、`QPixmap` 和轻量 State 渲染。

以下工作必须离开 GUI Thread：

- 图片/PDF/CBZ 解码。
- ONNX 检测和 OCR。
- 网络翻译。
- Inpainting。
- 全页排版、图层栅格化和导出。

画布 `paint()` 只合成已准备好的 `QPixmap` 或轻量 glyph 缓存；不得在绘制热路径中执行重新排版。`QImage` 可在后台构建，`QPixmap`、Widget、Scene 和 ViewModel 只能在 GUI Thread 创建或修改。

V0.1 采用有界 `QThreadPool + QRunnable` 任务执行器，配合标准 `std::stop_token` 取消令牌、`QFuture/QPromise` 进度通道和请求 generation。`QFuture/QPromise` 属 QtCore，不引入 `QtConcurrent`；禁止每页创建一个无管理线程。

每个任务必须定义：

- 输入快照和输出类型。
- 所属项目、页面与请求 ID。
- 进度单位。
- 合作式取消。
- 超时和重试边界。
- 回调接收者生命周期。
- 关闭项目/窗口时的行为。

Worker 使用 `QImage` 或模型缓冲区；`QPixmap` 和 Scene 只能在 GUI Thread 使用。

## 10. 持久化与缓存

- V0.1 使用目录型项目：`project.json`、`assets/original/` 和 `assets/derived/`。默认复制源图，JSON 只保存相对路径。
- 项目保存采用同文件系统临时位置写入、完整校验后原子替换，并保留 `.bak` 恢复文件。
- `project.json` 遵循 JSON Schema Draft 2020-12，并记录 `projectVersion`、`schemaVersion` 和 `formatVersion`。
- Page 和 Region 使用 UUID v4 小写标准字符串；其 ID 在保存、重开、导出和重跑任务间保持稳定。
- 项目保存 Region、原文、模型/候选/人工译文、排版、图层和处理配置。V0.1 Region 只持久化 `geometry.type` 为矩形的 `x/y/width/height` double 值，最小为 2×2 源像素。
- 用户级全局术语保存于 SQLite；项目术语保存于 `project.json`。缓存、历史和索引可重建，不能成为用户编辑的唯一副本。
- V0.3 新增 `project.db`，使用 SQLite WAL 保存整章任务、页面阶段、事件、重试、恢复信息和项目 TM；数据库只引用派生资产，不保存大图。
- 任务状态通过单一串行写入通道和事务更新。活动数据库使用 SQLite Backup API 生成一致快照；可写 WAL 数据库不放在网络文件系统，也不能通过复制打开中的主文件进行备份。
- `project.json` 与 `project.db` 没有跨文件事务。完整项目快照必须暂停新写入、保存 JSON、备份数据库并记录共同项目修订。
- 模型权重放在仓库外的用户数据/缓存目录，通过 Manifest、版本和 SHA-256 验证。
- API 密钥只进入 Windows Credential Manager；普通用户设置和会话状态进入 `QSettings`，项目级设置进入 `project.json`。密钥不得进入项目、`QSettings`、日志或错误信息。

## 11. 错误处理

错误按层映射为稳定类别：

- 输入/项目格式错误。
- 模型缺失、损坏或不兼容。
- OCR/推理失败或内存不足。
- 网络离线、认证、配额、限流、超时和返回格式错误。
- 保存冲突、磁盘空间不足和导出失败。
- 用户取消。

Application 层统一 `Result<T> = std::expected<T, ApplicationError>`：错误包含稳定类别、错误码（如 `network.timeout`）、用户安全摘要、日志诊断和恢复建议。第三方异常在适配器内捕获并映射，不穿越 Port 边界。

State 保存可恢复状态；一次性提示使用 Signal。日志保留操作、模型、页面和安全诊断，不默认记录完整漫画文本、图片或密钥。

## 12. CMake 与测试边界

- C++ 标准固定为 C++23，Qt 固定为 6.11 系列兼容配置。
- 使用 target-based CMake 和导入目标 `Qt6::*`、`FluentQt::FluentQt`。
- V0.1 仅支持 MSVC 2022 x64，最低系统建议为 Windows 10 22H2；CI 使用 GitHub Actions `windows-2022`。
- 采用自动下载预编译依赖机制，不再依赖 vcpkg 或系统全局包；Qt 通过可配置 CMake 路径提供。
- 预设命名为 `windows-msvc-debug`、`windows-msvc-release` 和 `windows-msvc-test`。
- 主要目标至少拆分为 `manga_domain`、`manga_application`、`manga_data`、`manga_services`、`manga_network`、`manga_ui` 和应用程序，避免 Domain/Application 测试链接完整 UI。
- MSVC 使用 `/W4` 和 `/permissive-`；第三方警告不提升为生产代码错误。
- Domain 和 UseCase 使用 Fake Port 单元测试。
- ViewModel 测试 State 序列、初始 observe、无变化抑制、取消和过期结果。
- Adapter 使用契约测试；模型测试覆盖预处理、输出解析和坐标回映。
- Canvas 测试坐标变换、选择、命令和 Undo/Redo。
- UI 变更需要 Light/Dark、正常/窄宽度和关键状态的真实运行检查。

## 13. 已确认的架构决策

| 决策 | 结论 |
| --- | --- |
| UI 模式 | Qt Widgets + MVVM + UDF |
| State | 每屏 ViewModel 持有权威 State |
| 全局 Redux | 不采用 |
| 依赖注入 | `ApplicationContext` Composition Root |
| 应用边界 | `application/ports + application/usecase` |
| 编辑画布 | `QGraphicsView/QGraphicsScene` |
| 图像编辑 | 非破坏性图层，导出时合成 |
| 检测首要目标 | 文字区域，不是完整气泡 |
| 模型运行 | ONNX Runtime，可替换实现 |
| AI 翻译 | 页面/上下文批量翻译，按 Region ID 对齐 |
| 产品模式 | AI 自动处理 + 人工校对 |
| V0.1 项目 | 目录型项目、相对路径、原始资产复制、备份恢复 |
| V0.1 Region | UUID v4、源图像像素坐标、轴对齐矩形 |
| V0.1 OCR | 一个 Manga OCR ONNX 实现，人工输入兜底 |
| V0.1 翻译 | OpenAI-compatible、非流式、结构化响应 |
| V0.1 任务 | `QThreadPool + QRunnable`、`std::stop_token` 取消、`QFuture` 进度、generation |
| Port 异步契约 | `QFuture<std::expected<T, ApplicationError>>`，接口无 QObject/signal |
| 异步结果采用 | 后台结果不直接写 Domain，用户采用后进入 Undo |
| V0.1 图像 | RGBA8、straight alpha、sRGB，导入时应用 EXIF 方向 |
| V0.1 导出 | PNG/JPEG，不覆盖既有文件 |
