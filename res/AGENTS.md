# Resources and Models

## Resource layout

- `icons`: vector or raster icons with clear licensing and theme behavior.
- `fonts`: redistributable fonts and license notices.
- `models`: manifests/configuration only unless redistribution of a weight file is explicitly licensed and approved.

## Qt resources

- Put small immutable UI assets in `.qrc` and reference them through resource URLs.
- Do not embed large ONNX weights, language packs, test corpora, PDFs, or sample manga in the executable.
- Keep resource aliases stable and organized by feature; detect missing critical resources during startup or feature initialization.
- Provide Light/Dark variants only when semantic tinting cannot produce the correct result.

## Model assets

- Large models live in a configurable user cache/data directory outside the repository and executable resources.
- Maintain a manifest containing model ID, version, source, license, expected size, SHA-256, input contract, supported execution providers, and required preprocessing version.
- Verify checksum and compatibility before loading. Download to a temporary file and atomically publish only after verification.
- Never auto-download a model without clear user intent, progress, cancellation, storage location, and failure recovery.
- Do not commit model weights or assets with unclear redistribution rights.

## Security and privacy

- No API keys, tokens, user paths, private endpoints, or real user manga.
- Treat SVG, archive, PDF, font, ICC, and model inputs as untrusted.
- Test fixtures must be generated, public-domain, or explicitly licensed and small enough for the repository.
