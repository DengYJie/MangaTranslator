# Domain Layer

## Responsibility

Model manga projects independently of UI, storage, networking, and inference libraries. Typical concepts include `ComicProject`, `ComicPage`, `TextRegion`, `TextLayout`, `TranslationEntry`, `GlossaryItem`, language identifiers, page order, and export intent.

## Purity and dependencies

- Depend on the C++23 standard library only whenever practical.
- Never include Qt Widgets, Qt Network, SQL, ONNX Runtime, archive/PDF libraries, FluentQt, or adapter headers.
- Prefer domain-specific value types over raw strings and integers for IDs, languages, coordinates, and confidence values.
- Keep transport, persistence, and UI DTOs outside the domain.

## Invariants

- Enforce validity when values are created: normalized rectangles, valid page indices, supported language codes, bounded confidence, and stable region identity.
- Define source-image pixel coordinates as the canonical geometry for page regions and layouts.
- Make state transitions explicit; avoid public mutable fields for entities with invariants.
- Equality must represent domain meaning, not storage artifacts or transient caches.

## Repository contracts

Repository interfaces belong under `domain/repository` only when their vocabulary is domain-oriented. They expose aggregate-level operations and structured errors, not SQL, JSON, filesystem handles, or Qt models.

## Testing

Domain tests require no `QApplication`, filesystem, network, model files, or GPU. Cover construction boundaries, invariants, equality, coordinate rules, and state transitions.
