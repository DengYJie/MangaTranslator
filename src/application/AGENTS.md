# Application Layer

## Layout

- `error`: `ApplicationError` and the `Result` type alias.
- `operation`: `OperationContext` and progress types.
- `ports`: capability interfaces required by the application.
- `dto`: request/response types that cross ports.
- `mapping`: DTO ↔ domain mapping.
- `usecase`: one task-oriented application operation per focused class or module.
- `workflow`: multi-stage orchestration that coordinates UseCases and ports.

Expected ports may include `ITextDetector`, `IOcrEngine`, `ITranslator`, `IInpainter`, `ITextLayouter`, and `IComicImporter`. Interfaces describe application needs, not third-party SDK shapes.

## UseCase rules

- A UseCase validates task input, loads required aggregates, coordinates ports, applies domain policy, and returns a typed result.
- Keep UseCases independent of widgets, scenes, dialogs, ViewModels, FluentQt, concrete repositories, HTTP, ONNX, and file formats.
- Do not combine detection, OCR, translation, inpainting, and export into one untestable god object. A pipeline UseCase may coordinate smaller focused UseCases or ports.
- Represent progress, cancellation, and partial results explicitly when an operation is long-running.
- A cancelled operation is distinct from failure. Do not publish stale results after cancellation or after the caller starts a newer request.
- Define idempotency and retry safety for operations that write projects or call paid providers.

## Ports

- Ownership and thread-safety expectations are part of every port contract.
- Use domain/application request and result types. Map provider, database, and model types inside adapters.
- Asynchronous port operations return `QFuture<std::expected<T, ApplicationError>>` — the standardized QtCore-only async contract. Interfaces do not inherit `QObject` and define no signals; cancellation uses `std::stop_token` carried in `OperationContext`.
- Never leak API keys, network responses, ONNX tensors, archive handles, or `QGraphicsItem`s through a port.

## Tests

Test UseCases with fakes. Cover success, validation, empty input, port failure, partial output, cancellation, retry boundaries, and write conflicts without starting the GUI.
