<!-- prettier-ignore -->
<div align="center">

# MangaTranslator

**AI 辅助漫画翻译工作台**

![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![Qt Widgets](https://img.shields.io/badge/Qt-6.11-41CD52?style=flat-square&logo=qt&logoColor=white)
![FluentQt](https://img.shields.io/badge/Fluent-Design-0078D4?style=flat-square)
![CMake](https://img.shields.io/badge/CMake-3.28-064F8C?style=flat-square&logo=cmake&logoColor=white)

[功能特性](#功能特性) • [快速开始](#快速开始) • [项目结构](#项目结构) • [构建指南](#构建指南) • [开发规范](#开发规范)

</div>

---

## 功能特性

MangaTranslator 是一款桌面漫画翻译工作台，提供从导入到导出的完整翻译流程：

- **多格式导入** — 支持 CBZ、PDF 及图片集合
- **智能文本检测** — 基于 ONNX 的漫画文字区域识别
- **OCR 识别** — 自动提取文字内容
- **LLM 翻译** — 集成大语言模型进行高质量翻译
- **修复与排版** — 自动修复原文区域并重新排版
- **人工校对** — 支持逐页审核与编辑
- **批量处理** — 整章流水线自动翻译
- **术语库管理** — 维护全局及项目专属术语

## 快速开始

### 环境要求

- **编译器**: 支持 C++23 的编译器 (MSVC 19.34+, GCC 13+, Clang 17+)
- **Qt**: 6.11 或更高版本
- **CMake**: 3.28 或更高版本
- **Ninja** (推荐): 用于并行构建

### 构建步骤

```powershell
# 配置项目
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON

# 编译
cmake --build build/dev --parallel

# 运行测试
ctest --test-dir build/dev --output-on-failure
```

## 构建指南

### 使用 CMake Presets

项目提供预定义的 CMake Presets：

```powershell
# 查看可用 presets
cmake --list-presets

# 使用开发配置
cmake --preset dev
cmake --build --preset dev
```

### 运行测试

```powershell
# 运行所有测试
ctest --test-dir build/dev --output-on-failure

# 运行特定测试
ctest --test-dir build/dev -R "TestName"
```

## 开发规范

### 代码风格

- 使用 UTF-8 编码，LF 换行
- 四空格缩进，禁止 Tab
- 英文标识符与代码注释
- 遵循 `.clang-format` 配置

### 命名约定

| 元素 | 风格 | 示例 |
|------|------|------|
| 类型 | `PascalCase` | `EditorViewModel` |
| 函数/变量 | `camelCase` | `translatePage` |
| 私有成员 | `m_camelCase` | `m_currentPage` |
| 常量 | `kPascalCase` | `kHandleSize` |
| 命名空间 | 小写 | `manga::application` |

### 提交规范

使用 [Conventional Commits](https://www.conventionalcommits.org/) 格式：

```
<type>(<scope>): <summary>
```

类型: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`, `revert`

示例：
- `feat(manga): add text region resize handles`
- `fix(application): ignore stale OCR completions`
- `docs(ui): define settings navigation behavior`

## 相关资源

- [Qt 6.11 文档](https://doc.qt.io/qt-6/)
- [FluentQt 项目](https://github.com/L-1124/Fluent-Qt)
- [ONNX Runtime](https://onnxruntime.ai/)

---

<div align="center">

**MangaTranslator** — 让漫画翻译更智能

</div>
