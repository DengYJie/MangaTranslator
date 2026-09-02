# MangaTranslator 构建与测试指南 (Build & Run Guide)

## 1. 开发环境要求
- **操作系统**：Windows 10/11 64-bit
- **编译器**：MSVC 2022 / VS 2025（支持 ISO C++23）
- **Qt 版本**：Qt 6.11（Widgets、Core、Gui、Network、Svg）
- **依赖管理**：CMake 3.28+、Ninja 构建工具、vcpkg
- **UI 框架**：FluentQt（链接 `FluentQt::FluentQt`）

---

## 2. 使用 CMake Presets 本地构建

项目已内置经过验证的标准构建预设 `local-test`：

### 2.1 编译主程序
```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build/build.ps1 -Preset local-test -Target MangaTranslatorApp
```

### 2.2 运行全量单元测试与集成测试
```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build/test.ps1 -Preset local-test
```

### 2.3 运行指定模块测试
```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build/test.ps1 -Preset local-test -Regex "manga_tests_(domain|services|integration)"
```

---

## 3. 代码质量与提交规范

- **Conventional Commits**：`<type>(<scope>): <summary>`（例如 `feat(ui): ...`, `fix(services): ...`）；
- **Qt 宏规范**：严格使用 `Q_SIGNALS`、`Q_SLOTS`、`Q_EMIT`（禁止使用 Qt 关键字别名 `signals`/`slots`/`emit`）；
- **Pre-commit Hooks**：每次提交前自动由 `prek` 运行 `clang-format` 格式化并检查代码行尾。
