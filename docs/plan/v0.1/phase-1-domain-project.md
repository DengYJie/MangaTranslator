# P1：Domain 与项目格式

> 状态：Complete
> 前置阶段：P0  
> 输出：可独立测试的领域模型、业务不变量和 V0.1 项目 Schema

## 目标

冻结 V0.1 的用户数据含义，避免 UI、存储和处理适配器分别发明页面、Region、文字和图层格式。

## TODO

### 领域模型

- [x] 定义 `ComicProject`、`ComicPage`、`TextRegion`、`TranslationEntry`、`TextLayout` 和非破坏性图层引用。
- [x] 定义 Page/Region UUID v4 创建、比较和小写标准字符串序列化规则。
- [x] 定义源图像像素坐标下的 `rect` 几何及 `double` 精度。
- [x] 实现 2×2 最小尺寸、创建越界拒绝、编辑结果裁剪规则。
- [x] 保存稳定 `orderIndex`；不实现独立阅读顺序工具。
- [x] 区分模型译文、候选译文和人工译文，并定义人工版本优先级。
- [x] 定义字体、字号、横排/竖排、对齐、换行、行距、边距和自动适配数据。
- [x] 定义原图、清除层、译文层和人工覆盖层的组合顺序。
- [x] 定义文本来源状态：OCR 自动识别、人工修正 OCR、纯手动输入，记录文本来源与可信度。
- [x] 定义 Region 状态标志：`enabled`（是否参与处理）、`cleared`（是否已清除背景）、`translationHidden`（是否隐藏译文）、`locked`（是否锁定防编辑）。
- [x] 定义图片替换规则：原图尺寸变化时拒绝加载并提示用户。

### 项目格式

- [x] 定义 `project.json` 的 JSON Schema Draft 2020-12。
- [x] 定义 `projectVersion`、`schemaVersion`、`formatVersion` 的职责与初始值。
- [x] 定义 `assets/original/`、`assets/derived/` 和相对资产引用。
- [x] 定义项目语言、阅读方向、处理配置和项目术语的保存结构。
- [x] 定义 Dirty、人工修改来源和异步输入版本字段。
- [x] 定义未知新版本拒绝、结构校验和语义校验错误。

## 测试

- [x] 合法、过小、负尺寸和越界 Region。
- [x] UUID 创建、序列化、反序列化和复制项目时稳定性。
- [x] 页面顺序、Region、文字、排版、图层和人工标记的往返一致性。
- [x] JSON Schema 合法/非法样本、缺失必填字段和未知版本。
- [x] Domain 测试不链接 QWidget、网络、数据库或 ONNX Runtime。
- [x] 文本来源状态：OCR/人工修正/手动输入的创建、序列化和优先级回退。
- [x] Region 状态标志：各标志独立设置、组合存储和往返一致性。
- [x] 图片替换：尺寸变化时坐标缩放精度、尺寸不匹配时拒绝加载。

## 退出条件

- 最小项目可往返序列化且不丢失任何用户编辑数据。
- 所有领域不变量在 Domain 内可直接验证，不依赖 UI 补救。
- V0.1 Schema 冻结；新增字段必须保持兼容或先设计迁移。

## 注意事项

- Schema 预留 `geometry.type` 不代表 V0.1 支持多边形或旋转。
- 派生资产可重建，用户文字、Region 和人工排版不可只存在缓存中。
- 不将 `QGraphicsItem`、HTTP DTO、ONNX Tensor 或数据库 Row 作为领域类型。

---

## 设计规格参考

> 以下为各领域的设计细节，供实现时参考。

### 领域模型字段定义

#### `ComicProject`

```cpp
ProjectId id;
QString name;
QString projectVersion;      // "0.1.0"
int schemaVersion;           // 1
int formatVersion;           // 1

LanguageCode sourceLanguage;
LanguageCode targetLanguage;
ReadingDirection readingDirection;

QVector<ComicPage> pages;
ProjectSettings settings;
QVector<GlossaryEntry> glossary;
```

#### `ComicPage`

```cpp
PageId id;
AssetRef originalAsset;
QSize imageSize;
int orderIndex;
QVector<TextRegion> regions;
```

#### `TextRegion`

```cpp
RegionId id;
RectGeometry geometry;
int orderIndex;

TextContent source;
TranslationEntry translation;
TextLayout layout;
ClearSettings clear;

bool enabled;
bool locked;
bool translationVisible;

quint64 contentRevision;
```

#### `RectGeometry`

```cpp
struct RectGeometry {
    double x;
    double y;
    double width;
    double height;
};
```

不用 `left/right/bottom`，JSON 更直观，与 `QRectF` 一致。坐标为**原图像素坐标**。

#### `TextLayout`

