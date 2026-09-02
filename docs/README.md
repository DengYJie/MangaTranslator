# MangaTranslator 文档

MangaTranslator 是基于 C++23、Qt 6.11 与 ONNX Runtime 的漫画翻译与排版桌面工作台。

---

## 目录索引

- [系统架构 (Architecture)](architecture.md)：系统分层架构、数据流向、线程模型与生命周期管理。
- [功能清单与规划 (Feature Status)](feature-status.md)：已实现功能表与后续改进规划。
- [编译与测试 (Build & Test)](build.md)：本地环境依赖、CMake Presets 编译与测试运行命令。
- [模型集成规范 (Models)](models.md)：OCR、文本检测与图像修补模型的 ONNX 导出及 manifest.json 校验规范。
- [常见问题 (FAQ)](faq.md)：模型下载镜像配置、翻译 API 设置与数据恢复。
- [界面设计规范 (UI & UX)](ui/README.md)：Fluent 设计系统、自研画布与界面控件说明。
