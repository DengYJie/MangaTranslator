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

### Two markers

`///` states a contract. `//` states an implementation note. What decides is what the comment claims, not whether the
declaration is public. A file-local helper that states a threading or lifetime invariant is stating a contract.

```cpp
/// Rejects an empty code or one that begins or ends with a hyphen.
[[nodiscard]] bool isValid() const;

// Masks arrive in the detector's coarse grid. Scaling once here keeps every consumer from repeating
// the arithmetic against a constant that may change.
QImage upscaleMask(const QImage& mask, QSize target);
```

Do not introduce a second dialect. Project code has no `//!` file banners, no `@brief`/`@param`/`@return` blocks, and no
`/// <` field markers. The ported FluentQt sources under `src/ui/window`, `src/ui/navigation`, and `src/ui/animation`
keep their upstream Doxygen style; nothing new joins that exemption.

Say each thing once. The header owns the contract, and the definition adds only a note about how it is achieved. A
trailing `//` earns its place with a unit, an ownership qualifier, or a short invariant on a field or enumerator, and a
bare noun phrase is acceptable as a region label such as `// Page lifecycle`.

### Sentences

```cpp
/// Validates internal consistency of the region.         // restates the name
// prevent leading hyphen                                 // fragment without a subject
/// Writes a setting by key.                              // restates the signature
QSemaphore concurrencyGate{1};  // concurrency gate = 1   // restates the code
```

```cpp
/// Rejects a code longer than kMaxLength or one carrying a hyphen at either end.
/// Keeps the aggregate alive until the repository close completes, because the task may still read
/// session-owned paths during close.
```

Write in the third-person present, one clause per `///` line, ending with a period, wrapped near 100 columns. Say `for
example` rather than `e.g.`. Avoid ALL-CAPS emphasis and a first-person `we`. Restating a numeric limit that already
appears in code guarantees the two disagree later, so name the constant or describe the constraint instead.

### English and ASCII

Comments are English and ASCII. Multiplication is `x`, comparison is `>=`, and an arrow is usually the word `to`.

```cpp
// Placeholder compilation unit — required for the STATIC library target.
/// Returns clamped geometry, or std::nullopt if clamped size < 2×2.
// Row 1: 字体 (Font Family) + 颜色 (Text Color)
```

```cpp
// Placeholder translation unit so the static library target has something to compile.
/// Returns the geometry clamped to the page, or nothing when a clamped side falls below kMinDimension.
```

Non-English is data rather than prose when the comment quotes the value itself. `Returns the phrasing "分钟" for a minute
count` documents behavior, while a Chinese label above a row of widgets narrates the code. User-visible text belongs in
`tr()` either way.

### References

A reference is worth writing when you could follow it. Naming the system a value came from usually qualifies:

```cpp
// ImageNet channel means and standard deviations. The detector was trained with these, so normalizing
// differently shifts every confidence threshold.
// Bicubic kernel with keys = -0.5, matching PIL Image.BICUBIC, keeps the golden tolerance at 1e-6.
// Layout follows docs/ui/pages/home.md.
```

Pointing at foreign code by file or line number does not survive, and neither do internal artifacts whose source has
been deleted: a phase label, a plan document that no longer exists, a numbered section of an unnamed specification.
Each leaves a reader checking something that is not there. Naming the task or review that produced a change belongs in
the commit message, and a version stamp outlives its usefulness unless code enforces the bound.

### Interfaces

A port method is often the only place a caller can learn what it may assume, so its `///` block states four things: the
thread the work runs on and the thread that receives the completion; who owns what it accepts and returns and what must
outlive the call; how cancellation is observed and what arrives when it fires; and which error codes the caller has to
handle. `src/application/ports/ICredentialStore.h` is the model to imitate, including the way it states its own limits.

### Tests

A test name carries intent, so a comment in a test earns its place with a precondition or the origin of an expected
value.

```cpp
// Verify the page was added
QCOMPARE(model.rowCount(), 1);
```

```cpp
// Each 1024x1024 RGBA entry costs 4 MiB, so a 64 MiB budget holds 16 and evicts the oldest first.
QCOMPARE(cache.maxCost(), 64 * 1024 * 1024);
```

Numbered step labels read well across a long scenario test and clutter a single assertion. A `Regression:` prefix
introduces an invariant that must not break; it is not a substitute for a tracked item.

Do not document a declaration twice. Actionable debt must reference a tracked item and explain the exit condition:

```cpp
// TODO(#128): Remove this fallback after the DirectML provider supports dynamic input shapes.
```

Do not commit commented-out code, author/change-history comments, decorative banners, unexplained `TODO`/`FIXME`, or
comments that compensate for poor naming. Permanent observability such as log fields, timings, metrics, and stable
error codes is expected. Investigation scaffolding gated by an environment variable is not committed.

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
