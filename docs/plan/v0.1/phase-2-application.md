# P2：Application 契约与 UseCase

> 状态：Planned  
> 前置阶段：P1  
> 输出：可由 Fake Port 完整驱动的 V0.1 业务流程

## 目标

固定应用能力、错误、异步和结果采用边界，使数据层、处理适配器和 UI 能独立开发。

## TODO

### Ports 与公共结果

- [ ] 定义 `IProjectRepository`、`IComicImporter`、`IOcrEngine`、`ITranslator`、`IInpainter`、`ITextLayouter`、`IComicExporter`。
- [ ] 定义结构化错误类别、用户安全摘要和可恢复建议。
- [ ] 定义取消令牌、进度单位、不可变输入快照和请求 generation。
- [ ] 定义任务身份中的 `projectId`、`pageId`、Region ID 集合和操作 ID。
- [ ] 定义 Port DTO 与 Domain 的显式映射，禁止第三方类型穿透接口。

### UseCases

- [ ] 项目：创建、打开、保存、另存、关闭和恢复。
- [ ] 导入：图片/文件夹选择、校验、排序和加入项目。
- [ ] Region：创建、删除、移动、缩放和顺序维护。
- [ ] 文本：编辑原文、译文和候选采用。
- [ ] 处理：单 Region/单页 OCR、单页翻译、清除和排版。
- [ ] 导出：单页、选中页和整个项目。
- [ ] 定义提交异步结果前的项目、页面、generation、输入版本和人工优先检查。

### 翻译协议

- [ ] 定义单页请求、稳定 Region ID、语言、阅读方向和有效项目术语输入。
- [ ] 定义非流式结构化响应 Schema。
- [ ] 缺失 ID 只使对应 Region 失败；重复 ID 使整次响应失败；未知 ID 忽略并形成安全诊断。
- [ ] 禁止按响应数组位置匹配 Region。

## 测试

- [ ] 每个 UseCase 的成功、输入错误、Port 失败、取消和过期结果。
- [ ] 关闭项目、切换页面及 generation 变化后的迟到结果。
- [ ] 人工译文、几何和排版不被后台结果覆盖。
- [ ] 翻译缺失、重复、未知 Region ID 和部分成功。
- [ ] Fake Port 不访问文件系统、网络、凭据或 GUI。

## 退出条件

- P3、P4、P5 只依赖稳定 Port 和 Domain 即可工作。
- 所有长操作都有可取消、可报告进度、可拒绝过期结果的契约。
- Application 不依赖 QWidget、FluentQt、HTTP、ONNX Runtime 或具体存储实现。

## 注意事项

- UseCase 负责业务协调，不承担页面状态渲染或底层格式解析。
- 不为单一调用创建跨层 Manager；优先保留明确 Port 与 UseCase。
- 保存不是 Undo 命令，异步结果只有用户明确采用时才进入 Undo 历史。
