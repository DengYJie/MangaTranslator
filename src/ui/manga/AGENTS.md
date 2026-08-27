# Manga Canvas and Graphics Scene

## Responsibility

The manga editor is a presentation and interaction layer built with `QGraphicsView/QGraphicsScene`. It visualizes immutable page/render data and translates pointer, keyboard, and tool gestures into ViewModel intents.

## State ownership

- Domain/application state is authoritative. `QGraphicsItem`s are projections, not entities or persistence records.
- Selection, active tool, page identity, region edits, zoom, and busy state flow through the editor ViewModel State.
- Scene items emit semantic intents such as select, move, resize, edit text, or create region. They do not call UseCases or repositories.
- Use stable region IDs, never scene item pointers or list indices, across async work and persistence.
- Route undo/redo through explicit commands that update application/domain state. Do not depend on accidental item mutations.

## Coordinate systems

- Canonical geometry is expressed in source-image pixels.
- Keep source, scene, view, device, crop, model-input, and export coordinates distinct and name conversions explicitly.
- Centralize transforms and test round trips, aspect-ratio preservation, letterboxing, rotation, DPI, and high-DPI scaling.
- Store normalized/rotated rectangles only if the domain type explicitly defines that representation. Never infer coordinate space from context.
- Snapping, hit testing, handles, and minimum sizes must behave consistently across zoom levels.

## Scene and rendering

- Separate layers for page image, masks/inpaint preview, text/regions, selection handles, guides, and transient tool overlays. Define z-order centrally.
- Use `QImage` for worker-produced image data; create/use `QPixmap` only on the GUI thread.
- Do not regenerate or replace full-resolution pixmaps on every zoom or pointer move.
- Keep item bounding rectangles correct before geometry changes and minimize scene invalidation.
- Avoid one heavyweight child widget per text region. Use custom items/delegates and open an editor only for the active region when practical.
- Large pages and documents need bounded caches, lazy page loading, and cancellation of obsolete renders.
- Painting must not perform I/O, inference, allocation-heavy layout, or mutate application state.

## Interaction

- Define tools as explicit states: select, pan, create region, edit text, erase/mask, and other supported modes.
- Preserve expected shortcuts for zoom, fit, pan, selection, delete, undo, redo, and temporary hand/pan behavior.
- Distinguish click, drag, resize, and context actions with documented thresholds.
- Keep handles and focus cues visible in Light/Dark themes and over both light and dark artwork.
- Clamp edits to valid page geometry unless overflow is an explicit feature.
- Disable or reconcile conflicting edits while a stage owns the same data; never silently discard a user's newer edit when async output arrives.

## Tests and performance gates

- Unit-test coordinate conversions, z-order constants, hit testing, snapping, resize constraints, command undo/redo, and stale-result rejection.
- Exercise very large pages, many regions, rapid zoom/pan, page switching during work, theme switching, and screen closing during callbacks.
- Track interactive latency and memory for representative pages. Optimize measured hot paths without weakening correctness.
