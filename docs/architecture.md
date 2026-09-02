# MangaTranslator 系统架构设计 (Architecture Design)

> 技术基线：C++23、Qt Widgets 6.11、FluentQt、CMake
> 功能状态与清单：[`feature-status.md`](feature-status.md)
> 编译构建指南：[`build.md`](build.md)

---

## 1. 架构定位与目标

MangaTranslator 是一个基于 C++23 与 Qt 6.11 构建的 AI 辅助漫画翻译与排版工作台。其核心定位是为漫画翻译、汉化组及个人创作者提供高可控性、非破坏性编辑与批量流式处理的桌面工具。

### 核心架构原则
- **模块解耦与可替换性**：检测、OCR、翻译、修补和排版引擎均通过标准 Port 接口抽象，业务逻辑与具体模型或云端 API 无强绑定；
- **非破坏性资产管道**：原始图像资产与衍生修补图层（`assets/derived`）分离存储，撤销/重做不修改原始文件；
- **确定性光栅化**：`manga_rendering` 模块提供无 Widget 依赖的纯粹渲染管道，确保画布交互预览与最终导出文件像素级一致；
- **异步安全与生命周期隔离**：所有耗时任务（模型推理、网络请求、大图加载）均运行于后台工作线程，通过 `OperationContext`（标准 `std::stop_token`）、UUID 及 Session Generation 防止结果竞争与过期覆盖；
- **单向数据流与权威 State**：采用 MVVM + UDF 架构，ViewModel 持有权威状态快照，View 仅负责状态渲染与用户意图派发。

---

## 2. 总体分层架构

工程采用 Clean Architecture 分层体系，依赖方向严格单向内向：

```text
src/
├── domain/       # 业务领域实体与不变量（ComicProject, ComicPage, TextRegion 等），纯 C++ 标准库
├── application/  # 应用层能力接口 (ports)、用例 (usecase)、工作流 (workflow) 与 DTO
├── rendering/    # 确定性排版与文本光栅化引擎（仅依赖 domain 与 Qt Core/Gui）
├── services/     # 核心本地算法与模型推理适配器（检测、OCR、LaMa 修补、排版）
├── data/         # 项目仓储、导入器、不可变衍生资产存储与导出
├── network/      # LLM 翻译适配器、HTTP 传输层与重试策略
├── ui/           # 视图层（ViewModel、State、FluentQt 界面、MangaCanvas）
├── app/          # 全局组合根（ApplicationContext）与应用程序生命周期
└── core/         # 基础通用工具与日志
```

### 依赖关系图

```text
ui ----------> application ----------> domain
 |                                      ^
 +-----------> rendering ---------------+
                    ^                    ^
                    |                    |
services -----------+----> application --+
data / network ----------> application --+

app/ApplicationContext 负责装配并连接所有层
core 不依赖任何业务层
```

---

## 3. 核心领域与数据模型 (Domain Layer)

Domain 层不感知 QWidget、网络、数据库或 ONNX Runtime，仅维护领域约束与数据不变量：

- `ComicProject`：项目根聚合实体，包含项目元数据、页面序列、源/目标语言、阅读方向及全局设置；
- `ComicPage`：单页聚合，维护页面身份（PageId UUID）、原始图像路径、尺寸及页面内所有文本选区；
- `TextRegion`：文本选区实体，维护几何轮廓（支持轴对齐矩形、旋转矩形及任意多边形顶点）、原文内容、翻译候选、人工修改、修补图层引用及排版样式；
- `TextLayout`：排版属性，包含字体族、字号、字距、行距、文字方向（横排/竖排）、对齐方式、描边（宽度与颜色）、外发光与阴影；
- `TranslationEntry`：维护原文、AI 生成候选列表、当前采纳译文及人工覆盖标记；
- `ReadingDirection`：漫画阅读顺序（RTL 右至左、TTB 上至下、LTR 左至右）。

---

## 4. 应用用例与工作流 (Application Layer)

### 4.1 端口契约 (Ports)
所有外部能力均抽象为纯虚接口，异步方法返回 `QFuture<Result<T>>`（其中 `Result<T> = std::expected<T, ApplicationError>`），取消操作通过 `OperationContext` 传递 `std::stop_token`：
- `IComicImporter` / `IPageImageLoader`：漫画导入与图像流式加载；
- `IProjectRepository`：项目元数据持久化、事务保存与备份恢复；
- `ITextDetector`：漫画文本气泡检测（ONNX Comic Text Detector / Koharu）；
- `IOcrEngine`：文字识别引擎（Baberu / PaddleOCR-VL-1.6 ONNX）；
- `ITranslator`：AI 翻译适配器（ChatCompletions 协议）；
- `IInpainter`：图像修补引擎（LaMa ONNX / 纯色填充）；
- `ITextLayouter`：排版测量与折行计算；
- `IComicExporter`：多图层合并与高精度图像导出。

