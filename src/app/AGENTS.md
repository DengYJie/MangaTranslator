# Application and Composition Root

## Responsibility

`app` owns process startup, application-wide lifetime, and dependency assembly. `ApplicationContext` is the composition root and the only place that constructs the complete object graph.

## ApplicationContext

- Construct configuration, logging, repositories, provider clients, services, UseCases, ViewModels, then windows in dependency order.
- Inject dependencies through constructors. Do not expose a global service locator or let leaf classes query `ApplicationContext`.
- Store long-lived dependencies with explicit ownership; prefer `std::unique_ptr` for C++ objects and Qt parent ownership for `QObject`s.
- Keep creation separate from business execution. `ApplicationContext` wires objects; it does not translate pages or mutate projects.
- Define deterministic reverse-order shutdown. Cancel work, stop accepting new jobs, wait for bounded teardown, persist safe settings, then destroy dependencies.
- Make test composition possible by accepting fake port implementations without constructing production network or model adapters.

## Startup order

1. Call `fluent::prepareHighDpiApplication()` before constructing `QApplication`.
2. Create `QApplication` and establish application identity.
3. Call `fluent::initializeResources()` and set the default font from FluentQt's `Typography::FontRole::Body` before constructing widgets.
4. Initialize safe logging, load validated settings, and apply the saved theme through FluentQt's public theme API.
5. Build the dependency graph.
6. Restore non-sensitive UI/session state.
7. Show the main window and enter the event loop.

Do not install a custom `QStyle`/`QProxyStyle`, call `QApplication::setStyle()`, or add an application-wide stylesheet.

Report startup failures before a partially initialized editor is shown. Do not perform model loading, network calls, or project scans synchronously during startup unless they are strictly bounded and measured.

## `main.cpp`

Keep `main.cpp` minimal: process setup, `Application` construction, and event-loop execution. No feature wiring, page processing, provider selection, or widget composition belongs there.
