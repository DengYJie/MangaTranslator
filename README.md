<!-- prettier-ignore -->
<div align="center">
  <img src="res/icons/manga-translator-app.svg" alt="MangaTranslator Logo" width="120" height="120" />
  <h1>MangaTranslator</h1>
  <p>AI 辅助漫画翻译与非破坏性本地化桌面工作台</p>

  <p>
    <a href="https://isocpp.org/"><img src="https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat-square&logo=cplusplus&logoColor=white" alt="C++23" /></a>
    <a href="https://www.qt.io/"><img src="https://img.shields.io/badge/Qt-6.11-41CD52?style=flat-square&logo=qt&logoColor=white" alt="Qt Widgets" /></a>
    <a href="https://github.com/L-1124/Fluent-Qt"><img src="https://img.shields.io/badge/Fluent-Design-0078D4?style=flat-square" alt="FluentQt" /></a>
    <a href="https://onnxruntime.ai/"><img src="https://img.shields.io/badge/ONNX-Runtime-005CED?style=flat-square" alt="ONNX Runtime" /></a>
  </p>
</div>

MangaTranslator 是一款高性能桌面应用，旨在为漫画翻译与本地化提供高度自动化的工作流。项目深度整合了先进的 ONNX 深度学习模型与现代 C++23 / Qt 6.11 核心，提供从图像导入到自动排版及最终导出的端到端、非破坏性编辑管线。

## 功能特性

- **多格式导入**：原生支持 CBZ 压缩包、多页 PDF 以及松散图片目录的无缝解析与页面索引。
- **智能文本检测**：基于 Koharu RF-DETR 模型，自动提取文本区域及异形气泡的 BoundingBox 与多边形 Mask。
- **漫画专用 OCR**：集成 Baberu Manga OCR 引擎，高精度提取中日韩（CJK）横排与竖排文本。
- **上下文感知翻译**：通过 OpenAI 兼容接口连接 LLM 服务，注入页面级上下文与项目专属术语库（Glossary），确保译文精准地道。
- **非破坏性图像修复**：双引擎驱动（LaMa 大掩码修复模型 + 纯色填充）实现原台词的无痕擦除，同时叠加透明度画笔/橡皮擦图层供人工微调。
- **自动化智能排版**：纯 C++ 确定性渲染引擎，原生支持中文纵书直排（竖排）、字号自适应收缩、多行对齐与文字描边合成。
- **交互式画布编辑器**：提供 8 向缩放手柄、旋转工具、翻译成品侧向对比预览视图，并自带完整的 Undo/Redo 历史栈。
- **批处理状态机**：支持以整章为单位，全自动流水线执行“检测 → OCR → 翻译 → 修复”任务，并提供全局活动中心进行进度监控与断点恢复。

## 架构设计

MangaTranslator 严格遵循 **六边形架构 (Hexagonal Architecture / Ports and Adapters)** 以及带下单向数据流的 MVVM 模式。

> [!NOTE]
> 领域核心与应用逻辑与 UI 层完全解耦。UI 组件（Qt Widgets）仅通过预定义的 UseCases 与组合根 `ApplicationContext` 发生单向通信。

- `domain`：核心业务实体（Project、Page、TextRegion）与业务不变量。
- `application`：业务用例（UseCases）、批处理工作流与外部能力端口（Ports）定义。
- `rendering`：纯文本测算与光栅化渲染引擎，不依赖任何 GUI 框架状态。
- `services` / `network` / `data`：端口的适配器实现（包括 ONNX Runtime 推理、LLM HTTP 客户端、SQLite 持久化）。
- `ui`：遵循 Fluent Design 的交互界面、图形场景（Graphics Scene）与视图模型（ViewModels）。

## 环境要求

在本地构建 MangaTranslator 前，请确保已安装以下依赖：

- 支持 **C++23** 的现代编译器（MSVC 19.34+, GCC 13+, 或 Clang 17+）
- **Qt 6.11** 或更高版本（需包含 Widgets 模块）
- **CMake 3.28** 或更高版本
- **Ninja** 构建系统（推荐，用于并行加速）
- **vcpkg** 包管理器

> [!IMPORTANT]
> 应用程序的检测、OCR 及图像修复能力依赖特定的 ONNX 权重文件。在运行前，必须将对应模型放置在 `res/models/` 目录下。模型下载链接与规格说明请查阅 `res/AGENTS.md`。

## 快速开始

### 构建项目

项目提供了预定义的 CMake Presets 以简化构建流程。在项目根目录下执行：

1. 应用开发配置：
   ```powershell
   cmake --preset dev
   ```

2. 编译可执行程序：
   ```powershell
   cmake --build build/dev --parallel
   ```

### 运行测试

项目包含覆盖整个处理管线的完备单元测试与集成测试矩阵（E01-E05 场景）。

```powershell
# 运行全部测试并输出失败日志
ctest --test-dir build/dev --output-on-failure

# 运行特定测试模块
ctest --test-dir build/dev -R "DomainTests"
```

## 开发规范

- **代码格式化**：项目配置了 `.clang-format` 门禁。提交前请使用 `prek run --all-files` 确保 C++ 代码格式合规。
- **提交约定**：遵循 Conventional Commits 规范（例如：`feat(manga): add brush tool`）。
- **文档契约**：架构决策与模块契约分散在 `src/` 各子目录下的 `AGENTS.md` 中。修改任何模块前，请先阅读该目录最近的 `AGENTS.md`。

> [!TIP]
> 如果您准备进行 UI 相关的开发，请优先阅读 `docs/ui/README.md` 与 `docs/ui/foundations.md`，以深入了解项目的 Fluent Design 令牌系统（Token System）及 MVVM 单向数据流约束。
