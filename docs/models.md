# ONNX 模型集成与清单制作指南 (Model Integration Guide)

MangaTranslator 采用轻量且解耦的 **ONNX Runtime + Manifest 校验** 架构，支持即插即用的本地模型加载。

---

## 1. OCR 模型清单格式 (`manifest.json`)

每个模型目录下必须提供 `manifest.json`，按类别分别由 `src/services/ocr/BaberuManifest.cpp`（文字识别）、`src/services/detection/KoharuLayoutManifest.cpp`（版面检测）、`src/services/inpaint/InpaintManifest.cpp`（文字消除）解析。三者公共字段为 `manifestVersion`、`id`、`version`、`source`、`runtime`、`graphs`、`input`，文字识别另需 `tokenizer`、`normalization`、`generation`、`postprocess`。

注意 `sha256` 字段：校验器只确认它是 64 位小写十六进制字符串，不会读取模型文件求哈希再比对。清单因此能挡住字段缺失或写错格式，挡不住权重被替换或损坏；要核对完整性，自己用 `Get-FileHash -Algorithm SHA256` 算一遍再与清单比对。

完整的文字识别清单形状如下（`graphs[].role` 固定为 `vision`、`decoder-prefill`、`decoder-step`）：

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
