# P0：工程与构建基线

> 状态：Complete
> 前置阶段：无
> 输出：可重复配置、构建、测试和启动的 C++23/Qt 6.11 空应用

## 目标

先建立不会把本机路径和第三方实现泄漏到业务层的工程骨架，为后续每个层提供独立 Target 和测试入口。

## TODO

### 工程结构

- [x] 创建根 `CMakeLists.txt`、`CMakePresets.json` 和 `vcpkg.json`。
- [x] 建立 `src/app`、`core`、`domain`、`application`、`services`、`data`、`network`、`ui` 的目标入口。
- [x] 建立 `manga_domain`、`manga_application`、`manga_services`、`manga_data`、`manga_network`、`manga_ui` 和应用可执行目标。
- [x] 只在需要的 Target 开启 `AUTOMOC`、`AUTORCC`、`AUTOUIC`。
- [x] 显式列出源文件，不使用递归 Glob。

### 工具链与依赖

- [x] 要求 C++23、关闭编译器扩展，并配置 MSVC 2022 x64。
- [x] 使用 Qt 6.11 imported targets，不写死 Qt 安装路径。
- [x] 通过 `FluentQt::FluentQt` 和公开 C++ API 链接 FluentQt。
- [x] 固定 vcpkg baseline；普通离线重建不得重新下载不受控依赖。
- [x] 项目 Target 启用 `/W4`、`/permissive-`，隔离第三方警告。
- [x] 提供 Debug、Release、Test Preset，并确保名称与文档一致。

### 测试与自动化

- [x] 启用 CTest，建立 Domain、Application、UI、Contract、Integration 测试目标骨架。
- [x] 添加最小启动测试和不依赖 GUI 的示例单元测试。
- [x] 配置 GitHub Actions `windows-2022` 的 Test/Release 矩阵，确保 PR、合并队列和 `main` 使用同一事件 SHA。
- [x] 确认所有签入的 Preset 均将生成物写入 `build/`。

## 验证

- [x] Debug、Release、Test Preset 均可完成配置。
- [x] 空应用可启动和正常退出。
- [x] Domain/Application 测试可在不链接 UI 的情况下构建运行。
- [x] 干净环境不需要修改源码中的本机路径。

## 完成证据

- Debug 和 Test 构建均通过 6 个 CTest：Domain、Application、UI、Contract、Integration 和应用 offscreen 启动测试。
- Release Preset 可完成配置和构建，且不要求 `Qt6::Test`。
- Domain 测试仅链接 `manga_domain` 和 C++ 标准库；Application 测试不链接 UI。
- CI 直接响应源事件，不使用 `workflow_run` 串联；Qt、vcpkg、FluentQt 和 GitHub Actions 均使用固定版本或提交。
- CI 首次运行将在相关改动推送到 GitHub 后验证，分支保护应要求 `Windows Test` 和 `Windows Release` 两个检查。

## 退出条件

- 所有目标的依赖方向与根 `AGENTS.md` 一致。
- 本地与 CI 使用同一 Preset 可以复现构建和 CTest。
- 后续阶段不需要通过目录级全局配置才能新增源文件或测试。

## 注意事项

- 不在 P0 引入 Repository、全局 Service Locator 或业务单例。
- 不为尚未使用的 Qt 模块和第三方库提前添加依赖。
- FluentQt 初始化只留应用入口，不在此阶段设计页面。
- P5 设计复审新增的 `manga_rendering` 是后续阶段按首个消费方创建的窄目标，不追溯为 P0 未完成项；它必须沿用 P0 的 target-scoped CMake、Qt imported target 和分层测试规则。
