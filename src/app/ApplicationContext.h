#pragma once

#include <memory>

namespace manga::application {
class IAppSettingsStore;
class IProjectRepository;
class IRecentProjectsStore;
class OperationRegistry;
} // namespace manga::application

namespace manga::data {
class ProjectIoCoordinator;
}

namespace manga::app {

/// Composition root: the only place that constructs the production object graph.
/// Ports are exposed as interface pointers; leaf classes never query this context.
class ApplicationContext {
public:
    /// Production wiring: filesystem repository, QSettings-backed stores.
    ApplicationContext();
    /// Test wiring: inject fake ports; no production adapters are constructed.
    ApplicationContext(std::unique_ptr<application::IProjectRepository> projectRepository,
                       std::unique_ptr<application::IRecentProjectsStore> recentProjectsStore,
                       std::unique_ptr<application::IAppSettingsStore> appSettingsStore);
    ~ApplicationContext();

    ApplicationContext(const ApplicationContext&) = delete;
    ApplicationContext& operator=(const ApplicationContext&) = delete;

    [[nodiscard]] application::IProjectRepository* projectRepository() const {
        return m_projectRepository.get();
    }
    [[nodiscard]] application::IRecentProjectsStore* recentProjectsStore() const {
        return m_recentProjectsStore.get();
    }
    [[nodiscard]] application::IAppSettingsStore* appSettingsStore() const {
        return m_appSettingsStore.get();
    }
    [[nodiscard]] application::OperationRegistry* operationRegistry() const {
        return m_operationRegistry.get();
    }

    /// Deterministic reverse-order teardown entry point. Idempotent.
    void shutdown();

private:
    // Production-only members stay behind the pimpl so the injecting constructor never
    // constructs production adapters.
    struct ProductionAdapters;
    std::unique_ptr<ProductionAdapters> m_production;
    std::unique_ptr<application::IProjectRepository> m_projectRepository;
    std::unique_ptr<application::IRecentProjectsStore> m_recentProjectsStore;
    std::unique_ptr<application::IAppSettingsStore> m_appSettingsStore;
    std::unique_ptr<application::OperationRegistry> m_operationRegistry;
    bool m_shutdown{false};
};

} // namespace manga::app