```cpp
QString fontFamily;
double fontSizePt;

TextOrientation orientation; // Horizontal / Vertical
Qt::Alignment alignment;

double lineSpacing;          // 倍数，例如 1.0 / 1.2
QMarginsF margins;           // 四边独立

bool wordWrap;
bool autoFit;

double minFontSizePt;
double maxFontSizePt;

double rotationDeg;
```

行距用**倍数**，边距四边独立。

### 译文优先级

```text
manualText 有值 → manual
selectedCandidateId 有效 → candidate
modelText 有值 → model
否则 → ""
```

```cpp
QString effectiveText() const;
```

不要"候选列表自动挑第一个"。

### 图层组合

V0.1 支持：

```text
Original → Clear → Translation → ManualOverride
```

混合模式 V0.1：Normal only, opacity = 1.0。Schema 预留 `blendMode` 和 `opacity`。

### `orderIndex` 规则

```cpp
int orderIndex;
```

规则：`0, 1, 2, 3...`，页面和 Region 分别维护。插入/删除/排序后重新规范化为连续整数。不使用浮点排序值。

### 项目格式

#### 版本号

```json
{
  "projectVersion": "0.1.0",
  "schemaVersion": 1,
  "formatVersion": 1
}
```

- `projectVersion` — 创建项目时对应的产品版本
- `schemaVersion` — project.json Schema 版本
- `formatVersion` — 项目目录/资产组织格式版本

迁移主要看 `schemaVersion + formatVersion`。

#### 资产命名

```text
assets/
└── original/
    ├── page_0001.png
    ├── page_0002.png
    └── page_0003.jpg
```

文件名人类可读，内部身份使用 `PageId UUID`。

派生文件：

```text
assets/derived/
└── <page-uuid>/
```

#### 错误码

```cpp
enum class ProjectErrorCode {
    UnsupportedSchemaVersion,
    UnsupportedFormatVersion,
    InvalidStructure,
    InvalidSemanticData,
    MissingAsset
};
```

消息示例：`Unsupported schema version: 3, maximum supported version is 1.`

Domain 不生成 UI 文案。

#### 结构校验 vs 语义校验

**结构校验**（Schema）：
- 字段是否存在
- 类型是否正确
- enum 是否合法
- UUID 是否是字符串
- rect 是否包含 x/y/width/height

**语义校验**（Domain）：
- width >= 2, height >= 2
- Region 不得创建在图像外
- orderIndex 不得重复
- PageId/RegionId 不得重复
- selectedCandidate 必须真实存在

### 迁移策略

```text
schemaVersion: 1 → 2 → 3
```

每次只支持逐级迁移：`migrateV1ToV2()`, `migrateV2ToV3()`。

加载规则：
- `version == current` → 直接读取
- `version < current` → 顺序 migrate
- `version > current` → 拒绝打开

`formatVersion` 同理，只在目录/资产组织变化时递增。

### 测试规格

#### 最小合法 JSON

```json
{
  "projectVersion": "0.1.0",
  "schemaVersion": 1,
  "formatVersion": 1,
  "name": "Test",
  "pages": [
    {
      "id": "uuid",
      "orderIndex": 0,
      "image": "assets/original/page_0001.png",
      "regions": []
    }
  ]
}
```

#### 非法样本覆盖

- 缺 schemaVersion
- 错误字段类型
- 负 width
- width < 2
- Region 越界
- 重复 Page UUID
- 重复 Region UUID
- 未知 schemaVersion
- 非法 orientation
- selectedCandidate 不存在

#### 往返一致性断言

```text
serialize → deserialize → compare
```

至少断言：
- Project ID 相同
- Page ID 相同
- 页面顺序相同
- Region ID 相同
- rect 完全一致
- sourceText 一致
- manual/model/candidate 一致
- effectiveText 一致
- TextLayout 一致
- ClearSettings 一致
- 人工修改标记一致

### 退出条件补充

#### "最小项目"定义

```text
1 个 Project
1 个 Page
1 个 TextRegion
1 个 sourceText
1 个 translation
1 个 TextLayout
1 个 ClearSettings
```

不使用 `regions=[]` 作为最终验收样本。

#### V0.1 领域不变量

- Project/Page/Region UUID 唯一，序列化为小写标准格式
- page.orderIndex 唯一且 >= 0
- region.orderIndex 唯一且 >= 0
- rect.width >= 2, rect.height >= 2
- 创建 Region 时必须位于图像范围内
- 编辑 Region 后裁剪到图像范围
- 人工译文优先于候选/模型
- selectedCandidate 必须存在
- 字体大小 > 0
- lineSpacing > 0
- opacity ∈ [0,1]
- 原始资产不可被编辑流程覆盖
