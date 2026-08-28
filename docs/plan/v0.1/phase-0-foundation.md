# P0：工程与构建基线

> 状态：Planned  
> 前置阶段：无  
> 输出：可重复配置、构建、测试和启动的 C++23/Qt 6.11 空应用

## 目标

先建立不会把本机路径和第三方实现泄漏到业务层的工程骨架，为后续每个层提供独立 Target 和测试入口。

## TODO

### 工程结构

- [ ] 创建根 `CMakeLists.txt`、`CMakePresets.json` 和 `vcpkg.json`。
- [ ] 建立 `src/app`、`core`、`domain`、`application`、`services`、`data`、`network`、`ui` 的目标入口。
- [ ] 建立 `manga_domain`、`manga_application`、`manga_services`、`manga_data`、`manga_network`、`manga_ui` 和应用可执行目标。
- [ ] 只在需要的 Target 开启 `AUTOMOC`、`AUTORCC`、`AUTOUIC`。
- [ ] 显式列出源文件，不使用递归 Glob。

### 工具链与依赖

- [ ] 要求 C++23、关闭编译器扩展，并配置 MSVC 2022 x64。
- [ ] 使用 Qt 6.11 imported targets，不写死 Qt 安装路径。
- [ ] 通过 `FluentQt::FluentQt` 和公开 C++ API 链接 FluentQt。
- [ ] 固定 vcpkg baseline；普通离线重建不得重新下载不受控依赖。
- [ ] 项目 Target 启用 `/W4`、`/permissive-`，隔离第三方警告。
- [ ] 提供 Debug、Release、Test Preset，并确保名称与文档一致。

### 测试与自动化

- [ ] 启用 CTest，建立 Domain、Application、UI、Contract、Integration 测试目标骨架。
- [ ] 添加最小启动测试和不依赖 GUI 的示例单元测试。
- [ ] 配置 GitHub Actions `windows-2022` 的配置、构建和测试流程。
- [ ] 确认所有生成物仅位于 `build/`。

## 验证

- [ ] Debug、Release、Test Preset 均可完成配置。
- [ ] 空应用可启动和正常退出。
- [ ] Domain/Application 测试可在不链接 UI 的情况下构建运行。
- [ ] 干净环境不需要修改源码中的本机路径。

## 退出条件

- 所有目标的依赖方向与根 `AGENTS.md` 一致。
- 本地与 CI 使用同一 Preset 可以复现构建和 CTest。
- 后续阶段不需要通过目录级全局配置才能新增源文件或测试。

## 注意事项

- 不在 P0 引入 Repository、全局 Service Locator 或业务单例。
- 不为尚未使用的 Qt 模块和第三方库提前添加依赖。
- FluentQt 初始化只留应用入口，不在此阶段设计页面。
