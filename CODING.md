# MangaTranslator Coding Guide

This document is the human-facing coding standard for MangaTranslator. Repository and directory-specific
`AGENTS.md` files remain authoritative for architecture boundaries and automated-agent behavior.

## Language and files

- Use C++23 and Qt Widgets 6.11.
- Save source, CMake, JSON, and Markdown files as UTF-8 with LF line endings and a final newline.
- Use English for identifiers and code comments. User-visible text must be translatable with `tr()` or the owning
  translation mechanism. Design documentation may be written in Chinese.
- Keep declarations in `.h` and non-trivial implementations in `.cpp`. A file should have one clear responsibility.
- Do not introduce catch-all `Manager`, `Helper`, `Common`, `Misc`, or `Utils` modules.

## Formatting

`.clang-format` is the formatting source of truth. It uses four-space indentation, no tabs, attached braces, and a
120-column limit.

- Format only files touched by the change; do not create unrelated formatting diffs.
- Do not hand-align declarations, assignments, or comments.
- Preserve deliberate line breaks only when they materially improve comprehension.
- Run `clang-format -i <changed-file>` before submitting C++ changes.
- When available in CI, `clang-format --dry-run --Werror <changed-file>` is the conformance check.

## Naming

| Element | Convention | Example |
| --- | --- | --- |
| Types and concepts | `PascalCase` | `EditorViewModel` |
| Functions and variables | `camelCase` | `translatePage` |
| Private data members | `m_` + `camelCase` | `m_currentPage` |
| Compile-time constants | `kPascalCase` | `kHandleSize` |
| Namespaces | lowercase | `manga::application` |
| Files | match the primary type | `EditorViewModel.h` |
| Qt signals | fact or state change | `stateChanged` |
| Commands and intent methods | imperative verb | `openProject` |

- Use `enum class` and descriptive enumerator names.
- Avoid abbreviations unless they are established domain terms such as OCR, LLM, PDF, or ONNX.
- Name booleans as predicates, such as `isBusy`, `hasSelection`, or `canExport`.

## Includes and namespaces

- Headers must be self-contained and include what they use.
- In a `.cpp`, include its matching header first, followed by project, Qt/FluentQt, third-party, and standard headers.
- Do not rely on transitive includes or use `using namespace` in headers.
- Put file-private helpers, constants, and control-specific Metrics in the `.cpp` anonymous namespace.
- Do not expose third-party types across application Port boundaries.

## C++ and ownership

- Prefer RAII, value types, `std::unique_ptr`, scoped enums, ranges, and `std::expected` where they clarify the design.
- Raw pointers express non-owning observation only. Make Qt parent ownership explicit at construction.
- Avoid manual `new`/`delete`; every `QObject` must have one clear owner.
- Use `const`, `constexpr`, `[[nodiscard]]`, `override`, `final`, and `noexcept` when their contracts are true.
- Use `auto` when the type is obvious from the initializer or spelling the type would obscure the intent.
- Avoid mutable global state, hidden blocking I/O, and macros that can be replaced by language features.

## Qt and FluentQt

- Use typed signal/slot connections and context-bound lambdas.
- Declare signal sections with `Q_SIGNALS`, declare meta-object slots with `Q_SLOTS`, and emit with `Q_EMIT`.
  Do not use Qt's keyword-style aliases `signals`, `slots`, or `emit`.
- A receiver used only by typed `connect()` is an ordinary member function. Use `Q_SLOTS` or `Q_SLOT` only when
  string-based meta-object invocation, Designer auto-connection, or another documented Qt runtime contract needs it.
- Define `QT_NO_KEYWORDS` for new Qt targets. Enable it on an existing target only after migrating that target's
  production and test sources to the `Q_*` macros.
- Access a `QObject` only from its owning thread. Widgets, ViewModels, scenes, and `QPixmap` stay on the GUI thread.
- Guard delayed callbacks with receiver context, `QPointer`, generation IDs, or another explicit lifetime mechanism.
- Use FluentQt public controls and semantic theme/color APIs. Do not add a custom `QStyle`, `QProxyStyle`, parallel
  theme system, or application-wide stylesheet.
- Raw Qt and custom-painted controls consume FluentQt semantic tokens directly.

## Comments and documentation

Comments explain why the code exists, which invariant it preserves, or which non-obvious constraint applies. They do
not translate the next statement into prose.

Comment the following when the type system and naming cannot express them clearly:

- ownership, lifetime, and thread affinity;
- cancellation, request generations, and stale-result handling;
- coordinate spaces, units, scale direction, and rounding rules;
- model tensor layout, channel order, normalization, and coordinate restoration;
- unusual performance choices, compatibility workarounds, or security boundaries;
- public preconditions, postconditions, and stable error behavior.

Use `//` for implementation notes and `///` for public API contracts. Do not mechanically document every parameter or
repeat a function name. Write complete English sentences and keep comments synchronized with the implementation.

Actionable debt must reference a tracked item and explain the exit condition:

```cpp
// TODO(#128): Remove this fallback after the DirectML provider supports dynamic input shapes.
```

Do not commit commented-out code, author/change-history comments, decorative banners, unexplained `TODO`/`FIXME`, or
comments that compensate for poor naming.

## Errors and logging

- Use structured errors at layer boundaries; do not encode failure as an empty string, null object, or magic integer.
- Assertions represent programmer invariants, not invalid user input or recoverable provider failures.
- Catch and translate third-party exceptions at adapter or task boundaries.
- Log stable identifiers, timings, model/provider names, and dimensions when useful. Do not log secrets or full user
  content by default.

## Commit messages

Use Conventional Commits:

```text
<type>(<scope>): <summary>
```

Allowed types are `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`, and `revert`. Prefer an
architecture layer or focused feature for the scope, such as `app`, `core`, `domain`, `application`, `services`,
`data`, `network`, `ui`, `manga`, `cmake`, `docs`, or `tests`.

- Write the summary in English, imperative mood, lowercase, without a trailing period; keep it within 72 characters.
- Use `!` and a `BREAKING CHANGE:` footer for an incompatible contract or persisted-format change.
- Keep each commit coherent and free of unrelated cleanup. Separate pure formatting from behavior changes.
- Use the body to explain motivation, important tradeoffs, migration, and risk; do not narrate the diff.
- Do not commit build output, caches, secrets, model weights, user content, or machine-specific paths.
- Before committing, format changed files and run the affected build and tests. Record checks that could not be run.

Examples:

```text
feat(manga): add text region resize handles
fix(application): ignore stale OCR completions
refactor(services): isolate ONNX preprocessing
docs(ui): define settings navigation behavior
```
