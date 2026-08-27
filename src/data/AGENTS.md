# Data, Import, and Persistence Adapters

## Responsibility

`data` implements repositories, serialization, caching, and local CBZ/PDF/image import or export. It maps external representations to domain/application types.

## Persistence

- Project formats are explicitly versioned and migrations are tested.
- Validate all loaded data before constructing domain objects; reject or repair invalid geometry, missing pages, duplicate IDs, and unsupported versions deliberately.
- Save through a temporary sibling file and atomic replacement where the platform supports it. Never leave a valid project truncated after failure.
- Preserve unknown forward-compatible fields only when the chosen format contract requires it.
- Cache data is disposable and must not be the sole copy of user edits.

## Import and export

- Treat archive entries, PDFs, filenames, metadata, and embedded profiles as untrusted input.
- Prevent path traversal, absolute-path extraction, decompression bombs, unbounded page counts, and oversized allocations.
- Preserve deterministic natural page ordering and retain source-to-import mapping.
- Apply image orientation and color conversion intentionally; do not silently change canonical pixel geometry.
- Export writes to a staging location and commits only after all required outputs succeed, unless partial export is an explicit user choice.
- Keep format-library objects and errors inside the adapter; return typed application/domain results.

## I/O and tests

- No disk work on the GUI thread.
- Use temporary directories and small licensed/generated fixtures in tests.
- Cover corrupt files, unsupported encryption/compression, missing pages, duplicate names, Unicode paths, interrupted writes, version migration, and cleanup.
