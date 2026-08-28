# P1：Domain 与项目格式

> 状态：Planned  
> 前置阶段：P0  
> 输出：可独立测试的领域模型、业务不变量和 V0.1 项目 Schema

## 目标

冻结 V0.1 的用户数据含义，避免 UI、存储和处理适配器分别发明页面、Region、文字和图层格式。

## TODO

### 领域模型

- [ ] 定义 `ComicProject`、`ComicPage`、`TextRegion`、`TranslationEntry`、`TextLayout` 和非破坏性图层引用。
- [ ] 定义 Page/Region UUID v4 创建、比较和小写标准字符串序列化规则。
- [ ] 定义源图像像素坐标下的 `rect` 几何及 `double` 精度。
- [ ] 实现 2×2 最小尺寸、创建越界拒绝、编辑结果裁剪规则。
- [ ] 保存稳定 `orderIndex`；不实现独立阅读顺序工具。
- [ ] 区分模型译文、候选译文和人工译文，并定义人工版本优先级。
- [ ] 定义字体、字号、横排/竖排、对齐、换行、行距、边距和自动适配数据。
- [ ] 定义原图、清除层、译文层和人工覆盖层的组合顺序。

### 项目格式

- [ ] 定义 `project.json` 的 JSON Schema Draft 2020-12。
- [ ] 定义 `projectVersion`、`schemaVersion`、`formatVersion` 的职责与初始值。
- [ ] 定义 `assets/original/`、`assets/derived/` 和相对资产引用。
- [ ] 定义项目语言、阅读方向、处理配置和项目术语的保存结构。
- [ ] 定义 Dirty、人工修改来源和异步输入版本字段。
- [ ] 定义未知新版本拒绝、结构校验和语义校验错误。

## 测试

- [ ] 合法、过小、负尺寸和越界 Region。
- [ ] UUID 创建、序列化、反序列化和复制项目时稳定性。
- [ ] 页面顺序、Region、文字、排版、图层和人工标记的往返一致性。
- [ ] JSON Schema 合法/非法样本、缺失必填字段和未知版本。
- [ ] Domain 测试不链接 QWidget、网络、数据库或 ONNX Runtime。

## 退出条件

- 最小项目可往返序列化且不丢失任何用户编辑数据。
- 所有领域不变量在 Domain 内可直接验证，不依赖 UI 补救。
- V0.1 Schema 冻结；新增字段必须保持兼容或先设计迁移。

## 注意事项

- Schema 预留 `geometry.type` 不代表 V0.1 支持多边形或旋转。
- 派生资产可重建，用户文字、Region 和人工排版不可只存在缓存中。
- 不将 `QGraphicsItem`、HTTP DTO、ONNX Tensor 或数据库 Row 作为领域类型。
