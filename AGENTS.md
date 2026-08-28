# MangaTranslator Agent Guide

## Scope

This is the repository-wide contract. Read the nearest nested `AGENTS.md` before editing files below a subdirectory; the nearest file adds to or overrides this one.

## Project goal

MangaTranslator is a desktop manga-translation workbench for importing CBZ, PDF, and image collections; detecting text; running OCR; translating with an LLM; inpainting; typesetting; reviewing; and exporting edited pages.

Target stack:

- C++23.
- Qt Widgets 6.11.
- FluentQt, using only its public C++ API and linking `FluentQt::FluentQt`.
- CMake with target-based configuration.
- ONNX-backed comic text detection and other replaceable processing engines.

The UI architecture follows the current ForgeAI style: per-screen State, `BaseViewModel`, `updateState`, `stateChanged`, `observe`, UseCases, and an `ApplicationContext` composition root. It is MVVM with unidirectional data flow, not Redux.

## Architecture and dependency direction

Expected top-level source areas are `app`, `core`, `domain`, `application`, `services`, `data`, `network`, and `ui`.

Dependencies point inward:

```text
ui -> application -> domain
services/data/network -> application ports + domain
app -> all layers (construction only)
core -> no feature layer
domain -> standard library
```

- `domain` contains business concepts and invariants.
- `application/ports` defines external capabilities; `application/usecase` coordinates them.
- `services`, `data`, and `network` implement ports.
- `ui` renders state and reports user intent.
- `app/ApplicationContext` is the only composition root.
- Never introduce a dependency from an inner layer to `ui`, an adapter, or `app`.

## Working rules

- Inspect nearby code, CMake targets, tests, and nested instructions before editing.
- Preserve unrelated user changes. Do not perform destructive Git operations.
- Make the smallest coherent change and include tests appropriate to its risk.
- Do not add a framework, global singleton, or cross-layer abstraction for one call site.
- Keep credentials, copyrighted model weights, private endpoints, and user manga content out of Git and logs.

## Coding conventions

Follow `CODING.md` and the checked-in `.clang-format`. In brief: use UTF-8/LF, four-space indentation, English
identifiers and code comments, self-contained headers, explicit ownership, and the established C++/Qt naming rules.
Format only files touched by the change; do not create unrelated formatting churn.

Comments state non-obvious constraints and invariants only. Do not write narrative asides that reference the current
task, fix, or history; persistence and similar rules belong in the owning code or docs, not in story-telling comments.
Write comments as plain declarative sentences: no hyphens as sentence separators, no em dashes, and no decorative
punctuation.

For UI work, also follow `src/ui/AGENTS.md` and start from `docs/ui/README.md`. Read `docs/ui/foundations.md`,
`docs/ui/control-reference.md`, and the relevant page/component specifications before implementation. UI draft images
are illustrative references; the textual UI specifications are authoritative when they differ.

## Commit policy

- Use Conventional Commits: `<type>(<scope>): <summary>`.
- Allowed types are `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`, and `revert`.
- Prefer a layer or focused feature as scope: `app`, `core`, `domain`, `application`, `services`, `data`, `network`,
  `ui`, `manga`, `cmake`, `docs`, or `tests`.
- Write the summary in English imperative mood, lowercase, without a trailing period, and within 72 characters.
- Mark breaking changes with `!` and a `BREAKING CHANGE:` footer.
- Keep each commit coherent; separate unrelated cleanup and pure formatting from behavior changes.
- Agents do not create, amend, rebase, squash, or push commits unless the user explicitly requests that Git action.
- Before an authorized commit, format changed files and run the affected build and tests; report anything not run.

Examples: `feat(manga): add text region resize handles`, `fix(application): ignore stale OCR completions`.

## Build and test

Prefer checked-in `CMakePresets.json` names when present; do not invent preset names. Until presets exist, the conventional local flow is:

```powershell
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure
```

Keep Qt and FluentQt discovery configurable through CMake cache variables or toolchain files. Never hard-code a developer machine path.

CMake changes must follow these rules:

- Require C++23 with `CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON`, and extensions disabled unless a documented dependency requires them.
- Discover Qt with a Qt 6.11-compatible `find_package(Qt6 ... REQUIRED COMPONENTS ...)` call and link imported targets such as `Qt6::Widgets`; never use raw library paths.
- Link FluentQt through `FluentQt::FluentQt` and public headers only.
- Use target-scoped sources, include directories, definitions, features, warnings, and dependencies. Avoid directory-global build state.
- Enable `AUTOMOC`, `AUTORCC`, and `AUTOUIC` only where needed. List owned sources explicitly rather than using recursive globbing.
- Keep one target per meaningful layer/capability so tests can link narrowly. Production libraries must not depend on test or UI-only targets.
- Pin third-party dependency revisions and keep dependency downloads out of ordinary offline rebuilds.
- Register tests with CTest and keep build artifacts under `build/`, never beside sources.

## Definition of done

- The affected supported configuration builds with C++23 and Qt 6.11.
- Relevant tests pass; new behavior has success and failure coverage.
- Dependency direction and thread affinity remain valid.
- Long-running work is cancellable and never blocks the GUI thread.
- UI changes are checked in Light and Dark themes and at normal and narrow widths.
- No build output, cache, temporary generated file, model weight, secret, or absolute local path is committed. Explicitly maintained documentation artifacts, test baselines, and formal resources are allowed when they are reviewed and intentional.
- The final report lists commands actually run and any unverified boundary.

## Nested guides

- `src/AGENTS.md`: C++ and source-layout rules.
- `src/app/AGENTS.md`: application startup and composition root.
- `src/core/AGENTS.md`: foundational utilities.
- `src/domain/AGENTS.md`: domain model and repository contracts.
- `src/application/AGENTS.md`: ports and UseCases.
- `src/services/AGENTS.md`: OCR, detection, inpaint, layout, and processing engines.
- `src/data/AGENTS.md`: persistence and file import/export.
- `src/network/AGENTS.md`: LLM and remote-provider adapters.
- `src/ui/AGENTS.md`: MVVM/UDF, FluentQt integration, and visual quality.
- `src/ui/manga/AGENTS.md`: graphics scene and editor canvas.
- `docs/ui/README.md`: UI design index, required reading order, and textual design baseline.
- `res/AGENTS.md`: resources and model assets.
- `tests/AGENTS.md`: test organization and acceptance rules.
