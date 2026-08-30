# UI, MVVM/UDF, and FluentQt

## Required design references

Before changing UI code, read the design documents in this order:

1. `docs/ui/README.md` for document scope and the current index.
2. `docs/ui/foundations.md` for global visual, responsive, motion, input, and accessibility rules.
3. `docs/ui/control-reference.md` for WinUI 3 reference semantics and Qt/FluentQt mapping constraints.
4. `docs/ui/components/application-shell.md` when changing windows, title bar, navigation, or Surface switching.
5. The relevant `docs/ui/pages/*.md` page specification.
6. Every `docs/ui/components/*.md` specification referenced by that page or changed component.

For the V0.1 editor, also read `docs/plan/v0.1/phase-5-ui-editor-supplement.md` for shared rendering,
generation, cache, Undo/Dirty, thumbnail, and credential boundaries.

Authority is divided by concern: this `AGENTS.md` governs architecture, state flow, threading, and implementation
constraints; `foundations.md` governs shared visual and accessibility requirements; page documents govern page layout
and responsive behavior; component documents govern reusable component contracts. A more specific page/component
document may refine, but must not violate, the architecture or global accessibility rules.

Files under `docs/ui/drafts/` communicate information architecture and density only. They are not pixel-accurate
acceptance baselines, and textual specifications take precedence when they differ. If textual documents conflict,
reconcile the affected documents instead of silently choosing one during implementation.

## Structure

Organize UI by responsibility:

- `base`: `BasePage`, `BaseViewModel`, and narrowly reusable UI foundations.
- `screen`: one folder per screen with its View and ViewModel.
- `manga`: graphics scene, canvas items, tools, and editor presentation.
- `navigation`: navigation models and routing.
- `widget`: reusable composed widgets, not business features.
- `window`: top-level window chrome and hosting.

Views never call repositories, network clients, or inference services directly.

## MVVM with unidirectional data flow

- Each screen ViewModel owns one authoritative value-type State snapshot.
- The View observes State and emits user intent. It never mutates State or domain objects directly.
- All persistent UI changes pass through ViewModel intent methods and `updateState`.
- `updateState` creates/updates the next snapshot, compares it with the current State, and emits `stateChanged` only when meaningfully different.
- `observe` immediately supplies the current State, then subscribes to future changes. A newly attached View must not wait for the next mutation.
- State types provide deterministic equality and contain renderable data, selections, validation, busy/progress flags, and recoverable status.
- Keep transient one-shot effects such as opening a file picker, focusing a field, or showing a toast as explicit events. Do not encode durable screen truth only as a signal.
- Public ViewModel intent methods are the UDF input boundary. Async completions re-enter the ViewModel and call `updateState`.
- Use request IDs/generations so an older completion cannot overwrite newer State.
- This project does not use a Redux-style global Store, Action hierarchy, or Reducer. Do not introduce one.

## FluentQt component rules

Treat MangaTranslator as a `greenfield` file-oriented workbench:

- `NavigationView` for fixed top-level product areas.
- `TabView` only for user-managed peer documents/work surfaces.
- Use a project-local resizable layout for the page pane, canvas, and Inspector. Do not assume a FluentQt `SplitView` API exists; verify any candidate public header before selecting it.
- FluentQt command, collection, progress, dialog, flyout, toast, and info components according to their semantics.

Only register destinations implemented in the current version. Do not expose placeholder navigation for future batch or glossary surfaces.

Before using a component, verify its public C++ header/API and its Gallery sample. Link `FluentQt::FluentQt`; do not include source-private headers.

Prefer FluentQt controls. A raw Qt widget is allowed only when FluentQt lacks the required behavior or native integration is essential. Document the gap, encapsulate the widget, and make it fully theme-aware.

### Registered porting exceptions

- The ported navigation pane includes two FluentQt headers that are private by convention but install-exported as inline headers: `components/foundation/overlay/OverlayShadow.h` and `components/foundation/overlay/OverlayGeometry.h`, used for `paintLayeredShadow` and `syncInheritedThemeOverride` in `navigation/NavigationFlyout`. This is a documented exception limited to those two headers; do not extend it to other source-private headers.
- `navigation/NavigationWidget.cpp`'s `InputModalityFilter` installs a global application event filter for keyboard/mouse modality detection. It is a deliberate keep-over from the FluentQt navigation port; do not remove it without a replacement modality source.

## Theme, tokens, and control metrics

- Initialize FluentQt resources and apply the selected FluentQt theme before constructing the main window.
- Use FluentQt's public semantic theme and color tokens for surfaces, text, strokes, controls, accent, focus, critical, caution, information, and success states. Do not create a parallel application token or theme system.
- Do not add `ui/style`, `res/themes`, a custom `QStyle`/`QProxyStyle`, `QApplication::setStyle()`, or an application-wide stylesheet.
- For a raw Qt or custom-painted control, consume FluentQt semantic tokens directly. A small control-local `QPalette`, palette role mapping, or paint helper is acceptable when the required integration is not exposed by FluentQt.
- Keep control-specific geometry and visual metrics as `constexpr` values in the implementation file's anonymous namespace. Do not create a shared metrics catalog for values that belong to one control.
- Avoid hard-coded color literals. Keep any unavoidable QSS narrowly scoped to one encapsulated control and limited to behavior or geometry that public tokens and painting APIs cannot express.
- Theme changes must repaint the open window, raw Qt viewports, selections, placeholders, popups, and overlays without restart.

## Visual and interaction quality

- Use the FluentQt Gallery as the finish benchmark, not as a required information architecture.
- Follow a 4 px rhythm. Default to 12/16 px panel insets, 8 px related-control gaps, 12–16 px section gaps, 28–32 px compact controls, and 32–36 px normal controls unless a verified component metric differs.
- Use one accent primary action per local decision region. Prefer spacing and typography before cards or borders.
- Define normal, narrow, and minimum supported layouts. Never rely on one demo string or fixed window size.
- Icon-only actions require accessible names and tooltips. Verify keyboard order, focus, Escape/cancel, default actions, and IME composition.
- Review Light/Dark, normal/narrow width, long/localized text, rest, hover, pressed, focus, selected, disabled, loading, empty, error, cancellation, and overlay states.
- Perform at least one build-run-inspect-fix-recheck loop. A compiling window or one scaled screenshot is not visual acceptance.

## UI thread

- All widgets, `QPixmap`, scenes, and ViewModels stay on the GUI thread.
- Render State quickly; never run disk, network, OCR, inference, inpaint, or full-page layout work in event handlers or `paintEvent`.
- Bind async callbacks to receiver lifetime. Closing a screen cancels or detaches view-owned image/preview work and prevents later view publication; project-owned OCR, translation, clear, layout, export, and thumbnail operations continue until explicitly cancelled or the project session closes.
