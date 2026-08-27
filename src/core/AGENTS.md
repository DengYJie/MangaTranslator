# Core Foundation

## Responsibility

`core` contains stable, low-level facilities such as logging, settings primitives, clocks, IDs, filesystem helpers, and small platform abstractions. It is not a dumping ground for feature code.

## Rules

- `core` must not depend on `domain`, `application`, adapters, or `ui`.
- Utilities must have a focused contract and tests; prefer a local function in the owning layer over a premature shared helper.
- Settings keys and defaults are centralized, typed, versioned, and validated at the boundary.
- Logging exposes severity and structured context without coupling callers to one backend.
- Filesystem helpers accept explicit paths and never silently use the current working directory for user data.
- Time, randomness, and ID generation used by business logic must be injectable or controllable in tests.
- Platform-specific code stays behind a narrow interface and is isolated by platform in CMake.

Do not place repository implementations, HTTP clients, model sessions, ViewModels, or domain policies in `core`.
