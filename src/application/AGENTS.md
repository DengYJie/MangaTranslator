# Application Layer

## Layout

- `ports`: capability interfaces required by the application.
- `usecase`: one task-oriented application operation per focused class or module.

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
- Avoid Qt signals in interfaces unless the application layer intentionally standardizes a QtCore-only asynchronous contract. Prefer testable callbacks, task/result objects, or standard cancellation abstractions.
- Never leak API keys, network responses, ONNX tensors, archive handles, or `QGraphicsItem`s through a port.

## Tests

Test UseCases with fakes. Cover success, validation, empty input, port failure, partial output, cancellation, retry boundaries, and write conflicts without starting the GUI.
