# Test Rules

## Organization

Mirror production boundaries so failures identify the owning layer:

- `unit/domain`: entities, value objects, and invariants.
- `unit/application`: UseCases with fake ports.
- `unit/ui`: ViewModel State transitions and canvas math without a visible window where possible.
- `contract`: repository, provider, importer, and model-adapter contracts.
- `integration`: filesystem, Qt event delivery, inference fixtures, and pipeline boundaries.
- `visual`: deterministic UI states and screenshot/geometry review support.

Use the test framework and target naming established by CMake once present. Each test target links only the production target it exercises.

## Required coverage by change

- Domain change: valid and invalid boundaries plus equality/invariants.
- UseCase change: success, validation, dependency failure, cancellation, and partial/retry behavior when applicable.
- ViewModel change: initial `observe` replay, no-op update suppression, intended State sequence, stale async completion, and one-shot effects.
- Async change: completion, cancellation, receiver destruction, shutdown, and repeated/overlapping requests.
- Import/persistence change: corrupt/untrusted input, Unicode paths, atomic failure, versioning, and cleanup.
- Model change: preprocessing/postprocessing, tensor contract, coordinate restoration, missing/corrupt model, and backend error mapping.
- Canvas change: coordinate round trips, zoom-independent hit behavior, undo/redo, z-order, and selection identity.

## Determinism and isolation

- Tests do not use real credentials, paid APIs, public network services, user model caches, or user documents.
- Use fakes, temporary directories, generated/licensed fixtures, controllable clocks, and deterministic IDs.
- Keep unit tests independent of execution order and machine-specific absolute paths.
- Compare floating-point/model geometry with documented tolerances, not arbitrary exact pixels.
- GUI tests must pump the Qt event loop deliberately and use bounded waits; no unconditional sleeps.

## Visual validation

For visible UI changes, inspect the real built application with deterministic data in Light and Dark themes, normal and narrow widths, and relevant interaction/error states. Compare major controls with named FluentQt Gallery samples. Record concrete issues and fixes; do not approve from a single default screenshot.

## Commands

Prefer repository presets when available. Otherwise:

```powershell
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure
```

Run focused tests during iteration and the affected suite before completion. Report skipped or unavailable tests explicitly; never describe an unrun test as passing.
