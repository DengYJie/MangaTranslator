# Network and LLM Adapters

## Responsibility

`network` implements remote application ports, especially LLM translation providers. Transport schemas and SDK objects do not cross into UseCases or ViewModels.

## Provider contracts

- Keep provider selection, endpoint, model name, timeout, and capabilities in validated configuration.
- Credentials come from an approved secret store or runtime environment, never source files, project documents, logs, screenshots, or test fixtures.
- A credential status query reports configured/missing/unavailable without reading or returning the secret blob. Only the network adapter reads a key for immediate request authorization.
- Map domain translation requests to provider messages in one adapter and parse responses into typed results.
- Prefer structured outputs with schema validation. Do not trust model-produced JSON, markup, indices, or region IDs without validation.
- Preserve region identity and ordering; reject ambiguous mappings instead of attaching text to the wrong bubble.
- Include glossary and context deliberately and minimize transmission of unrelated user content.

## Reliability

- Set connection and operation timeouts; support cancellation throughout the request lifetime.
- Retry only transient, idempotent failures with bounded exponential backoff and jitter. Respect provider rate limits and retry hints.
- Never automatically retry a paid or non-idempotent request when its completion status is unknown.
- Surface offline, authentication, authorization, quota, rate-limit, validation, server, timeout, cancellation, and parse failures distinctly.
- Redact authorization headers, secrets, prompts, translations, and image data from default logs.

## Qt threading

If using `QNetworkAccessManager`, create and use it on one event-loop thread and keep replies on that thread. Do not move live replies between threads. Deliver parsed immutable results to the caller with queued, lifetime-safe notification.

## Tests

Use fake transports or a local deterministic server. Tests must not require real credentials, consume paid quota, or depend on a public service being available.
