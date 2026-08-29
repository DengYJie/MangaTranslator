# Source Code Rules

## Scope and layout

Keep code in the layer that owns the responsibility:

- `app`: startup, shutdown, and dependency wiring.
- `core`: logging, settings, small platform-neutral utilities.
- `domain`: entities, value objects, invariants, and repository abstractions.
- `application`: ports and task-oriented UseCases.
- `rendering`: deterministic text measurement and rasterization shared by UI, processing adapters, and export; Qt Core/Gui only, with no I/O or Widgets.
- `services`: local processing implementations.
- `data`: persistence, serialization, import, and export adapters.
- `network`: remote clients and transport mapping.
- `ui`: Qt Widgets views, ViewModels, navigation, styling, and canvas presentation.

Do not create catch-all `manager`, `common`, `helper`, or `misc` modules. Name a module after the capability it owns.

## C++23 conventions

- Prefer RAII, value types, `std::unique_ptr`, scoped enums, ranges, and `std::expected` where they clarify ownership or failure.
- Use raw pointers only for non-owning observations, especially Qt parent-owned objects; make that ownership evident.
- Avoid manual `new`/`delete`. Give every `QObject` one explicit owner.
- Use `nullptr`, `override`, `final` where appropriate, and `[[nodiscard]]` for meaningful results.
- Keep headers self-contained. Include what is used; do not depend on transitive includes.
- Put public declarations in `.h` and non-trivial implementation in `.cpp`. Avoid large inline implementations.
- Prefer project namespaces over global symbols. Follow established namespace spelling once code exists.
- Types use `PascalCase`; functions and variables use `camelCase`; private data members use `m_` prefix; constants use the established local convention.
- Qt signals describe facts in past/present state (`stateChanged`, `progressChanged`); slots and intent methods use verbs (`openProject`, `cancelTranslation`).

## Qt boundaries

- Use Qt types freely in `ui`; keep `domain` independent of Qt UI and infrastructure.
- Never access a `QObject` from a thread other than the one owning it.
- Prefer typed signal/slot connections and context-bound lambdas.
- Do not capture a raw `this` in delayed work unless lifetime is guaranteed; use QObject context connections, `QPointer`, or another explicit lifetime guard.
- Assertions express programmer invariants, not recoverable input or provider failures.

## Errors and diagnostics

- Return structured errors at layer boundaries. Do not use empty strings, null objects, or magic integers as failure channels.
- Include operation, stable error code/category, safe user message, and diagnostic cause where useful.
- Catch third-party and standard exceptions at adapter/task boundaries before they cross Qt event delivery.
- Log identifiers, timings, provider/model names, and dimensions when helpful; do not log credentials or full user content by default.

## Prohibited

- Business rules in widgets, delegates, `main.cpp`, or signal wiring.
- Layer bypasses for convenience.
- Mutable process-wide feature state.
- Hidden blocking I/O in getters, constructors, paint events, or UI callbacks.
- Absolute paths, machine-specific compiler flags, or generated binaries in source directories.
