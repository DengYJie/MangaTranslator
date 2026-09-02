# ONNX 模型集成与清单制作指南 (Model Integration Guide)

MangaTranslator 采用轻量且解耦的 **ONNX Runtime + Manifest 校验** 架构，支持即插即用的本地模型加载。

---

## 1. 模型目录结构

所有模型统一保存在项目根目录的 `models/` 下：

```text
models/
├── detection/
│   └── comic-text-detector/
│       └── 1.0.0/
│           ├── model.onnx
│           └── manifest.json
└── ocr/
    ├── genshiai-baberu-ocr-onnx/
    │   └── 1.0.0/
    │       ├── onnx/
    │       ├── tokenizer.json
    │       └── manifest.json
    └── paddleocr-vl-1.6-onnx/
        └── 1.0.0/
            ├── onnx/
            ├── tokenizer.json
            └── manifest.json
```

---

## 2. OCR 模型清单格式 (`manifest.json`)

每个模型目录下必须提供 `manifest.json`，由 [`BaberuManifest`](file:///F:/source/MangaTranslator/src/services/ocr/BaberuManifest.h) 进行严格的模式与 SHA-256 校验：

```json
{
  "manifestVersion": 1,
  "id": "model-identifier",
  "version": "1.0.0",
  "source": {
    "repository": "huggingface-user/repo-name",
    "revision": "main",
    "license": "Apache-2.0"
  },
  "runtime": {
    "minimumOnnxRuntime": "1.20.0",
    "opset": 17,
    "executionProvider": "cpu"
  },
  "graphs": [
    {
      "role": "vision",
      "file": "onnx/vision_encoder.onnx",
      "sha256": "<64-char-hex-sha256>",
      "inputs": ["pixel_values"],
      "outputs": ["image_embeds"]
    },
    {
      "role": "decoder-prefill",
      "file": "onnx/embedding.onnx",
      "sha256": "<64-char-hex-sha256>",
      "inputs": ["input_ids"],
      "outputs": ["embeddings"]
    },
    {
      "role": "decoder-step",
      "file": "onnx/decoder.onnx",
      "sha256": "<64-char-hex-sha256>",
      "inputs": ["inputs_embeds", "attention_mask"],
      "outputs": ["logits"]
    }
  ],
  "tokenizer": {
    "type": "bpe",
    "vocabFile": "tokenizer.json",
    "sha256": "<64-char-hex-sha256>",
    "bosTokenId": 1,
    "eosTokenId": 2,
    "padTokenId": 0,
    "unkTokenId": 3
  },
  "input": {
    "width": 224,
    "height": 224,
    "layout": "NCHW",
    "color": "RGB",
    "sourceConversion": "RGB",
    "resizeMode": "resize-only",
    "resample": "bicubic"
  },
  "normalization": {
    "scale": 0.00392156862745098,
    "mean": [0.5, 0.5, 0.5],
    "std": [0.5, 0.5, 0.5]
  },
  "generation": {
    "maxLength": 512,
    "strategy": "greedy",
    "repetitionPenalty": 1.2,
    "contentRunCap": 12
  },
  "postprocess": "paddleocr-v1"
}
```

---

## 3. 计算 SHA-256 校验和

在 Windows PowerShell 中可直接使用 `Get-FileHash` 计算：
```powershell
(Get-FileHash -Algorithm SHA256 "onnx/vision_encoder.onnx").Hash.ToLower()
```
