# Remove Inpaint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在编辑器画布增加 Koharu 式 Remove 工具，使用本地 dynamic ONNX inpaint bundle，笔刷松开后立即生成并可撤销地更新页面清理层。

**Architecture:** 保留现有 Region 级 `IInpainter/SolidColorInpainter`。新增 page-level `IPageInpainter`、dynamic LaMa ONNX 适配器和手绘 inpaint 工作流；UI 只提交源图坐标点列与笔刷直径，应用层负责 mask、异步任务、stale guard、资产写入和原子 Undo。

**Tech Stack:** C++23、Qt 6.11 Widgets/Concurrent/Gui、FluentQt、ONNX Runtime、CMake。

**Spec:** 本会话已批准的快速实现设计。

## Global Constraints

- Remove 图标必须使用 `Typography::Icons::glyph(QStringLiteral("ic_fluent_square_hint_sparkles_16_regular"))`。
- 首版只实现 dynamic spatial bundle；manifest 保留后续增加 fixed adapter 的扩展点。
- 不硬编码 `F:` 路径，不提交 `models/` 或 ONNX 权重。
- 同一页面只允许一个 Remove job；PointerUp 立即运行，Escape/PointerCancel 取消未提交 stroke。
- 成功结果自动采纳为一个可撤销页面清理层命令，不显示 Adopt/Reject。

---

### Task 1: Inpaint bundle 配置与构造

**Files:**
- Modify: `src/application/dto/AppSettingsDto.h`
- Create: `src/application/dto/InpaintModelDto.h`
- Modify: `src/application/usecase/settings/AppSettingsUseCases.cpp`
- Create: `src/application/ports/IInpaintModelValidator.h`
- Create: `src/services/inpaint/InpaintManifest.{h,cpp}`
- Create: `src/services/inpaint/InpaintModelValidator.{h,cpp}`
- Modify: `src/ui/screen/settings/SettingsPage.cpp`
- Modify: `src/ui/screen/settings/SettingsViewModel.{h,cpp}`
- Modify: `src/app/ApplicationContext.{h,cpp}`
- Modify: `src/services/CMakeLists.txt`

**Produces:** `AppSettingsSnapshot::inpaintModelDirectory`、`InpaintModelStatus` 和经验证的 dynamic manifest。

- [ ] 定义最小 manifest：模型文件、input/output 名、NCHW RGB、0..1、dynamic spatial、padMultiple=8、tileCore=512、tileContext=128、ORT>=1.20。
- [ ] 设置页增加模型目录选择与状态 InfoBar；本机选择 `F:/source/MangaTranslator/models/inpaint/ogkalu`，源码不保存该绝对路径。
- [ ] 无效 bundle 时禁用 Remove，并提供“设置 > 修复模型目录”指引。
- [ ] 构建 `manga_services` 与 `manga_ui`。

### Task 2: Mask 与 dynamic ONNX 推理

**Files:**
- Create: `src/application/dto/PageInpaintDto.h`
- Create: `src/application/ports/IPageInpainter.h`
- Create: `src/rendering/inpaint/StrokeMaskRasterizer.{h,cpp}`
- Create: `src/services/inpaint/OnnxLamaPageInpainter.{h,cpp}`
- Modify: `src/app/ApplicationContext.{h,cpp}`
- Modify: `src/rendering/CMakeLists.txt`
- Modify: `src/services/CMakeLists.txt`

**Key interface:**

```cpp
class IPageInpainter {
public:
    virtual QFuture<Result<PageInpaintResponse>> inpaint(
        PageInpaintRequest request, OperationContext context) = 0;
};
```

- [ ] 将源图坐标点列和 1–128 px 直径栅格化为抗锯齿灰度 mask，并返回 dirty bounds。
- [ ] dirty bounds 按 512 core + 128 context 切块，pad 到 8 倍数；输入转 RGB float32 NCHW 0..1。
- [ ] 推理输出只在 mask 内写回，未遮罩像素逐字节保持不变；每块检查 `OperationContext` 取消状态。
- [ ] 最小契约验证：mask/bounds、非 mask 像素不变、取消返回 cancelled。

### Task 3: 页面级工作流、stale guard 与 Undo

**Files:**
- Create: `src/application/usecase/processing/ManualInpaintWorkflow.{h,cpp}`
- Create: `src/application/usecase/apply/PreparePageInpaintResultUseCase.{h,cpp}`
- Create: `src/ui/command/PageClearLayerCommand.{h,cpp}`
- Modify: `src/ui/screen/editor/EditorProcessingController.{h,cpp}`
- Modify: `src/ui/screen/editor/EditorViewModel.{h,cpp}`
- Modify: `src/application/CMakeLists.txt`
- Modify: `src/ui/CMakeLists.txt`

**Produces:** `EditorProcessingController::inpaintStroke(pageId, points, diameter)` 和单步 Undo/Redo。

- [ ] 工作流把原图与当前 `clearLayerAsset` 合成为模型 source，调用 `IPageInpainter`，并写入新的不可变 clear-layer asset。
- [ ] 启动时捕获 project/session/page/imageSize/旧 clear asset；完成时任一不匹配即丢弃结果，不覆盖新状态。
- [ ] 成功后自动 push `PageClearLayerCommand(oldAsset, newAsset)`；Undo/Redo 只切换 asset 引用并刷新预览/导出。
- [ ] 每页单任务；重复请求在前一个完成前拒绝，现有停止入口可取消。
- [ ] 用 fake inpainter 覆盖成功与 stale-result 两条高风险路径。

### Task 4: Koharu 式 Remove 画布交互

**Files:**
- Modify: `src/ui/screen/editor/EditorState.h`
- Modify: `src/ui/screen/editor/EditorViewModel.{h,cpp}`
- Modify: `src/ui/manga/MangaCanvas.{h,cpp}`
- Modify: `src/ui/screen/editor/EditorPage.{h,cpp}`

- [ ] `CanvasTool` 增加 `Remove`；浮动工具栏增加互斥 FluentQt 按钮并使用指定 Sparkles glyph。
- [ ] 默认直径 48 px，范围 1–128 px；Alt+滚轮调整；画布显示缩放正确的圆形游标。
- [ ] PointerDown/Move 记录源图点列并绘制半透明本地 stroke preview；Escape、PointerCancel、页面/工具切换清除 preview。
- [ ] PointerUp 发出一次 `removeStrokeCommitted(points, diameter)`；EditorPage 转给 controller，运行期间禁用新 stroke。
- [ ] 实际应用冒烟：绘制一次、观察任务、确认清理层更新、Undo、Redo、取消。

### Task 5: 集成收尾

**Files:**
- Modify: `docs/ui/pages/editor.md`
- Modify: `docs/ui/pages/settings.md`

- [ ] 记录 Remove、Alt+滚轮、Escape、模型目录和单页单任务语义。
- [ ] 格式化改动文件；构建 `MangaTranslatorApp`。
- [ ] 运行上述最小契约验证与实际 Remove 冒烟，不扩展为完整测试矩阵。
- [ ] 按任务边界提交：`feat(application)`、`feat(services)`、`feat(ui)`、`docs(ui)`。