### 4.2 批量工作流 (Batch Workflow)
`BatchPagesProcessingWorkflow` 协调全项目/多页跨页的整章自动处理：
- 采用**逐页流式内存管理**：依次执行「检测 → OCR → 翻译 → 修补 → 排版」；
- 单页处理完成后立即通过事务原子提交至项目，避免整章数据积压在内存或崩溃丢失已完成进度；
- 具备全局 Stop Token 支持，随时响应用户取消或暂停。

---

## 5. 本地服务与网络适配器 (Services & Network)

### 5.1 文本检测与 OCR
- **Comic Text Detector**：基于 ONNX Runtime，结合 DBNet 后处理生成文本框与多边形掩膜；
- **OCR 引擎**：支持 Baberu ONNX 与 PaddleOCR-VL-1.6 ONNX，包含 BPE 分词器、贪心搜索生成与模型清单校验机制（`manifest.json` 与 SHA-256 完整性校验）。

### 5.2 图像修补 (Inpainting)
- **LaMa ONNX**：高分辨率图像切块规划（`LamaTilePlanner`）、动态掩膜生成（`LamaMaskBuilder`）与 CPU 推理；
- **纯色填充**：对话气泡纯色背景快速取色覆盖；
- **衍生资产管理**：修补图层作为不可变 PNG 资产存储在 `assets/derived/<pageId>/` 下，撤销/重做仅切换图层引用，零额外磁盘 I/O。

### 5.3 翻译与网络韧性
- **OpenAiCompatibleTranslator**：兼容 OpenAI 标准 ChatCompletions 接口；
- **独立网络线程**：`QtHttpTransport` 运行于专属 QThread，避免网络 I/O 阻塞 GUI；
- **重试策略**：`RetryPolicy` 支持 429 速率限制等待（遵守 Retry-After 头）与指数退避重试；
- **结构化对齐**：请求携带 `RegionId`，响应严格对齐文本选区，防止未知 ID 污染领域数据。

---

## 6. 确定性渲染与排版 (Rendering Layer)

`manga_rendering` 模块独立于 UI 与 Widget：
- `TextLayoutEngine`：根据可用区域、阅读方向、字体和字距行距计算字形排布与自动换行，采用二分法精确计算最佳字号；
- `TextRasterizer`：在源图像像素坐标下进行高精度文字光栅化，支持平滑描边、外发光与阴影；
- 画布实时预览与最终文件导出均共享此渲染核心，保证两者所见即所得、100% 像素一致。

---

## 7. 视图与界面层 (UI Layer)

### 7.1 MVVM 与单向数据流 (UDF)
```text
View (MangaCanvas / Inspector)
  │ (用户操作 / 意图方法)
  ▼
ViewModel<EditorState>
  │ (调用 UseCase / Workflow)
  ▼
Application Layer
  │ (更新 State)
  ▼
ViewModel::updateState()
  │ (发出 stateChanged 信号)
  ▼
View::render(State)
```

### 7.2 画布与编辑交互 (MangaCanvas)
- 基于 `QGraphicsView` / `QGraphicsScene` 构建二维漫画编辑画布；
- 支持源图像图层、修补图层、多边形/矩形选区覆盖层、文字排版层与交互控制句柄；
- 图元仅作为 State 的可视化投影，交互产生的编辑意图通过 ViewModel 统一转化为 Domain Command 并推入 Undo/Redo 栈。

### 7.3 界面导航与多 Surface 架构
- `NavigationWindow` 提供双 Surface 导航（主工作区 `main` 与设置界面 `settings`）；
- 标题栏与 Windows 11 Fluent 风格原生融合，集成文件与处理菜单栏；
- 进入设置界面时标题栏自动显示返回按钮，点击一键返回主工作区。

---

## 8. 持久化与数据安全

- **项目目录结构**：
  ```text
  ProjectRoot/
  ├── project.json    # 项目元数据与选区编辑配置（JSON Schema 2020-12）
  └── assets/
      ├── original/   # 原始漫画图像（只读，防破坏）
      └── derived/    # 衍生修补图层与缩略图缓存
  ```
- **原子保存与恢复机制**：
  - 保存时先写入同盘临时文件，校验完整后原子替换目标文件；
  - 自动保留 `.bak` 备份文件，异常退出时提示并支持一键恢复；
  - API Key 等敏感信息使用操作系统凭据库（Windows Credential Manager）加密存储，严禁写入项目文件或日志。
