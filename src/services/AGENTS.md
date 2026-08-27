# Processing Services

## Responsibility

`services` implements local application ports for comic text detection, OCR, inpainting, typesetting, and image processing. It provides processing adapters and stage primitives; workflow/pipeline orchestration belongs to `application/usecase`. Third-party types remain private to the adapter that owns them.

## Model and pipeline rules

- Reuse expensive model/session objects; do not reload weights per page or region.
- Record model identity, version, input constraints, preprocessing, postprocessing, device/provider, and confidence thresholds.
- For comic-text-detector/ONNX, validate tensor names, shapes, color order, normalization, scaling, padding, and coordinate restoration with tests.
- Convert detected geometry back to canonical source-image pixel coordinates before returning it.
- Keep OCR, translation, inpainting, and layout outputs independently reviewable. Preserve provenance and confidence where available.
- Stage results must expose enough state for an application UseCase to retry a failed stage without recomputing successful expensive stages unnecessarily.
- Keep cross-stage ordering, retry policy, progress aggregation, and application commit boundaries in `application/usecase`, not in service adapters.
- Never mutate the persisted project incrementally from worker code without an application-level commit boundary.

## Threads and cancellation

- CPU/GPU inference, image processing, and file decoding never run on the GUI thread.
- Prefer a bounded task executor or thread pool. Do not create one unmanaged thread per page.
- Make cancellation cooperative using a stop token or equivalent checked between meaningful units of work.
- Progress is monotonic within a stage and rate-limited before reaching the UI.
- Do not touch GUI objects, `QPixmap`, scenes, or ViewModels from workers. Use `QImage` or library-native buffers and deliver immutable results back to the owning thread.
- Serialize access when an inference backend or model session is not documented as thread-safe.
- Teardown must cancel queued work and prevent callbacks into destroyed receivers.

## Errors and performance

- Translate backend exceptions/status codes into stable application error categories while retaining safe diagnostics.
- Distinguish unavailable model, invalid model, unsupported hardware/provider, out-of-memory, cancelled, and invalid input.
- Add benchmarks or representative timing checks for hot paths; avoid implicit full-page copies inside tight loops.
- Results must be deterministic within documented backend tolerances for fixed fixtures and settings.
