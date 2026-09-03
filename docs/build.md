# MangaTranslator 构建与测试指南 (Build & Run Guide)

## 1. 开发环境要求
- **操作系统**：Windows 10/11 64-bit
- **编译器**：MSVC 2022 / VS 2025（支持 ISO C++23）
- **Qt 版本**：Qt 6.11（Widgets、Core、Gui、Network、Svg）
- **依赖管理**：CMake 3.28+、Ninja 构建工具、vcpkg
- **UI 框架**：FluentQt（链接 `FluentQt::FluentQt`）

---

## 2. 使用 CMake Presets 本地构建

仓库预设定义在 `CMakePresets.json`：`windows-msvc-debug`、`windows-msvc-release`（测试关闭）、`windows-msvc-test`（Debug 且 `BUILD_TESTING=ON`）。`local-debug` / `local-release` / `local-test` 来自各人的 `CMakeUserPresets.json`，该文件不入库、只在本机存在，里面按机器设置 `QTDIR`。产物目录固定为 `build/<预设名>/`。

`tools/build/` 下两个脚本会自行进入 VS 开发者环境，因此推荐从它们走；直接在 PowerShell 里敲 `cmake` / `ctest` 则要先用 `vswhere.exe` + `Enter-VsDevShell` 加载工具链，否则 `cmake` 与 `cl` 都不在 PATH 中。

### 2.1 首次配置（新增源文件或改过 CMake 时才需要）
```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build/build.ps1 -Preset local-test -Configure
```

### 2.2 编译主程序
```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build/build.ps1 -Preset local-test -Target MangaTranslatorApp
```

### 2.3 运行全量单元测试与集成测试
```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build/test.ps1 -Preset local-test
```

### 2.4 运行指定模块测试
```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build/test.ps1 -Preset local-test -Regex "manga_tests_(domain|services|integration)"
```

---

## 3. 脚本开关与日志位置

- `build.ps1`：`-Preset`、`-Target`、`-Configure`、`-ListTargets`（列出真实可构建目标）、`-LogFile`。
- `test.ps1`：`-Preset`、`-Regex`、`-List`、`-RerunFailed`、`-StopOnFailure`、`-ShowTest <名称>`（只回看上一次的输出块，不重跑）、`-LogFile`。
- 完整日志：`build/<preset>/build.log`、`build/<preset>/ctest.log`；单个测试的逐条输出在 `build/<preset>/Testing/Temporary/LastTest.log`。过滤后的输出不够时直接查这三个文件，不要重跑整个构建。
- 多个 `git worktree` 想共用依赖源码，可在机器上设一次 `FETCHCONTENT_BASE_DIR`（如某个缓存盘目录），`build.ps1` 配置阶段会把它转成 `-DFETCHCONTENT_BASE_DIR`；没设过就要在每个 worktree 里重下 FluentQt 与 QWindowKit。

---

## 4. 代码质量与提交规范

- **Conventional Commits**：`<type>(<scope>): <summary>`（例如 `feat(ui): ...`, `fix(services): ...`）；
- **Qt 宏规范**：严格使用 `Q_SIGNALS`、`Q_SLOTS`、`Q_EMIT`（禁止使用 Qt 关键字别名 `signals`/`slots`/`emit`）；
- **Pre-commit Hooks**：每次提交前自动由 `prek` 运行 `clang-format` 格式化并检查代码行尾。
